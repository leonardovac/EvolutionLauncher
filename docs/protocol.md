# Warframe content update protocol

Wire format used by the stock `Tools\Launcher.exe` to keep an install current. Recovered
from the retail launcher and confirmed against the live service on 2026-09-07. RVAs below
refer to `Launcher.exe` with a zero image base.

There is no authentication, no request signing, and no user agent
(`WinHttpOpen(pszAgentW = NULL)`). Cookies are disabled via
`WinHttpSetOption(WINHTTP_OPTION_DISABLE_FEATURE, WINHTTP_DISABLE_COOKIES)`.

## Hosts

| Role | Public | Test | Dev |
|---|---|---|---|
| Index | `https://origin.warframe.com` | `https://origin-test.warframe.com` | `https://origin-dev.warframe.com` |
| Content | `http://content.warframe.com` | `http://content-test.warframe.com` | `http://content-dev.warframe.com` |

The launcher retries once with the opposite scheme when a connection fails.

## Index

    GET https://origin.warframe.com/origin/<8 uppercase hex>/index.txt.lzma

The hex group is `(rand() << 16) | rand()` — a cache-buster nonce, not a build id. Any
value is accepted; the response carries `Cache-Control: no-cache`.

The body is an **LZMA-alone** stream: 13-byte header (1 props byte, 4-byte LE dictionary
size, 8-byte LE uncompressed size) then raw LZMA1. Retail currently ships
`lc=3 lp=0 pb=2`, 64 MiB dictionary. Decoded, it is `\n`-separated UTF-8 text; a recent
snapshot is 258 lines / ~20 KB.

## Index line

    <path>.<MD5 32 hex>.<lzma|bulk>,<wireSize>

Example lines:

    /Warframe.x64.exe.F96EA142994065F342FDB2A857191C7E.lzma,14034887
    /Cache.Windows/B.Misc.cache.918ECBFC7D8E59019C30A6D5EF31E58D.bulk,4733502268

A line is accepted only when `indexOf(".lzma,")` equals `lastIndexOf('.') + 33` before it,
i.e. exactly 32 hex characters sit between the final `.` and the extension. Lines failing
that are dropped silently. `.bulk,` is tried when `.lzma,` is absent.

Three fields are derived from each line (`WF_ParseIndexLine`, 0x2655C):

| Field | Derivation |
|---|---|
| Request path | `line[0 .. indexOf(ext) + 5)` — keeps the extension, **drops the comma and size** |
| Install path | `line[0 .. lastDotIndex)`, `/` → `\`, appended to the branch root |
| MD5 | the 32 hex characters at `lastDotIndex + 1` |
| Wire size | decimal after the comma |

The truncation is easy to miss when reading a decompilation: the length argument sits in
`r8` (`lea r8, [r13+5]`) and Hex-Rays drops it from the `WF_WStrAssign` call.

`.../B.CharacterCodesCache.cache.6D0E….bulk,14781` therefore means:

    GET  http://content.warframe.com/Cache.Windows/B.CharacterCodesCache.cache.6D0E….bulk
    →    <root>\Cache.Windows\B.CharacterCodesCache.cache

## Size and hash semantics

**`wireSize` is the `Content-Length` of the CDN object**, not the size of the installed
file — the compressed length for `.lzma`, the raw length for `.bulk`. For the sample
above, `,8346` matched `Content-Length: 8346` while the decompressed file was 43970 bytes.

**The MD5 covers the installed bytes**: post-decompression for `.lzma`, raw for `.bulk`.
Both were verified live. CDN objects are content-addressed by that digest, so files from
older builds stay fetchable. As a self-check, the index lists
`/Tools/Launcher.exe.A605C7ACA82F918462771ECC927AB12C.lzma` and `A605C7AC…` is the MD5 of
the retail `Launcher.exe` on disk.

## Content fetch

    GET http://content.warframe.com<request path>

`200` is required for a fresh request. To resume, send `Range: bytes=<offset>-` and
require `206` plus a `Content-Range: bytes <first>-<last>/<total>` whose `first` equals the
requested offset; anything else is a protocol error and the partial file is truncated and
retried. `404` and `410` are permanent failures. Other statuses are retried — four attempts
for the index, two for content files.

The body handler is chosen by whether the request path contains `.lzma`: LZMA-alone stream
decode (`WF_ReadBodyLzma`, 0x29C54, 16 KiB in / 32 KiB out) or a straight copy
(`WF_ReadBodyRaw`, 0x29958, 32 KiB). Both run MD5 over the bytes that reach disk.

## Content scheme

`ForceHTTPS` in the launcher's registry key selects the scheme used for the content host.
The stock settings dialog's **Allow Network Caches** checkbox stores its negation —
checking it writes `ForceHTTPS = 0`. We honour the setting for `content.warframe.com` and
its `-test`/`-dev` siblings, so a checked box lets a transparent proxy or CDN cache serve
those objects over `http://`. The origin index fetch stays on `https://` unconditionally
regardless of the setting: it is one small compressed request where caching buys nothing,
and downgrading it would trade away transport security for no benefit. `flipScheme` in
`updater.cpp` still retries the opposite scheme on a connect failure either way, so a wrong
guess degrades to one retry rather than a hard failure.

## Install paths

The root is **not** always under `%LOCALAPPDATA%`. A Steam install keeps content in the
Steam library folder, and only logs and configuration under `%LOCALAPPDATA%\Warframe`. The
launcher records its own location in the registry, and the root is that path's grandparent:

    HKCU\Software\Digital Extremes\Warframe\Launcher
      LauncherExe = ...\steamapps\common\Warframe\Tools\Launcher.exe
      root        = ...\steamapps\common\Warframe

    %LOCALAPPDATA%\Warframe\Downloaded\<Public|Test|Dev>      (non-Steam fallback)

Under the root, content lives in `Cache.Windows`, `Tools` and `Lotus`, with
`Warframe.x64.exe` at the top. The same key also holds `Language` (the two-letter code the
applicability filter needs), `GraphicsAPI`, and `EnableBulkDownload`.

## Applicability filter

`WF_EntryAppliesToClient` (0x26D18) drops entries before any I/O:

- `/steam` unless this is a Steam install; `/eossdk` unless EOS is in use
- `d3d12sdklayers` on the Public branch
- `dx12.toc` / `dx12.cache` unless DX12 is selected
- files whose stem ends in `_<lang>` that do not match the selected language, with `_en`
  as the fallback for `misc`

159 of 258 lines in the current index are language-suffixed, so this filter is not
optional.

Those switches come from the machine rather than from the index, and they resolve as
follows (`sub_6FA0` loads the settings, `sub_7500` saves them):

| Switch | Source |
|---|---|
| language | `Language` in the launcher's registry key, else `GetUserDefaultLangID` |
| DX12 | `GraphicsAPI` → `dword_10F570`; the `dx12` caches are kept only when it is `1` |
| Steam / Epic | the launcher's own `-registry:<tag>` argument, **not** the registry |
| branch | `ServerCluster`, clamped to Public unless `dword_10F564` allows more |

Steam and Epic are the awkward ones: `sub_25248` sets them by searching that command-line
tag for `Steam` and `Epic`, so a client started by Steam runs as `-registry:Steam`. A
replacement updater is not launched that way and has to infer the platform some other way
— matching `steamapps` or `Epic` against the recorded `LauncherExe` path is the closest
equivalent.

`dword_10F564` is the dev-controls ceiling, not a branch: `sub_6F00` sets it to 2 for
`-dev`, 1 when `EnableTestCluster` is set, and leaves it 0 on retail. It gates two things —
how high `ServerCluster` may go, and, together with the Public branch, whether
`d3d12sdklayers` is skipped. On a retail client both terms hold, so that file is never
fetched; a copy on disk is a leftover the purge keeps because the index lists it.

`EnableBulkDownload` (`byte_105EA2`) is a third gate worth knowing: when it is off, the
launcher skips every category-4 `.cache` / `.toc` entry that is not inside an offline
archive, and lets the game stream content instead.

Getting these wrong is silent: the run succeeds and simply installs less than it should.
On a Steam machine with DX12 selected, defaulting them all off skips `steam_api64.dll`
and six `Dx12` cache and toc file pairs.

## Update decision

`WF_BuildDownloadQueue` (0x28410) walks the index in category order:

| Category | Match |
|---|---|
| 0 | path contains `/cef` |
| 1 | `/Tools/Launcher.exe`, `/Tools/RemoteCrashSender.exe`, `/Tools/Windows/x64/dbghelp.dll`, `symsrv.dll`, `EOSSDK`, `/Tools/CEF3_1/`, or `/steam` |
| 2 | the main game executable |
| 3 | everything else |
| 4 | `.cache` / `.toc` |

For each surviving entry: a missing file is queued; otherwise the local file is MD5'd and
queued on mismatch. **Category 4 is never hashed** — existence alone is accepted, which is
why the launcher starts quickly against a multi-gigabyte cache and why a full re-verify is
a separate, explicit action. When one member of a `.cache`/`.toc` pair is queued, its
sibling is queued with it.

## Install

Download to `<install path>.tmp`, verify the accumulated MD5 against the index digest, then
`MoveFileExW(tmp, dest, MOVEFILE_REPLACE_EXISTING)`, falling back to `ReplaceFileW`. A
mismatch discards the temporary file and reports `DownloadCorrupted`.

## Vestigial file purge

Not part of updating, but it shares the manifest. `WF_PurgeVestigialFiles` (0x2C500) walks
the branch root and deletes any file matching `*.exe`, `*.dll`, `*.dat`, `*.bin`, `*.pak`,
`*.zip`, `*charactercodescachedx*`, `*dx9*` (and separately `*.tmp`, `*.dmp`) that the
index does not list, keeping only `.Texture.`, `.Texture_` and `\unins00`. Empty
directories go next. Any unmanaged file placed beside `Warframe.x64.exe` is removed on the
next launch.

## Not needed here

`.bulk` in the index is simply an uncompressed object. The separate `archive://` path
(`WF_LoadOfflineInstaller`, 0x2D3FC) is the offline installer: a local manifest plus split
volumes `<base>b00`, `b01`, … addressed as one concatenated stream.
`WF_CopyFromPublicBranch` (0x2A9F0) reuses an already-downloaded Public file when
populating Test or Dev.

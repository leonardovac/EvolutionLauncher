# Digital Extremes content update protocol

Wire format used by the stock `Tools\Launcher.exe` to keep an install current. Recovered
from the retail launcher and confirmed against the live service on 2026-09-07.

There is no authentication, no request signing, and no user agent
(`WinHttpOpen(pszAgentW = NULL)`). Cookies are disabled via
`WinHttpSetOption(WINHTTP_OPTION_DISABLE_FEATURE, WINHTTP_DISABLE_COOKIES)`.

## Hosts

Every title serves the same protocol from its own pair of hosts. Only the stem differs.

**Warframe**

| Role | Public | Test | Dev |
|---|---|---|---|
| Index | `https://origin.warframe.com` | `https://origin-test.warframe.com` | `https://origin-dev.warframe.com` |
| Content | `http://content.warframe.com` | `http://content-test.warframe.com` | `http://content-dev.warframe.com` |

**Soulframe**

| Role | Public | Test | Dev |
|---|---|---|---|
| Index | `https://origin.soulframe.com` | `https://origin-test.soulframe.com` | `https://origin-dev.soulframe.com` |
| Content | `http://content.soulframe.com` | `http://content-test.soulframe.com` | `http://content-dev.soulframe.com` |

The launcher retries once with the opposite scheme when a connection fails.

## Index

    GET https://origin.warframe.com/origin/<8 uppercase hex>/index.txt.lzma

The hex group is `(rand() << 16) | rand()`, a cache-buster nonce. Any value is accepted;
the response carries `Cache-Control: no-cache`.

The body is an LZMA-alone stream: 13-byte header (1 props byte, 4-byte LE dictionary
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

Three fields are derived from each line:

| Field | Derivation |
|---|---|
| Request path | `line[0 .. indexOf(ext) + 5)`, which keeps the extension and **drops the comma and size** |
| Install path | `line[0 .. lastDotIndex)`, `/` → `\`, appended to the branch root |
| MD5 | the 32 hex characters at `lastDotIndex + 1` |
| Wire size | decimal after the comma |

`.../B.CharacterCodesCache.cache.6D0E….bulk,14781` therefore means:

    GET  http://content.warframe.com/Cache.Windows/B.CharacterCodesCache.cache.6D0E….bulk
    →    <root>\Cache.Windows\B.CharacterCodesCache.cache

## Size and hash semantics

`wireSize` is the `Content-Length` of the CDN object: the compressed length for `.lzma`,
the raw length for `.bulk`. For the sample above, `,8346` matched `Content-Length: 8346`
while the decompressed file was 43970 bytes.

The MD5 covers the installed bytes: post-decompression for `.lzma`, raw for `.bulk`.
Both were verified live. CDN objects are content-addressed by that digest, so files from
older builds stay fetchable. As a self-check, the index lists
`/Tools/Launcher.exe.A605C7ACA82F918462771ECC927AB12C.lzma` and `A605C7AC…` is the MD5 of
the retail `Launcher.exe` on disk.

## Content fetch

    GET http://content.warframe.com<request path>

`200` is required for a fresh request. To resume, send `Range: bytes=<offset>-` and
require `206` plus a `Content-Range: bytes <first>-<last>/<total>` whose `first` equals the
requested offset; anything else is a protocol error and the partial file is truncated and
retried. `404` and `410` are permanent failures. Other statuses are retried, four attempts
for the index and two for content files.

The body handler is chosen by whether the request path contains `.lzma`: LZMA-alone stream
decode, or a straight copy. Both run MD5 over the bytes that reach disk.

## Content scheme

`ForceHTTPS` in the launcher's registry key selects the scheme used for the content host.
The stock settings dialog's Allow Network Caches checkbox stores its negation: checking it
writes `ForceHTTPS = 0`. We honour the setting for `content.warframe.com` and
its `-test`/`-dev` siblings, so a checked box lets a transparent proxy or CDN cache serve
those objects over `http://`. The origin index fetch stays on `https://` regardless of the
setting. Either way a connect failure retries once with the opposite scheme.

## Install paths

The launcher records its own location in the registry, and the root is that path's
grandparent. A Steam install keeps content in the Steam library folder, with logs and
configuration under `%LOCALAPPDATA%\Warframe`:

    HKCU\Software\Digital Extremes\Warframe\Launcher
      LauncherExe = ...\steamapps\common\Warframe\Tools\Launcher.exe
      root        = ...\steamapps\common\Warframe

    %LOCALAPPDATA%\Warframe\Downloaded\<Public|Test|Dev>      (non-Steam fallback)

The same key also holds `Language` (the two-letter code the applicability filter needs),
`GraphicsAPI`, and `EnableBulkDownload`.

## Game launch

The retail command line is composed in this order and run with
`CreateProcessW(nullptr, line, …, CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS)`:

    "<root>\Warframe.x64.exe" -windowMode:N -shaderCache:N -graphicsDriver:dx11|dx12
      -gpuPreference:N -cluster:public|test|dev -language:xx [-languageVO:xx]
      [-clienttype:<tag>] [-forceHTTPS]

| Fragment | Source |
|---|---|
| exe path | branch root + `\<title>.x64.exe` |
| `-windowMode` | the registry `WindowMode` |
| `-shaderCache` | the registry `ShaderCache` |
| `-graphicsDriver` | `dx11` or `dx12`, from the registry `GraphicsAPI` |
| `-gpuPreference` | the registry `GPUPreference` |
| cluster | the selected branch |
| `-language` | the registry `Language` |
| `-languageVO` | omitted when the VO index is 15, the "no override" sentinel |
| `-clienttype` | the launcher's own `-registry:<tag>` argument |
| `-forceHTTPS` | the registry `ForceHTTPS` |

`-allowmultiple` is emitted only when the launcher already tracks a live instance, and the
`-dedicated`, `-dscfg`, `-epic`, `-onlive` and `-relaunch` branches belong to the dedicated
server and store relaunch paths. None of them apply here.

On success the stock launcher reaches `PostQuitMessage(0)` and exits once the game is up.

## Cache defragment

The launcher's Optimize action runs locally. It prompts, purges its unused-binary list,
checks free space against 1.5x the largest known file, removes `<root>\Defrag.log`, and
spawns the game with:

    -applet:/EE/Types/Framework/CacheDefraggerIOCP /Tools/CachePlan.txt

A trailing ` benchmark` is appended when the launcher itself was started with `-benchmark`.
`Tools/CachePlan.txt` is an index-listed file; the applet rewrites the `.cache` set in place
and writes `Defrag.log`.

## Sideload patch

A local patch of the game executable, applied after each update. The retail launcher links
the game with `/DEPENDENTLOADFLAG:0x800`
(LOAD_LIBRARY_SEARCH_SYSTEM32), which forces DLL resolution to System32 and defeats a proxy
DLL dropped beside the executable. This launcher zeroes that field so the default search
order (including the application directory) applies.

The field is `IMAGE_LOAD_CONFIG_DIRECTORY64::DependentLoadFlags`, at offset 0x4E into the
load-config directory (data directory index 10, `IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG`).
Patching flips the two bytes to `00 00`. Because this changes the file's MD5, the pre-patch
(index) hash and the post-patch hash are stored in `launcher.json`'s `patched` section so
the check treats the patched file as up to date, and re-patches automatically once the index
hash moves past the recorded `source`.

## Applicability filter

Entries are dropped before any I/O:

- `/steam` unless this is a Steam install; `/eossdk` unless EOS is in use
- `d3d12sdklayers` on the Public branch
- `dx12.toc` / `dx12.cache` unless DX12 is selected
- files whose stem ends in `_<lang>` that do not match the selected language, with `_en`
  as the fallback for `misc`

159 of 258 lines in the current index are language-suffixed.

Those switches come from the machine, and resolve as follows:

| Switch | Source |
|---|---|
| language | `Language` in the launcher's registry key, else `GetUserDefaultLangID` |
| DX12 | the registry `GraphicsAPI`; the `dx12` caches are kept only when it is `1` |
| Steam / Epic | the launcher's own `-registry:<tag>` argument |
| branch | `ServerCluster`, clamped to Public unless the dev controls allow more |

Steam and Epic are the awkward ones: they are set by searching that command-line tag for
`Steam` and `Epic`, so a client started by Steam runs as `-registry:Steam`. A
replacement updater has to infer the platform some other way. Matching `steamapps` or
`Epic` against the recorded `LauncherExe` path is the closest equivalent.

The dev-controls ceiling is a separate value: 2 for `-dev`, 1 when `EnableTestCluster` is
set, 0 on retail. It gates how high `ServerCluster` may go and, with the Public branch,
whether `d3d12sdklayers` is skipped. On a retail client both terms hold, so that file stays
unfetched; a copy on disk is a leftover the purge keeps because the index lists it.

`EnableBulkDownload` is a third gate: when it is off, the launcher skips every category-4
`.cache` / `.toc` entry that is not inside an offline archive, and lets the game stream
content instead.

Getting these wrong is silent: the run succeeds and installs less than it should.
On a Steam machine with DX12 selected, defaulting them all off skips `steam_api64.dll`
and six `Dx12` cache and toc file pairs.

## Update decision

The index is walked in category order:

| Category | Match |
|---|---|
| 0 | path contains `/cef` |
| 1 | `/Tools/Launcher.exe`, `/Tools/RemoteCrashSender.exe`, `/Tools/Windows/x64/dbghelp.dll`, `symsrv.dll`, `EOSSDK`, `/Tools/CEF3_1/`, or `/steam` |
| 2 | the main game executable |
| 3 | everything else |
| 4 | `.cache` / `.toc` |

For each surviving entry: a missing file is queued; otherwise the local file is MD5'd and
queued on mismatch. **Category 4 is existence-checked**: the file's presence alone is
accepted, which is why a full re-verify is a separate action. When one member of a
`.cache`/`.toc` pair is queued, its sibling is queued with it.

## Install

Download to `<install path>.tmp`, verify the accumulated MD5 against the index digest, then
`MoveFileExW(tmp, dest, MOVEFILE_REPLACE_EXISTING)`, falling back to `ReplaceFileW`. A
mismatch discards the temporary file and reports `DownloadCorrupted`.

## Vestigial file purge

Separate from updating, but it shares the manifest. The launcher walks the branch root and
deletes any file matching `*.exe`, `*.dll`, `*.dat`, `*.bin`, `*.pak`, `*.zip`,
`*charactercodescachedx*`, `*dx9*` (and separately `*.tmp`, `*.dmp`) that the index does
not list, keeping only `.Texture.`, `.Texture_` and `\unins00`. Empty
directories go next. Any unmanaged file placed beside the game executable is removed on
the next launch.

## Out of scope

`.bulk` in the index is an uncompressed object. The separate `archive://` path is the
offline installer: a local manifest plus split volumes `<base>b00`, `b01`, … addressed as
one concatenated stream. There is also a path that reuses an already-downloaded Public file
when populating Test or Dev.

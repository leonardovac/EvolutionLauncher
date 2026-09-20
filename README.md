# Evolution Launcher

A custom launcher for Warframe and Soulframe.

Has all the capabilities of the original and works as a complete replacement for it.
The protocol is written up in [docs/protocol.md](docs/protocol.md).

> [!WARNING]
> Do **NOT** replace the original `Tools\Launcher.exe` in the game folder with this. The first thing it would do is restore the original.

## Why

The original launcher purges unlisted files. It walks the install and deletes anything it does not recognise, so a ReShade DLL or a mod sitting beside the game's executable is gone after the next launch/update.

This one purges only inside directories the index itself populates, and leaves the root and anything beside the executable alone.
A `protect` list covers whatever else you want kept, and an `exclude` list covers content you would rather skip.
Both live in `launcher.json` beside the executable.

Downloads run in parallel.

## Building

MSVC, built with `/std:c++latest`. Run `build.bat` from a developer prompt, or `build.bat debug`. Output is `bin\Launcher.exe`.
There is no solution file and no CMake.

## Running

With no arguments it opens the window. Everything else is a command-line run:

    Launcher.exe --check                 plan the work, download nothing
    Launcher.exe --verify                hash the caches too, not just check they exist
    Launcher.exe --stale                 list files the index does not name
    Launcher.exe --jobs 8                more parallel downloads
    Launcher.exe --title soulframe
    Launcher.exe --help

Exit codes are `0` ok, `1` failed, `2` cancelled, `3` work to do (`--check` only).

The install root comes from `HKCU\Software\Digital Extremes\<title>\Launcher\LauncherExe`, so an Epic/Steam install is found where the platform actually put it. Override it with `--root`.

## Notes

Bytes land in a `.tmp`, get hashed, and are only then moved. A mismatch throws the file away, so interrupting a run leaves the install consistent.

Large uncompressed entries are split into ranged chunks fetched at once.
Cancelling one of those keeps the partial file and a small `.ranges` file beside it, so the next run picks up where it stopped.

Cache files are checked for existence unless you pass `--verify`, because hashing a full cache is tens of gigabytes of reads.
Defragmenting rewrites the cache in place, so its bytes stop matching the index; a hashed cache that differs is reported and left alone.

## Thanks

Digital Extremes, for producing both games.

Igor Pavlov, for LZMA and the SDK around it.

## Disclaimer

Not affiliated with, endorsed by, or connected to Digital Extremes. Warframe, Soulframe, and the artwork under `assets/` are their property, used here only to point the launcher at their own games.
The font is Roboto Condensed under the SIL Open Font License; LZMA is public domain.

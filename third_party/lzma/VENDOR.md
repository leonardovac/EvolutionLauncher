# LZMA SDK (vendored)

Warframe's content CDN serves `.lzma` objects in the **LZMA-alone** container: a 13-byte
header (1 props byte, 4-byte LE dictionary size, 8-byte LE uncompressed size) followed by
a raw LZMA1 stream. Windows' Compression API does not decode LZMA1, so the reference
decoder is vendored.

Drop these five files from the LZMA SDK into this directory:

    LzmaDec.c
    LzmaDec.h
    7zTypes.h
    Compiler.h
    Precomp.h

Source: <https://www.7-zip.org/sdk.html> — `lzma<version>.7z`, files under `C/`.
Use SDK 19.00 or newer: `src/update/lzma.cpp` passes an `ISzAllocPtr`-style allocator,
which older SDKs spell differently.
Take the plain C reference decoder only; nothing else from the SDK is used.

`build.bat` compiles `LzmaDec.c` as C and fails with a clear message if it is absent.
These files are gitignored: leave them in their reference form, do not restyle them.

# Build baseline

The original solution is a Visual Studio 2008 `.sln` with `.vcproj` projects.
Current MSBuild cannot load those project files, so the supported build path is
the checked-in NMAKE files. `WinDjView.Modern.sln` is a VS2022 Makefile-project wrapper for that supported build path.

On the local Visual Studio 18 Enterprise installation, the following commands
were run successfully on 2026-08-01:

- Release Win32: `nmake /nologo /f makefile` in `src/libdjvu`, then `src`.
- Release x64: `nmake /nologo /f makefile X64=1` in `src/libdjvu`, then `src`.
- Debug Win32: `nmake /nologo /f makefile DEBUG=1` in `src/libdjvu`, then
  `src`.
- Debug x64: `nmake /nologo /f makefile X64=1 DEBUG=1` in `src/libdjvu`, then
  `src`.

All four commands create a statically linked `libdjvu` and the corresponding
`WinDjView.exe`. The x64 path builds the matching
`third_party/libjpeg-turbo/jpeg64.lib` or Debug counterpart from bundled
source instead of attempting to link a Win32 binary.

The libjpeg-turbo 3.1.4.1 adapter configures the static libjpeg 6.2 API/ABI
with `WITH_JPEG7=OFF`, `WITH_JPEG8=OFF`, `/MT` for Release, and `/MTd` for
Debug. See [libjpeg-turbo.md](libjpeg-turbo.md) for the full integration notes.

The Debug x64 build completes without newly introduced JPEG warnings. Existing
warnings in inherited DjVuLibre code about legacy CRT APIs remain recorded for
a separate upstream-portability audit rather than hidden globally.
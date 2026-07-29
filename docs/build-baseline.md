# Build baseline

The original solution is a Visual Studio 2008 `.sln` with `.vcproj` projects.
Current MSBuild cannot load those project files, so the supported build path is
the checked-in NMAKE files. `WinDjView.Modern.sln` is a VS2022 Makefile-project wrapper for that supported build path.

On the local Visual Studio 18 Enterprise installation, the following commands
were run successfully on 2026-07-29:

- Release Win32: `nmake /nologo /f makefile` in `src/libdjvu`, then `src`.
- Release x64: `nmake /nologo /f makefile X64=1` in `src/libdjvu`, then `src`.
- Debug Win32: `nmake /nologo /f makefile DEBUG=1` in `src/libdjvu`, then
  `src`.
- Debug x64: `nmake /nologo /f makefile X64=1 DEBUG=1` in `src/libdjvu`, then
  `src`.

All four commands create a statically linked `libdjvu` and the corresponding
`WinDjView.exe`. The x64 path builds `third_party/jpeg-6b/jpeg64.lib` or its
Debug counterpart from the bundled source instead of attempting to link the
legacy Win32 binary.

The Debug x64 build completes with warnings in inherited DjVuLibre and IJG
JPEG 6b code about size conversions and legacy CRT APIs. These are recorded
for a separate upstream-portability audit rather than hidden globally.
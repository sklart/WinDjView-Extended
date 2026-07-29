# Build baseline

The original solution is a Visual Studio 2008 `.sln` with `.vcproj` projects.
Current MSBuild cannot load those project files, so the supported build path is
the checked-in NMAKE files.

On the local Visual Studio 18 Enterprise installation, the following commands
were run successfully on 2026-07-29:

- Release Win32: `nmake /nologo /f makefile` in `src/libdjvu`, then `src`.
- Release x64: `nmake /nologo /f makefile X64=1` in `src/libdjvu`, then `src`.

Both commands create a statically linked `libdjvu` and the corresponding
`WinDjView.exe`. The x64 path builds `third_party/jpeg-6b/jpeg64.lib` from the
bundled source instead of attempting to link the legacy Win32 binary.

Debug is not yet part of the supported NMAKE baseline. See [BUILDING.md](../BUILDING.md).
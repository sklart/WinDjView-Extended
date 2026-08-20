# Building WinDjView Extended

The checked-in build is an NMAKE build for Visual Studio C++. It builds the
application and the bundled static DjVuLibre library. The legacy VS2008
`.sln`/`.vcproj` files are retained for reference; current MSBuild does not
consume them directly.

## Prerequisites

Install Visual Studio 2022 or newer with **Desktop development with C++** and
MFC. Open **Developer Command Prompt for VS**, then run the commands below.
This works with Community, Professional, and Enterprise editions. For scripted
discovery, use `vswhere.exe` instead of hard-coding a Visual Studio edition or
installation-directory version.

## Visual Studio 2022 solution

`WinDjView.Modern.sln` opens the NMAKE build through a modern VS2022 Makefile
project and exposes Debug/Release for Win32/x64. The historical
`src\WinDjView.sln` and `.vcproj` files are preserved unchanged.

The project initializes `VsDevCmd.bat` itself, including the MFC include paths.
It can therefore be built from MSBuild as well as from a Developer Command
Prompt.

## Release Win32

```bat
cd src\libdjvu
nmake /nologo /f makefile SIMD=1
cd ..
nmake /nologo /f makefile SIMD=1
```

Output: `src\Release\WinDjView.exe`.

## Release x64

```bat
cd src\libdjvu
nmake /nologo /f makefile X64=1 SIMD=1
cd ..
nmake /nologo /f makefile X64=1 SIMD=1
```

Output: `src\Release_x64\WinDjView.exe`. The build also creates the matching
x64 bundled libjpeg-turbo static library from source.

## Debug Win32 and x64

Pass `DEBUG=1` to both makefiles. Add `X64=1` for x64:

```bat
cd src\libdjvu
nmake /nologo /f makefile DEBUG=1
cd ..
nmake /nologo /f makefile DEBUG=1
```

The outputs are `src\Debug\WinDjView.exe` and
`src\Debug_x64\WinDjView.exe`. Debug builds use separate static
`libdjvud*.lib` and `jpegd*.lib` artifacts. libjpeg-turbo is built internally
through its NMAKE adapter, linked statically, and configured for the libjpeg
6.2 API/ABI.

Release builds use NASM SIMD. If `nasm` is unavailable, the JPEG adapter stops
before CMake with an explanation. For diagnostic or legacy environments, omit
`SIMD=1` from both NMAKE commands to make a supported non-SIMD Release build.

## libjpeg-turbo

The bundled JPEG implementation is libjpeg-turbo 3.2.0. See
[docs/libjpeg-turbo.md](docs/libjpeg-turbo.md) for its NMAKE/CMake adapter,
static-link settings, optional NASM SIMD mode, licensing notice, and update
procedure.

## Windows 7

The build keeps the application's Windows 7-era API target; runtime validation
on a clean Windows 7 system remains a separate manual test because the local
build uses a modern Visual Studio runtime/toolset.

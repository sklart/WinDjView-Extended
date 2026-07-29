# Building WinDjView Extended

The checked-in build is an NMAKE build for Visual Studio C++. It builds the
application and the bundled static DjVuLibre library. The legacy VS2008
`.sln`/`.vcproj` files are retained for reference; current MSBuild does not
consume them directly.

## Prerequisites

Install Visual Studio 2022 or newer with **Desktop development with C++** and
MFC. Run commands in a Developer Command Prompt or initialise the environment
with `VsDevCmd.bat`.

## Release Win32

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
cd src\libdjvu
nmake /nologo /f makefile
cd ..
nmake /nologo /f makefile
```

Output: `src\Release\WinDjView.exe`.

## Release x64

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cd src\libdjvu
nmake /nologo /f makefile X64=1
cd ..
nmake /nologo /f makefile X64=1
```

Output: `src\Release_x64\WinDjView.exe`. The build also creates the x64
variant of the bundled IJG JPEG 6b static library from source.

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
`libdjvud*.lib` and `jpegd*.lib` artifacts.

## Windows 7

The build keeps the application's Windows 7-era API target; runtime validation
on a clean Windows 7 system remains a separate manual test because the local
build uses a modern Visual Studio runtime/toolset.
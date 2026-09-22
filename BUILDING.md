# Building WinDjView Extended

The primary build is Native MSBuild for Visual Studio 2022/v143. It builds the
application and bundled static DjVuLibre library. Legacy NMAKE and VS2008
projects are retained as compatibility paths.

## Prerequisites

Install Visual Studio 2022 or newer with **Desktop development with C++** and
MFC. Open **Developer Command Prompt for VS**, then run the commands below.
This works with Community, Professional, and Enterprise editions. For scripted
discovery, use `vswhere.exe` instead of hard-coding a Visual Studio edition or
installation-directory version.

## Visual Studio 2022 solution

`WinDjView.Modern.sln` builds `WinDjView.Native` for Debug/Release and
Win32/x64. The historical
`src\WinDjView.sln` and `.vcproj` files are preserved unchanged.

The solution also contains `WinDjViewRU.Modern`, a native MSBuild resource-DLL
project. Its outputs are `WinDjViewRU-Win32.dll` and `WinDjViewRU-x64.dll` in
the matching application output directories. `WinDjView.Native` is the primary
native MSBuild application project, separate from the legacy NMAKE wrapper.
`libdjvu.Modern` is a native static-library MSBuild project which compiles the
bundled DjVuLibre sources directly; it invokes only the existing JPEG adapter
to retain the checked-in libjpeg-turbo 3.2.0 configuration and Release SIMD
policy.

Build `WinDjView.Native` directly while validating the migration. It has a
project reference to `libdjvu.Modern` and is enabled for solution-wide Build.
The legacy NMAKE wrapper remains visible but excluded, preventing two producers
from writing the same EXE output.

The native application project uses `/W4`. Its first baseline showed that
`/permissive-` causes broad historical source incompatibilities, so it remains
disabled pending a focused compatibility pass; `/WX` is likewise not enabled.
The correctness-related warning review is recorded in
[docs/native-warning-audit.md](docs/native-warning-audit.md).

Initialize a VS Developer Command Prompt (with MFC installed) before building
the native projects. For example:

```bat
msbuild src\WinDjView.Native.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
msbuild src\WinDjView.Native.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64
msbuild src\WinDjView.Native.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32
msbuild src\WinDjView.Native.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64
```

Native MSBuild has explicit Debug/Release build parity with the legacy build:

| Property | NMAKE | Native MSBuild |
| --- | --- | --- |
| Debug runtime and define | `/MTd`, `_DEBUG` | `/MTd`, `_DEBUG` |
| Release runtime and define | `/MT`, `NDEBUG` | `/MT`, `NDEBUG` |
| RTTI | disabled | disabled |
| Exceptions | synchronous C++ | synchronous C++ |
| Release optimization | optimized, static codecs | optimized, static codecs |
| Application subsystem | Windows | Windows |

Legacy NMAKE remains a compatibility build; Native MSBuild is the production reference.

Build the resource-only Russian DLL with the same configuration and platform:

```bat
msbuild src\Languages\Russian\WinDjViewRU.Modern.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64
```

The application's historical manifest remains embedded by `WinDjView.rc2`.
The resource compiler receives the platform's `WIN64` define, so x64 embeds
the amd64 Common Controls dependency rather than the x86 one. The native
project suppresses MSBuild's additional generated manifest to avoid creating a
duplicate resource.

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

@echo off
setlocal EnableExtensions
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "BUILD_FLAVOR=%~3"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%PLATFORM%"=="" set "PLATFORM=Win32"
if "%BUILD_FLAVOR%"=="" set "BUILD_FLAVOR=legacy"
if /I "%CONFIGURATION%"=="Debug" (
  set "CRT=/MTd"
  set "DEBUG_SUFFIX=d"
  set "CONFIG_DIR=Debug"
) else if /I "%CONFIGURATION%"=="Release" (
  set "CRT=/MT"
  set "DEBUG_SUFFIX="
  set "CONFIG_DIR=Release"
) else (
  echo Configuration must be Debug or Release.
  exit /b 2
)
if /I "%PLATFORM%"=="Win32" (
  set "ARCH=x86"
  set "LIB_SUFFIX="
) else if /I "%PLATFORM%"=="x64" (
  set "ARCH=x64"
  set "LIB_SUFFIX=64"
  set "CONFIG_DIR=%CONFIG_DIR%_x64"
) else (
  echo Platform must be Win32 or x64.
  exit /b 2
)
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo Visual Studio C++ tools were not found.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
set "JPEG_BUILD=src\third_party\libjpeg-turbo\build\%CONFIG_DIR%"
set "TEST_BASENAME=tools\tests\libdjvu_core_regression-%CONFIGURATION%-%PLATFORM%"
if /I "%BUILD_FLAVOR%"=="legacy" (
  set "DJVU_LIBRARY=src\libdjvu\libdjvu%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
) else if /I "%BUILD_FLAVOR%"=="native" (
  set "DJVU_LIBRARY=src\%CONFIG_DIR%\libdjvu%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
) else (
  echo Build flavor must be legacy or native.
  exit /b 2
)
cl /nologo /W4 /EHsc %CRT% /DWIN32 /D_WINDOWS /D_CONSOLE /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /Fo"%TEST_BASENAME%.obj" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\libdjvu_core_regression.cpp" /Fe"%TEST_BASENAME%.exe" "%DJVU_LIBRARY%" "src\third_party\libjpeg-turbo\jpeg%DEBUG_SUFFIX%%LIB_SUFFIX%.lib" advapi32.lib
if errorlevel 1 exit /b %errorlevel%
"%TEST_BASENAME%.exe"

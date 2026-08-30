@echo off
setlocal EnableExtensions
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "BUILD_FLAVOR=%~3"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"
if "%BUILD_FLAVOR%"=="" set "BUILD_FLAVOR=native"
if /I "%CONFIGURATION%"=="Debug" (
  set "CRT=/MTd"
  set "DEBUG_SUFFIX=d"
  set "CONFIG_DIR=Debug"
  set "CONFIG_DEFINE=/DNDEBUG"
) else if /I "%CONFIGURATION%"=="Release" (
  set "CRT=/MT"
  set "DEBUG_SUFFIX="
  set "CONFIG_DIR=Release"
  set "CONFIG_DEFINE=/DNDEBUG"
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
set "TEST_BASENAME=tools\tests\real_djvu_regression-%CONFIGURATION%-%PLATFORM%-%BUILD_FLAVOR%"
if /I "%BUILD_FLAVOR%"=="legacy" (
  set "DJVU_LIBRARY=src\libdjvu\libdjvu%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
) else if /I "%BUILD_FLAVOR%"=="native" (
  set "DJVU_LIBRARY=src\%CONFIG_DIR%\libdjvu%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
) else (
  echo Build flavor must be legacy or native.
  exit /b 2
)
cl /nologo /W4 /EHsc %CRT% %CONFIG_DEFINE% /DWIN32 /D_WINDOWS /D_CONSOLE /DHAS_WCTYPE=1 /DTHREADMODEL=WINTHREADS /DDO_CHANGELOCALE=0 /DWIN32_MONITOR /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SECURE_SCL=0 /D_UNICODE /DUNICODE /Fo"%TEST_BASENAME%.obj" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\real_djvu_regression.cpp" /Fe"%TEST_BASENAME%.exe" "%DJVU_LIBRARY%" "src\third_party\libjpeg-turbo\jpeg%DEBUG_SUFFIX%%LIB_SUFFIX%.lib" advapi32.lib
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tools\tests\run-real-djvu-corpus.ps1" -TestExecutable "%CD%\%TEST_BASENAME%.exe"

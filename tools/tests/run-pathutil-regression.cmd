@echo off
setlocal EnableExtensions
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%PLATFORM%"=="" set "PLATFORM=Win32"
if /I "%CONFIGURATION%"=="Debug" (set "CRT=/MTd") else if /I "%CONFIGURATION%"=="Release" (set "CRT=/MT") else (echo Configuration must be Debug or Release.& exit /b 2)
if /I "%PLATFORM%"=="Win32" (set "ARCH=x86") else if /I "%PLATFORM%"=="x64" (set "ARCH=x64") else (echo Platform must be Win32 or x64.& exit /b 2)
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (echo Visual Studio C++ tools were not found.& exit /b 1)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if not defined TEST_BASENAME set "TEST_BASENAME=tools\tests\pathutil_regression-%CONFIGURATION%-%PLATFORM%"
cl /nologo /W4 /EHsc %CRT% /DWIN32 /D_WINDOWS /D_UNICODE /DUNICODE /I"src" /c /Fo"%TEST_BASENAME%-pathutil.obj" "src\PathUtil.cpp"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /W4 /EHsc %CRT% /DWIN32 /D_WINDOWS /D_UNICODE /DUNICODE /I"src" /Fo"%TEST_BASENAME%.obj" "tools\tests\pathutil_regression.cpp" "%TEST_BASENAME%-pathutil.obj" /Fe"%TEST_BASENAME%.exe" ole32.lib shell32.lib uuid.lib
if errorlevel 1 exit /b %errorlevel%
"%TEST_BASENAME%.exe"

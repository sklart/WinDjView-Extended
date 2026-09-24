@echo off
setlocal EnableExtensions EnableDelayedExpansion
for %%I in ("%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do set "VSWHERE=%%~sI"
set "VSROOT="
for /f "delims=" %%I in ('%VSWHERE% -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSROOT=%%I"
if not defined VSROOT (
	echo Visual Studio 2022 C++ tools were not found. 1>&2
	exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
set "TEST_BASENAME=tools\tests\tile_geometry_regression"
cl /nologo /W4 /EHsc /MT /DNDEBUG /Fo"%TEST_BASENAME%.obj" "tools\tests\tile_geometry_regression.cpp" /Fe"%TEST_BASENAME%.exe" /link /MANIFEST:NO
if errorlevel 1 exit /b %errorlevel%
"%TEST_BASENAME%.exe"
exit /b %errorlevel%

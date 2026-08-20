@echo off
setlocal
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cl /nologo /W4 /EHsc /MTd /DWIN32 /D_WINDOWS /D_CONSOLE /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /Fo"tools\tests\jpegdecoder_regression.obj" /I"src\libdjvu" /I"src\third_party\libjpeg-turbo\build\Debug" /I"src\third_party\libjpeg-turbo\src" "tools\tests\jpegdecoder_regression.cpp" /Fe"tools\tests\jpegdecoder_regression.exe" "src\libdjvu\libdjvud.lib" "src\third_party\libjpeg-turbo\jpegd.lib" advapi32.lib
if errorlevel 1 exit /b %errorlevel%
"tools\tests\jpegdecoder_regression.exe"

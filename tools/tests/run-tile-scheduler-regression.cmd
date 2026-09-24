@echo off
setlocal EnableExtensions EnableDelayedExpansion
for %%I in ("%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do set "VSWHERE=%%~sI"
set "VSROOT="
for /f "delims=" %%I in ('%VSWHERE% -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
set "TEST_BASENAME=tools\tests\tile_scheduler_regression"
set "TEST_FLAGS=/nologo /W4 /EHsc /MT /DNDEBUG /DWIN32 /DWIN64 /D_WINDOWS /D_CONSOLE /D_UNICODE /DUNICODE /DHAS_WCTYPE=1 /DTHREADMODEL=WINTHREADS /DDO_CHANGELOCALE=0 /DWIN32_MONITOR /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SECURE_SCL=0 /I"src" /I"src\libdjvu""
cl %TEST_FLAGS% /Fo"%TEST_BASENAME%.obj" /c "tools\tests\tile_scheduler_regression.cpp"
if errorlevel 1 exit /b %errorlevel%
cl %TEST_FLAGS% /Fo"%TEST_BASENAME%-scheduler.obj" /c "src\RenderScheduler.cpp"
if errorlevel 1 exit /b %errorlevel%
link /nologo /out:"%TEST_BASENAME%.exe" "%TEST_BASENAME%.obj" "%TEST_BASENAME%-scheduler.obj" /entry:mainCRTStartup /subsystem:console /manifest:no
if errorlevel 1 exit /b %errorlevel%
"%TEST_BASENAME%.exe"
exit /b %errorlevel%

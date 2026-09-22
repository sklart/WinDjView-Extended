@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "CORPUS_ROOT=%~1"
if "%CORPUS_ROOT%"=="" set "CORPUS_ROOT=tools\tests\corpus"
for %%I in ("%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do set "VSWHERE=%%~sI"
if not exist "%VSWHERE%" (
  echo Visual Studio locator was not found: %VSWHERE%
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo VS 2022 C++ tools were not found.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if not "%VCToolsVersion:~0,5%"=="14.44" (
  echo Expected v143 MSVC 14.44, got %VCToolsVersion%.
  exit /b 1
)

msbuild src\libdjvu\libdjvu.Modern.vcxproj /t:Rebuild /p:Configuration=Asan /p:Platform=x64 /m
if errorlevel 1 exit /b %errorlevel%
set "DJVU_LIBRARY=src\Asan_x64\libdjvu_asan64.lib"
if not exist "%DJVU_LIBRARY%" (
  echo Missing ASan libdjvu library: %DJVU_LIBRARY%
  exit /b 1
)
set "ASAN_INPUT_OBJECT=src\libdjvu\Asan\x64\IFFByteStream.obj"
if not exist "%ASAN_INPUT_OBJECT%" (
  echo Missing ASan-instrumented DjVu input object: %ASAN_INPUT_OBJECT%
  exit /b 1
)
dumpbin /symbols "%ASAN_INPUT_OBJECT%" | findstr /i /c:"__asan_" >nul
if errorlevel 1 (
  echo ASan instrumentation symbols were not found in %ASAN_INPUT_OBJECT%.
  exit /b 1
)

set "TEST_BASENAME=tools\tests\malformed_djvu_asan-x64"
cl /nologo /W4 /EHsc /MT /DNDEBUG /fsanitize=address /DWIN32 /DWIN64 /D_WINDOWS /D_CONSOLE /DHAS_WCTYPE=1 /DTHREADMODEL=WINTHREADS /DDO_CHANGELOCALE=0 /DWIN32_MONITOR /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SECURE_SCL=0 /D_UNICODE /DUNICODE /Fo"%TEST_BASENAME%.obj" /I"src\libdjvu" /I"src\third_party\libjpeg-turbo\build\Release_x64" /I"src\third_party\libjpeg-turbo\src" "tools\tests\malformed_djvu_asan.cpp" /Fe"%TEST_BASENAME%.exe" "%DJVU_LIBRARY%" "src\third_party\libjpeg-turbo\jpeg64.lib" advapi32.lib
if errorlevel 1 exit /b %errorlevel%
set "ASAN_RUNTIME=%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll"
if not exist "%ASAN_RUNTIME%" (
  echo ASan runtime was not found: %ASAN_RUNTIME%
  exit /b 1
)
copy /y "%ASAN_RUNTIME%" "tools\tests\clang_rt.asan_dynamic-x86_64.dll" >nul
if errorlevel 1 exit /b %errorlevel%
for %%I in ("%TEST_BASENAME%.exe") do set "TEST_EXECUTABLE=%%~fI"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tools\tests\run-malformed-djvu-asan.ps1" -TestExecutable "%TEST_EXECUTABLE%" -CorpusRoot "%CORPUS_ROOT%"
exit /b %errorlevel%

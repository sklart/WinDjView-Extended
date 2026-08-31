@echo off
setlocal EnableExtensions
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "BUILD_FLAVOR=%~3"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"
if "%BUILD_FLAVOR%"=="" set "BUILD_FLAVOR=native"
if /I not "%CONFIGURATION%"=="Release" (
  echo The performance baseline is Release-only.
  exit /b 2
)
if /I not "%PLATFORM%"=="x64" (
  echo The performance baseline is x64-only.
  exit /b 2
)
if /I not "%BUILD_FLAVOR%"=="native" (
  echo The performance baseline uses native MSBuild libdjvu.
  exit /b 2
)
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo Visual Studio C++ tools were not found.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
set "JPEG_BUILD=src\third_party\libjpeg-turbo\build\Release_x64"
set "BENCHMARK_BASENAME=tools\tests\real_djvu_benchmark-Release-x64-native"
set "OBJECT_DIR=src\Release\x64"
set "APP_OBJECT_NAMES=AnnotationDlg AppSettings BookmarkDlg BookmarksView CropPagesDlg DjVuDoc DjVuSource DjVuView DocPropertiesDlg Drawing FindDlg FullscreenWnd Global GotoPageDlg InstallDicDlg MagnifyWnd MainFrm MDIChild MyBitmapButton MyColorPicker MyComboBox MyDialog MyDocManager MyDocTemplate MyEdit MyFileDialog MyGdiPlus MyScrollView MyStatusBar MyTheme MyToolBar MyTreeView NavPane PageIndexWnd PathUtil PositionParser PrintDlg ProgressDlg RenderThread Scaling SearchResultsView SettingsAdvancedPage SettingsDictPage SettingsDisplayPage SettingsDlg SettingsGeneralPage stdafx TabbedMDIWnd ThumbnailsThread ThumbnailsView UpdateDlg WinDjView XMLParser ZoomDlg"
set "APP_OBJECTS="
for %%F in (%APP_OBJECT_NAMES%) do set "APP_OBJECTS=!APP_OBJECTS! %OBJECT_DIR%\%%F.obj"
lib /nologo /out:"%BENCHMARK_BASENAME%-app.lib" !APP_OBJECTS!
if errorlevel 1 exit /b %errorlevel%
cl /nologo /W4 /EHsc /MT /DNDEBUG /DWIN32 /D_WINDOWS /D_CONSOLE /DHAS_WCTYPE=1 /DTHREADMODEL=WINTHREADS /DDO_CHANGELOCALE=0 /DWIN32_MONITOR /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SECURE_SCL=0 /D_UNICODE /DUNICODE /Fo"%BENCHMARK_BASENAME%.obj" /I"src" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\real_djvu_benchmark.cpp" /Fe"%BENCHMARK_BASENAME%.exe" "%BENCHMARK_BASENAME%-app.lib" "src\Release_x64\libdjvu64.lib" "src\third_party\libjpeg-turbo\jpeg64.lib" advapi32.lib psapi.lib msimg32.lib version.lib shlwapi.lib shell32.lib ole32.lib uuid.lib /MANIFEST:NO
if errorlevel 1 exit /b %errorlevel%
for %%I in ("%BENCHMARK_BASENAME%.exe") do set "BENCHMARK_EXECUTABLE=%%~fI"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tools\tests\run-real-djvu-benchmark.ps1" -TestExecutable "%BENCHMARK_EXECUTABLE%"

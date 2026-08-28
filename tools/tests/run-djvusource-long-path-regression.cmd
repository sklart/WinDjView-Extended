@echo off
setlocal EnableExtensions EnableDelayedExpansion
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
  set "CONFIG_DEFINE=/D_DEBUG"
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
  set "OBJECT_PLATFORM=Win32"
) else if /I "%PLATFORM%"=="x64" (
  set "ARCH=x64"
  set "LIB_SUFFIX=64"
  set "CONFIG_DIR=%CONFIG_DIR%_x64"
  set "OBJECT_PLATFORM=x64"
) else (
  echo Platform must be Win32 or x64.
  exit /b 2
)
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if not defined TEST_BASENAME set "TEST_BASENAME=tools\tests\djvusource_long_path_regression-%CONFIGURATION%-%PLATFORM%"
set "JPEG_BUILD=src\third_party\libjpeg-turbo\build\%CONFIG_DIR%"
if /I "%BUILD_FLAVOR%"=="native" (
  set "OBJECT_DIR=src\%CONFIGURATION%\%OBJECT_PLATFORM%"
  set "DJVU_LIBRARY=src\%CONFIG_DIR%\libdjvu%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
  set "JPEG_LIBRARY=src\third_party\libjpeg-turbo\jpeg%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
) else if /I "%BUILD_FLAVOR%"=="legacy" (
  set "OBJECT_DIR=src\%CONFIG_DIR%"
  set "DJVU_LIBRARY=src\libdjvu\libdjvu%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
  set "JPEG_LIBRARY=src\third_party\libjpeg-turbo\jpeg%DEBUG_SUFFIX%%LIB_SUFFIX%.lib"
) else (
  echo Build flavor must be native or legacy.
  exit /b 2
)
set "APP_OBJECT_NAMES=AnnotationDlg AppSettings BookmarkDlg BookmarksView CropPagesDlg DjVuDoc DjVuSource DjVuView DocPropertiesDlg Drawing FindDlg FullscreenWnd Global GotoPageDlg InstallDicDlg MagnifyWnd MainFrm MDIChild MyBitmapButton MyColorPicker MyComboBox MyDialog MyDocManager MyDocTemplate MyEdit MyFileDialog MyGdiPlus MyScrollView MyStatusBar MyTheme MyToolBar MyTreeView NavPane PageIndexWnd PathUtil PositionParser PrintDlg ProgressDlg RenderThread Scaling SearchResultsView SettingsAdvancedPage SettingsDictPage SettingsDisplayPage SettingsDlg SettingsGeneralPage stdafx TabbedMDIWnd ThumbnailsThread ThumbnailsView UpdateDlg WinDjView XMLParser ZoomDlg"
set "APP_OBJECTS="
for %%F in (%APP_OBJECT_NAMES%) do set "APP_OBJECTS=!APP_OBJECTS! %OBJECT_DIR%\%%F.obj"
lib /nologo /out:"%TEST_BASENAME%-app.lib" !APP_OBJECTS!
if errorlevel 1 exit /b %errorlevel%
cl /nologo /W4 /EHsc %CRT% %CONFIG_DEFINE% /DWIN32 /D_WINDOWS /D_UNICODE /DUNICODE /c /Fo"%TEST_BASENAME%.obj" /I"src" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\djvusource_long_path_regression.cpp"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /W4 /EHsc %CRT% %CONFIG_DEFINE% /DWIN32 /D_WINDOWS /D_UNICODE /DUNICODE /c /Fo"%TEST_BASENAME%-bridge.obj" /I"src" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\djvusource_long_path_bridge.cpp"
if errorlevel 1 exit /b %errorlevel%
link /nologo /debug /out:"%TEST_BASENAME%.exe" "%TEST_BASENAME%.obj" "%TEST_BASENAME%-bridge.obj" "%TEST_BASENAME%-app.lib" %DJVU_LIBRARY% %JPEG_LIBRARY% msimg32.lib version.lib shlwapi.lib shell32.lib ole32.lib uuid.lib /ENTRY:wmainCRTStartup /MANIFEST:NO
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tools\tests\run-long-path-djvu-regression.ps1" -TestExecutable "%CD%\%TEST_BASENAME%.exe" -Fixture "%CD%\tools\tests\fixtures\minimal.djvu"
exit /b %errorlevel%

@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "BUILD_FLAVOR=%~3"
set "FIXTURE=%~4"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"
if "%BUILD_FLAVOR%"=="" set "BUILD_FLAVOR=native"
if /I not "%CONFIGURATION%"=="Release" exit /b 2
if /I not "%PLATFORM%"=="x64" exit /b 2
if /I not "%BUILD_FLAVOR%"=="native" exit /b 2
if "%FIXTURE%"=="" exit /b 2
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
set "TEST_BASENAME=tools\tests\render_prefetch_regression-Release-x64-native"
set "OBJECT_DIR=src\Release\x64"
set "JPEG_BUILD=src\third_party\libjpeg-turbo\build\Release_x64"
set "APP_OBJECT_NAMES=AnnotationDlg AppSettings BookmarkDlg BookmarksView CropPagesDlg DjVuDoc DjVuSource DjVuView DocPropertiesDlg Drawing FindDlg FullscreenWnd Global GotoPageDlg InstallDicDlg MagnifyWnd MainFrm MDIChild MyBitmapButton MyColorPicker MyComboBox MyDialog MyDocManager MyDocTemplate MyEdit MyFileDialog MyGdiPlus MyScrollView MyStatusBar MyTheme MyToolBar MyTreeView NavPane PageIndexWnd PathUtil PositionParser PrintDlg ProgressDlg RenderThread Scaling SearchResultsView SettingsAdvancedPage SettingsDictPage SettingsDisplayPage SettingsDlg SettingsGeneralPage stdafx TabbedMDIWnd ThumbnailsThread ThumbnailsView UpdateDlg WinDjView XMLParser ZoomDlg"
set "APP_OBJECTS="
for %%F in (%APP_OBJECT_NAMES%) do set "APP_OBJECTS=!APP_OBJECTS! %OBJECT_DIR%\%%F.obj"
lib /nologo /out:"%TEST_BASENAME%-app.lib" !APP_OBJECTS!
if errorlevel 1 exit /b %errorlevel%
cl /nologo /W4 /EHsc /MT /DNDEBUG /DWIN32 /D_WINDOWS /D_UNICODE /DUNICODE /c /Fo"%TEST_BASENAME%.obj" /I"src" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\render_prefetch_regression.cpp"
if errorlevel 1 exit /b %errorlevel%
link /nologo /out:"%TEST_BASENAME%.exe" "%TEST_BASENAME%.obj" "%TEST_BASENAME%-app.lib" src\Release_x64\libdjvu64.lib src\third_party\libjpeg-turbo\jpeg64.lib msimg32.lib version.lib shlwapi.lib shell32.lib ole32.lib uuid.lib /ENTRY:wmainCRTStartup /MANIFEST:NO
if errorlevel 1 exit /b %errorlevel%
"%TEST_BASENAME%.exe" "%FIXTURE%"

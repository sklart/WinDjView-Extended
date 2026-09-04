@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "BUILD_FLAVOR=%~3"
set "FIXTURE=%~4"
set "MODE=%~5"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"
if "%BUILD_FLAVOR%"=="" set "BUILD_FLAVOR=native"
if "%FIXTURE%"=="" set "FIXTURE=tools\tests\fixtures\minimal.djvu"
if /I not "%CONFIGURATION%"=="Release" exit /b 2
if /I not "%PLATFORM%"=="x64" exit /b 2
if /I not "%BUILD_FLAVOR%"=="native" exit /b 2
for %%I in ("%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do set "VSWHERE=%%~sI"
for /f "delims=" %%I in ('%VSWHERE% -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.14.44.17.14.MFC -requires Microsoft.VisualStudio.Component.VC.14.44.17.14.ATL -property installationPath') do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
set "JPEG_BUILD=src\third_party\libjpeg-turbo\build\Release_x64"
set "TEST_BASENAME=tools\tests\page_cache_regression-Release-x64-native"
set "OBJECT_DIR=src\Release\x64"
if not exist "%OBJECT_DIR%\DjVuView.obj" set "OBJECT_DIR=src\Release_x64"
set "APP_OBJECT_NAMES=AnnotationDlg AppSettings BookmarkDlg BookmarksView CropPagesDlg DjVuDoc DjVuSource DjVuView DocPropertiesDlg Drawing FindDlg FullscreenWnd Global GotoPageDlg InstallDicDlg MagnifyWnd MainFrm MDIChild MyBitmapButton MyColorPicker MyComboBox MyDialog MyDocManager MyDocTemplate MyEdit MyFileDialog MyGdiPlus MyScrollView MyStatusBar MyTheme MyToolBar MyTreeView NavPane PageIndexWnd PathUtil PositionParser PrintDlg ProgressDlg RenderThread Scaling SearchResultsView SettingsAdvancedPage SettingsDictPage SettingsDisplayPage SettingsDlg SettingsGeneralPage stdafx TabbedMDIWnd ThumbnailsThread ThumbnailsView UpdateDlg WinDjView XMLParser ZoomDlg"
set "APP_OBJECTS="
for %%F in (%APP_OBJECT_NAMES%) do set "APP_OBJECTS=!APP_OBJECTS! %OBJECT_DIR%\%%F.obj"
lib /nologo /out:"%TEST_BASENAME%-app.lib" !APP_OBJECTS!
if errorlevel 1 exit /b %errorlevel%
set "DJVU_LIBRARY=src\Release_x64\libdjvu64.lib"
if not exist "%DJVU_LIBRARY%" set "DJVU_LIBRARY=src\libdjvu\libdjvu64.lib"
set "JPEG_LIBRARY=src\third_party\libjpeg-turbo\build\Release_x64\jpeg-static.lib"
if not exist "%JPEG_LIBRARY%" set "JPEG_LIBRARY=src\third_party\libjpeg-turbo\jpeg64.lib"
cl /nologo /W4 /EHsc /MT /DNDEBUG /DWIN32 /D_WINDOWS /D_CONSOLE /DHAS_WCTYPE=1 /DTHREADMODEL=WINTHREADS /DDO_CHANGELOCALE=0 /DWIN32_MONITOR /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SECURE_SCL=0 /D_UNICODE /DUNICODE /Fo"%TEST_BASENAME%.obj" /I"src" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\page_cache_regression.cpp" /Fe"%TEST_BASENAME%.exe" "%TEST_BASENAME%-app.lib" "%DJVU_LIBRARY%" "%JPEG_LIBRARY%" advapi32.lib psapi.lib msimg32.lib version.lib shlwapi.lib shell32.lib ole32.lib uuid.lib /MANIFEST:NO
if errorlevel 1 exit /b %errorlevel%
"%TEST_BASENAME%.exe" "%FIXTURE%" %MODE%
exit /b %errorlevel%

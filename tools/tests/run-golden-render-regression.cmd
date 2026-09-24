@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "BUILD_FLAVOR=%~3"
set "CORPUS_ROOT=%~4"
set "MODE=%~5"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"
if "%BUILD_FLAVOR%"=="" set "BUILD_FLAVOR=native"
if "%CORPUS_ROOT%"=="" set "CORPUS_ROOT=tools\tests\corpus\files"
if /I not "%CONFIGURATION%"=="Release" exit /b 2
if /I not "%PLATFORM%"=="x64" exit /b 2
if /I not "%BUILD_FLAVOR%"=="native" exit /b 2
if not "%MODE%"=="" if /I not "%MODE%"=="--update-baseline" exit /b 2

for %%I in ("%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do set "VSWHERE=%%~sI"
set "VSROOT="
for /f "delims=" %%I in ('%VSWHERE% -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSROOT=%%I"
if not defined VSROOT (
	echo Visual Studio 2022 C++ tools were not found. 1>&2
	exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if not defined VCToolsVersion (
	echo Visual Studio 2022 did not initialize an MSVC toolset. 1>&2
	exit /b 1
)
if not exist "!VCToolsInstallDir!atlmfc\include\afxwin.h" (
	echo MFC for MSVC !VCToolsVersion! is not installed. 1>&2
	exit /b 1
)
if not exist "!VCToolsInstallDir!atlmfc\include\atlbase.h" (
	echo ATL for MSVC !VCToolsVersion! is not installed. 1>&2
	exit /b 1
)
if not exist "!VCToolsInstallDir!atlmfc\lib\x64\mfc140u.lib" (
	echo MFC x64 libraries for MSVC !VCToolsVersion! are not installed. 1>&2
	exit /b 1
)
if not exist "!VCToolsInstallDir!atlmfc\lib\x64\atls.lib" (
	echo ATL x64 libraries for MSVC !VCToolsVersion! are not installed. 1>&2
	exit /b 1
)
echo Using VS2022 MSVC !VCToolsVersion! with MFC and ATL.
msbuild src\WinDjView.Native.vcxproj /nologo /t:Rebuild /m /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b %errorlevel%
set "TEST_BASENAME=tools\tests\golden_render_regression-Release-x64-native"
set "OBJECT_DIR=src\Release\x64"
set "JPEG_BUILD=src\third_party\libjpeg-turbo\build\Release_x64"
set "APP_OBJECT_NAMES=AnnotationDlg AppSettings BitmapCache BookmarkDlg BookmarksView CropPagesDlg DjVuDoc DjVuSource DjVuView DocPropertiesDlg Drawing FindDlg FullscreenWnd Global GotoPageDlg InstallDicDlg MagnifyWnd MainFrm MDIChild MyBitmapButton MyColorPicker MyComboBox MyDialog MyDocManager MyDocTemplate MyEdit MyFileDialog MyGdiPlus MyScrollView MyStatusBar MyTheme MyToolBar MyTreeView NavPane PageIndexWnd PathUtil PositionParser PrintDlg ProgressDlg RenderScheduler RenderThread Scaling SearchResultsView SettingsAdvancedPage SettingsDictPage SettingsDisplayPage SettingsDlg SettingsGeneralPage stdafx TabbedMDIWnd ThumbnailsThread ThumbnailsView UpdateDlg WinDjView XMLParser ZoomDlg"
set "APP_OBJECTS="
for %%F in (%APP_OBJECT_NAMES%) do (
	if not exist "%OBJECT_DIR%\%%F.obj" (
		echo Missing v143 app object: "%OBJECT_DIR%\%%F.obj" 1>&2
		exit /b 3
	)
	set "APP_OBJECTS=!APP_OBJECTS! %OBJECT_DIR%\%%F.obj"
)
lib /nologo /out:"%TEST_BASENAME%-app.lib" !APP_OBJECTS!
if errorlevel 1 exit /b %errorlevel%
set "DJVU_LIBRARY=src\Release_x64\libdjvu64.lib"
if not exist "%DJVU_LIBRARY%" set "DJVU_LIBRARY=src\libdjvu\libdjvu64.lib"
set "JPEG_LIBRARY=src\third_party\libjpeg-turbo\build\Release_x64\jpeg-static.lib"
if not exist "%JPEG_LIBRARY%" set "JPEG_LIBRARY=src\third_party\libjpeg-turbo\jpeg64.lib"
cl /nologo /W4 /EHsc /MT /DNDEBUG /DWIN32 /D_WINDOWS /D_CONSOLE /DHAS_WCTYPE=1 /DTHREADMODEL=WINTHREADS /DDO_CHANGELOCALE=0 /DWIN32_MONITOR /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SECURE_SCL=0 /D_UNICODE /DUNICODE /Fo"%TEST_BASENAME%.obj" /I"src" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\golden_render_regression.cpp" /Fe"%TEST_BASENAME%.exe" "%TEST_BASENAME%-app.lib" "%DJVU_LIBRARY%" "%JPEG_LIBRARY%" advapi32.lib bcrypt.lib psapi.lib msimg32.lib version.lib shlwapi.lib shell32.lib ole32.lib uuid.lib /link /LTCG /MANIFEST:NO
if errorlevel 1 exit /b %errorlevel%
set "UPDATE_ARGS="
if /I "%MODE%"=="--update-baseline" (
	for /f "delims=" %%I in ('git rev-parse HEAD') do set "BASELINE_COMMIT=%%I"
	if not defined BASELINE_COMMIT (
		echo Could not resolve current Git commit for baseline provenance. 1>&2
		exit /b 1
	)
	set "UPDATE_ARGS=--update-baseline !BASELINE_COMMIT!"
)
"%TEST_BASENAME%.exe" "tools\tests\golden-render-baseline.json" "%CORPUS_ROOT%" !UPDATE_ARGS!
exit /b %errorlevel%

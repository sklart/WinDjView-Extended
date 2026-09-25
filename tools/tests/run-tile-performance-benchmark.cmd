@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "CORPUS_ROOT=%~1"
if "%CORPUS_ROOT%"=="" set "CORPUS_ROOT=tools\tests\corpus\files"
set "OUTPUT_DIR=%~2"
if "%OUTPUT_DIR%"=="" set "OUTPUT_DIR=tools\tests\artifacts\tile-performance"
if not exist "%CORPUS_ROOT%\cable_1973_100133.djvu" (
	echo Missing real DjVu corpus in "%CORPUS_ROOT%". 1>&2
	exit /b 2
)
for %%I in ("%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do set "VSWHERE=%%~sI"
set "VSROOT="
for /f "delims=" %%I in ('%VSWHERE% -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if not defined VCToolsVersion exit /b 1
if not exist "!VCToolsInstallDir!atlmfc\include\afxwin.h" exit /b 1
if not exist "!VCToolsInstallDir!atlmfc\include\atlbase.h" exit /b 1
if not exist "!VCToolsInstallDir!atlmfc\lib\x64\mfc140u.lib" exit /b 1
if not exist "!VCToolsInstallDir!atlmfc\lib\x64\atls.lib" exit /b 1
echo Tile benchmark using VS2022 MSVC !VCToolsVersion! with MFC and ATL.
msbuild src\WinDjView.Native.vcxproj /nologo /t:Rebuild /m /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b %errorlevel%
set "TEST_BASENAME=tools\tests\tile_performance_benchmark-Release-x64-native"
set "OBJECT_DIR=src\Release\x64"
set "JPEG_BUILD=src\third_party\libjpeg-turbo\build\Release_x64"
set "APP_OBJECT_NAMES=AnnotationDlg AppSettings BitmapCache BookmarkDlg BookmarksView CropPagesDlg DjVuDoc DjVuSource DjVuView DocPropertiesDlg Drawing FindDlg FullscreenWnd Global GotoPageDlg InstallDicDlg MagnifyWnd MainFrm MDIChild MyBitmapButton MyColorPicker MyComboBox MyDialog MyDocManager MyDocTemplate MyEdit MyFileDialog MyGdiPlus MyScrollView MyStatusBar MyTheme MyToolBar MyTreeView NavPane PageIndexWnd PageWorkingSet PathUtil PositionParser PrintDlg ProgressDlg RenderScheduler RenderThread Scaling SearchResultsView SettingsAdvancedPage SettingsDictPage SettingsDisplayPage SettingsDlg SettingsGeneralPage stdafx TabbedMDIWnd ThumbnailsThread ThumbnailsView UpdateDlg WinDjView XMLParser ZoomDlg"
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
set "JPEG_LIBRARY=src\third_party\libjpeg-turbo\build\Release_x64\jpeg-static.lib"
if not exist "%DJVU_LIBRARY%" exit /b 3
if not exist "%JPEG_LIBRARY%" exit /b 3
cl /nologo /W4 /EHsc /MT /DNDEBUG /DWIN32 /D_WINDOWS /D_CONSOLE /DHAS_WCTYPE=1 /DTHREADMODEL=WINTHREADS /DDO_CHANGELOCALE=0 /DWIN32_MONITOR /DNEED_JPEG_DECODER /DLIBDJVU_STATIC /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SECURE_SCL=0 /D_UNICODE /DUNICODE /Fo"%TEST_BASENAME%.obj" /I"src" /I"src\libdjvu" /I"%JPEG_BUILD%" /I"src\third_party\libjpeg-turbo\src" "tools\tests\tile_performance_benchmark.cpp" /Fe"%TEST_BASENAME%.exe" "%TEST_BASENAME%-app.lib" "%DJVU_LIBRARY%" "%JPEG_LIBRARY%" advapi32.lib psapi.lib msimg32.lib version.lib shlwapi.lib shell32.lib ole32.lib uuid.lib /link /LTCG /MANIFEST:NO
if errorlevel 1 exit /b %errorlevel%
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if errorlevel 1 exit /b %errorlevel%
for /f "delims=" %%I in ('git rev-parse HEAD') do set "SOURCE_COMMIT=%%I"
echo commit=!SOURCE_COMMIT!>"%OUTPUT_DIR%\metadata.txt"
echo msvc=!VCToolsVersion!>>"%OUTPUT_DIR%\metadata.txt"
echo configuration=Native Release x64>>"%OUTPUT_DIR%\metadata.txt"
echo logical_processors=!NUMBER_OF_PROCESSORS!>>"%OUTPUT_DIR%\metadata.txt"
"%TEST_BASENAME%.exe" "%CORPUS_ROOT%" "%OUTPUT_DIR%" >"%OUTPUT_DIR%\console.log" 2>&1
set "TEST_EXIT=!errorlevel!"
type "%OUTPUT_DIR%\console.log"
exit /b !TEST_EXIT!

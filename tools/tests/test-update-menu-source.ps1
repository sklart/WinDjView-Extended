[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Require-SourcePattern {
	param([string]$Path, [string]$Pattern, [string]$Description)
	$source = Get-Content -LiteralPath (Join-Path $root $Path) -Raw
	if ($source -notmatch $Pattern) { throw "Missing $Description in $Path" }
}

Require-SourcePattern 'src\MainFrm.h' 'virtual void OnInitMenuPopup\(CMenu\* pPopupMenu, UINT nIndex, BOOL bSysMenu\)' 'menu popup override'
Require-SourcePattern 'src\MainFrm.cpp' 'ON_WM_INITMENUPOPUP\(\)' 'menu popup message-map binding'
Require-SourcePattern 'src\MainFrm.cpp' 'void CMainFrame::OnInitMenuPopup\(CMenu\* pPopupMenu, UINT nIndex, BOOL bSysMenu\)' 'menu popup implementation'
Require-SourcePattern 'src\MainFrm.cpp' 'GetMenuState\(ID_APP_ABOUT, MF_BYCOMMAND\) != \(UINT\)-1' 'Help/About menu guard'
Require-SourcePattern 'src\MainFrm.cpp' 'GetMenuState\(ID_CHECK_FOR_UPDATE, MF_BYCOMMAND\) == \(UINT\)-1' 'duplicate insertion guard'
Require-SourcePattern 'src\MainFrm.cpp' 'LoadString\(ID_CHECK_FOR_UPDATE\)' 'localized update command caption'
Require-SourcePattern 'src\MainFrm.cpp' 'InsertMenu\(nAbout, MF_BYPOSITION \| MF_STRING,\s*ID_CHECK_FOR_UPDATE, strCaption\)' 'update command insertion'
Require-SourcePattern 'src\WinDjView.cpp' 'ON_COMMAND\(ID_CHECK_FOR_UPDATE, OnCheckForUpdate\)' 'update command routing'
Require-SourcePattern 'src\WinDjView.cpp' 'void CDjViewApp::OnCheckForUpdate\(\)\s*\{\s*CUpdateDlg dlg;' 'update dialog handler'

Write-Host 'Update menu source regression: PASS'

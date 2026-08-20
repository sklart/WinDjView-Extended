param(
	[ValidateSet('Debug', 'Release')]
	[string]$Configuration = 'Release',
	[ValidateSet('Win32', 'x64')]
	[string]$Platform = 'Win32',
	[string]$ReportPath = '',
	[string]$PvsStudioPath = ${env:ProgramFiles(x86)} + '\PVS-Studio',
	[string]$NasmDirectory = ''
)

$ErrorActionPreference = 'Stop'

if ($Configuration -eq 'Release')
{
	if (-not [string]::IsNullOrWhiteSpace($NasmDirectory))
	{
		if (-not (Test-Path -LiteralPath (Join-Path $NasmDirectory 'nasm.exe')))
		{
			throw "nasm.exe was not found in: $NasmDirectory"
		}
		$env:Path = $NasmDirectory + ';' + $env:Path
	}
	elseif (-not (Get-Command nasm.exe -ErrorAction SilentlyContinue))
	{
		throw 'NASM is required for Release SIMD builds. Install NASM or pass -NasmDirectory to an existing NASM installation.'
	}
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$solutionPath = Join-Path $repositoryRoot 'WinDjView.Modern.sln'
$monitorPath = Join-Path $PvsStudioPath 'CLMonitor.exe'
$msbuildCommand = Get-Command msbuild.exe -ErrorAction SilentlyContinue
$msbuildPath = if ($msbuildCommand) { $msbuildCommand.Source } else { $null }

if ($msbuildPath -eq $null)
{
	$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (Test-Path -LiteralPath $vswherePath)
	{
		$msbuildPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
	}
}

if (-not (Test-Path -LiteralPath $monitorPath))
{
	throw "CLMonitor.exe was not found: $monitorPath"
}

if ([string]::IsNullOrWhiteSpace($msbuildPath) -or -not (Test-Path -LiteralPath $msbuildPath))
{
	throw 'MSBuild.exe was not found. Install the Visual Studio MSBuild component or run from a Developer PowerShell.'
}

if ([string]::IsNullOrWhiteSpace($ReportPath))
{
	$ReportPath = Join-Path $repositoryRoot ("out\pvs\WinDjView-{0}-{1}.plog" -f $Configuration, $Platform)
}

$ReportPath = [System.IO.Path]::GetFullPath($ReportPath)
$reportDirectory = Split-Path -Parent $ReportPath
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null

& $monitorPath monitor
try
{
	& $msbuildPath $solutionPath /m /t:Rebuild "/p:Configuration=$Configuration" "/p:Platform=$Platform" /v:minimal
	if ($LASTEXITCODE -ne 0)
	{
		throw "Build failed with exit code $LASTEXITCODE."
	}

	& $monitorPath analyze --log $ReportPath
	if ($LASTEXITCODE -ne 0)
	{
		throw "PVS-Studio analysis failed with exit code $LASTEXITCODE."
	}
}
catch
{
	& $monitorPath abortTrace | Out-Null
	throw
}

Write-Host "PVS-Studio report: $ReportPath"

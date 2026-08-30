[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$TestExecutable,
	[string]$ManifestPath,
	[int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
	$ManifestPath = Join-Path $PSScriptRoot 'corpus\manifest.json'
}

if (-not (Test-Path -LiteralPath $TestExecutable -PathType Leaf)) {
	throw "Corpus regression executable was not found: $TestExecutable"
}
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
	throw "Corpus manifest was not found: $ManifestPath"
}
if ($TimeoutSeconds -le 0) {
	throw 'TimeoutSeconds must be positive.'
}

$manifestRoot = Split-Path -Parent $ManifestPath
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$failed = New-Object System.Collections.Generic.List[string]

foreach ($fixture in $manifest.files) {
	$fixturePath = Join-Path $manifestRoot $fixture.file
	if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
		$failed.Add("$($fixture.id): fixture is missing")
		Write-Host "FAIL $($fixture.id): fixture is missing"
		continue
	}
	$expectedPages = if ($null -eq $fixture.expected.pages) { -1 } else { [int]$fixture.expected.pages }
	$features = New-Object System.Collections.Generic.List[string]
	foreach ($feature in @($fixture.expected.features | Where-Object { $_ -in @('text_layer', 'annotations') })) {
		if (-not $features.Contains($feature)) { $features.Add($feature) }
	}
	if ($fixture.actual.features) {
		foreach ($feature in @('text_layer', 'annotations')) {
			if ($fixture.actual.features.$feature -and -not $features.Contains($feature)) { $features.Add($feature) }
		}
	}
	$featuresArgument = $features -join ','
	$arguments = '"{0}" {1} "{2}"' -f $fixturePath.Replace('"', '""'), $expectedPages, $featuresArgument
	Write-Host "RUN  $($fixture.id)"
	$startInfo = New-Object System.Diagnostics.ProcessStartInfo
	$startInfo.FileName = $TestExecutable
	$startInfo.Arguments = $arguments
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$child = New-Object System.Diagnostics.Process
	$child.StartInfo = $startInfo
	if (-not $child.Start()) { throw "Could not start fixture process: $($fixture.id)" }
	if (-not $child.WaitForExit($TimeoutSeconds * 1000)) {
		$child.Kill()
		$child.WaitForExit()
		$failed.Add("$($fixture.id): timeout after $TimeoutSeconds seconds")
		Write-Host "FAIL $($fixture.id): timeout"
		continue
	}
	$childExitCode = [int]$child.ExitCode
	if ($childExitCode -ne 0) {
		$failed.Add("$($fixture.id): exit code $childExitCode")
		Write-Host "FAIL $($fixture.id): exit code $childExitCode"
		continue
	}
	Write-Host "PASS $($fixture.id)"
}

if ($failed.Count -gt 0) {
	Write-Host "DjVu corpus regression: FAIL ($($failed -join '; '))"
	exit 1
}
Write-Host "DjVu corpus regression: PASS ($($manifest.files.Count) fixtures)"

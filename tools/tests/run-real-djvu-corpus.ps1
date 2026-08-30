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

function Get-ExpectedFeatures {
	param($Fixture)
	$features = New-Object System.Collections.Generic.List[string]
	foreach ($feature in @($Fixture.expected.features | Where-Object { $_ -in @('text_layer', 'annotations', 'bookmarks', 'hyperlinks') })) {
		if (-not $features.Contains($feature)) { $features.Add($feature) }
	}
	if ($Fixture.actual.features) {
		foreach ($feature in @('text_layer', 'annotations')) {
			if ($Fixture.actual.features.$feature -and -not $features.Contains($feature)) { $features.Add($feature) }
		}
	}
	return $features -join ','
}

function ConvertFrom-CodePoints {
	param([int[]]$CodePoints)
	return (-join ($CodePoints | ForEach-Object { [char]$_ }))
}

function Invoke-Fixture {
	param($Fixture, [string]$FixturePath)
	if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
		$failed.Add("$($fixture.id): fixture is missing")
		Write-Host "FAIL $($fixture.id): fixture is missing"
		return
	}
	$expectedResult = if ($fixture.expected_result) { [string]$fixture.expected_result } else { 'pass' }
	$expectedPages = if ($expectedResult -eq 'fail' -or $null -eq $fixture.expected.pages) { -1 } else { [int]$fixture.expected.pages }
	$featuresArgument = if ($expectedResult -eq 'fail') { '' } else { Get-ExpectedFeatures $fixture }
	$arguments = '"{0}" {1} "{2}"' -f $fixturePath.Replace('"', '""'), $expectedPages, $featuresArgument
	Write-Host "RUN  $($fixture.id)"
	$startInfo = New-Object System.Diagnostics.ProcessStartInfo
	$startInfo.FileName = $TestExecutable
	$startInfo.Arguments = $arguments
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$child = New-Object System.Diagnostics.Process
	$child.StartInfo = $startInfo
	if (-not $child.Start()) { throw "Could not start fixture process: $($fixture.id)" }
	if (-not $child.WaitForExit($TimeoutSeconds * 1000)) {
		$child.Kill()
		$child.WaitForExit()
		$failed.Add("$($fixture.id): timeout after $TimeoutSeconds seconds")
		Write-Host "FAIL $($fixture.id): timeout"
		return
	}
	$childExitCode = [int]$child.ExitCode
	$childOutput = $child.StandardOutput.ReadToEnd() + "`n" + $child.StandardError.ReadToEnd()
	$failureMatches = @([regex]::Matches($childOutput, '(?m)^CORPUS_RESULT:\s*FAIL\s+([a-z0-9_]+)\s*$'))
	$observedFailure = if ($failureMatches.Count -eq 1) { $failureMatches[0].Groups[1].Value } else { $null }
	if ($expectedResult -eq 'fail') {
		if ($childExitCode -eq 1 -and $observedFailure -eq $fixture.expected_failure) {
			Write-Host "PASS $($fixture.id): expected $observedFailure"
			return
		}
		$failed.Add("$($fixture.id): expected $($fixture.expected_failure), got $observedFailure (exit code $childExitCode)")
		Write-Host "FAIL $($fixture.id): expected $($fixture.expected_failure), got $observedFailure (exit code $childExitCode)"
		return
	}
	if ($expectedResult -ne 'pass') {
		$failed.Add("$($fixture.id): unknown expected_result '$expectedResult'")
		Write-Host "FAIL $($fixture.id): unknown expected_result '$expectedResult'"
		return
	}
	if ($childExitCode -ne 0) {
		$failed.Add("$($fixture.id): exit code $childExitCode")
		Write-Host "FAIL $($fixture.id): exit code $childExitCode"
		return
	}
	if ($childOutput -notmatch '(?m)^CORPUS_RESULT:\s*PASS\s*$') {
		$failed.Add("$($fixture.id): missing machine-readable PASS result")
		Write-Host "FAIL $($fixture.id): missing machine-readable PASS result"
		return
	}
	Write-Host "PASS $($fixture.id)"
}

foreach ($fixture in $manifest.files) {
	Invoke-Fixture $fixture (Join-Path $manifestRoot $fixture.file)
}

# These copies exercise the production UTF-16 path boundary without putting
# generated content into the corpus download directory or using a long-path prefix.
$pathSource = $manifest.files | Where-Object { $_.id -eq 'watchmaker' } | Select-Object -First 1
if ($pathSource) {
	$sourcePath = Join-Path $manifestRoot $pathSource.file
	$pathRoot = Join-Path ([IO.Path]::GetTempPath()) ('WinDjView-DjVuCorpus-' + [guid]::NewGuid().ToString('N'))
	try {
		[IO.Directory]::CreateDirectory($pathRoot) | Out-Null
		$cyrillicName = ConvertFrom-CodePoints @(0x043a,0x0438,0x0440,0x0438,0x043b,0x043b,0x0438,0x0446,0x0430,0x002d,0x043f,0x0440,0x043e,0x0432,0x0435,0x0440,0x043a,0x0430,0x002e,0x0064,0x006a,0x0076,0x0075)
		$unicodeName = ConvertFrom-CodePoints @(0x65e5,0x672c,0x8a9e,0x002d,0x0394,0x03bf,0x03ba,0x03b9,0x03bc,0x03ae,0x002e,0x0064,0x006a,0x0076,0x0075)
		$longName = ConvertFrom-CodePoints @(0x0434,0x043b,0x0438,0x043d,0x043d,0x044b,0x0439,0x002d,0x65e5,0x672c,0x8a9e,0x002e,0x0064,0x006a,0x0076,0x0075)
		$segment = ConvertFrom-CodePoints @(0x0434,0x043b,0x0438,0x043d,0x043d,0x044b,0x0439,0x002d,0x0441,0x0435,0x0433,0x043c,0x0435,0x043d,0x0442,0x002d,0x65e5,0x672c,0x8a9e,0x002d,0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039)
		$pathCases = @(
			@{ id = 'path_cyrillic'; name = $cyrillicName },
			@{ id = 'path_unicode'; name = $unicodeName },
			@{ id = 'path_long_unicode'; name = $longName }
		)
		$longDirectory = $pathRoot
		while ($longDirectory.Length -le 270) {
			$longDirectory = Join-Path $longDirectory $segment
		}
		foreach ($pathCase in $pathCases) {
			$destinationDirectory = if ($pathCase.id -eq 'path_long_unicode') { $longDirectory } else { $pathRoot }
			[IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
			$pathFixture = [PSCustomObject]@{ id = $pathCase.id; expected = $pathSource.expected; actual = $null; expected_result = 'pass' }
			Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $destinationDirectory $pathCase.name)
			Invoke-Fixture $pathFixture (Join-Path $destinationDirectory $pathCase.name)
		}
	} finally {
		if (Test-Path -LiteralPath $pathRoot) { Remove-Item -LiteralPath $pathRoot -Recurse -Force }
	}
}

if ($failed.Count -gt 0) {
	Write-Host "DjVu corpus regression: FAIL ($($failed -join '; '))"
	exit 1
}
Write-Host "DjVu corpus regression: PASS ($($manifest.files.Count) manifest fixtures plus path variants)"

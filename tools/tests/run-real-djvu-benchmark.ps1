[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$TestExecutable,
	[string]$ManifestPath,
	[string]$OutputDirectory,
	[ValidateRange(2, 32)]
	[int]$Runs = 4
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ManifestPath)) { $ManifestPath = Join-Path $PSScriptRoot 'corpus\manifest.json' }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $PSScriptRoot 'artifacts\djvu-performance-baseline' }
if (-not (Test-Path -LiteralPath $TestExecutable -PathType Leaf)) { throw "Benchmark executable was not found: $TestExecutable" }
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { throw "Corpus manifest was not found: $ManifestPath" }

function Get-Statistics {
	param([double[]]$Values)
	$ordered = @($Values | Sort-Object)
	if ($ordered.Count -eq 0) { return $null }
	$middle = [int]($ordered.Count / 2)
	$median = if ($ordered.Count % 2) { $ordered[$middle] } else { ($ordered[$middle - 1] + $ordered[$middle]) / 2.0 }
	return [ordered]@{ values_ms = @($Values); cold_ms = $Values[0]; min_ms = $ordered[0]; median_ms = $median; max_ms = $ordered[$ordered.Count - 1] }
}

$manifestRoot = Split-Path -Parent $ManifestPath
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$commit = (& git rev-parse HEAD 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) { $commit = 'unknown' }
$fixtures = New-Object System.Collections.Generic.List[object]

foreach ($fixture in $manifest.files) {
	if ($fixture.expected_result -eq 'fail') { continue }
	$fixturePath = Join-Path $manifestRoot $fixture.file
	if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) { throw "Benchmark fixture is missing: $($fixture.id)" }
	$output = & $TestExecutable $fixturePath $fixture.id $Runs 2>&1 | Out-String
	if ($LASTEXITCODE -ne 0 -or $output -notmatch '(?m)^BENCHMARK_RESULT:\s*PASS\s*\r?$') { throw "Benchmark failed for $($fixture.id):`n$output" }
	$opens = @([regex]::Matches($output, '(?m)^BENCHMARK_OPEN run=(\d+) ms=([0-9.]+) pages=(\d+) peak_ws=(\d+)\r?$'))
	$decodes = @([regex]::Matches($output, '(?m)^BENCHMARK_DECODE run=(\d+) page=(first|middle|last) index=(\d+) ms=([0-9.]+) width=(\d+) height=(\d+) peak_ws=(\d+)\r?$'))
	if ($opens.Count -ne $Runs) { throw "Benchmark output has $($opens.Count) open samples for $($fixture.id), expected $Runs" }
	$pageSamples = [ordered]@{}
	foreach ($decode in $decodes) {
		$label = $decode.Groups[2].Value
		if (-not $pageSamples.Contains($label)) { $pageSamples[$label] = New-Object System.Collections.Generic.List[double] }
		$pageSamples[$label].Add([double]::Parse($decode.Groups[4].Value, [Globalization.CultureInfo]::InvariantCulture))
	}
	$requiredLabels = if ([int]$opens[0].Groups[3].Value -eq 1) { @('first') } else { @('first', 'middle', 'last') }
	foreach ($label in $requiredLabels) {
		$actualCount = if ($pageSamples.Contains($label)) { $pageSamples[$label].Count } else { 0 }
		if ($actualCount -ne $Runs) {
			throw "Benchmark output has $actualCount $label decode samples for $($fixture.id), expected $Runs"
		}
	}
	foreach ($label in $pageSamples.Keys) {
		if ($requiredLabels -notcontains $label) {
			throw "Benchmark output has an unexpected $label decode sample for $($fixture.id)"
		}
	}
	$peak = 0L
	foreach ($match in @($opens) + @($decodes)) { $peak = [Math]::Max($peak, [Int64]$match.Groups[$match.Groups.Count - 1].Value) }
	$pageResults = [ordered]@{}
	foreach ($label in $pageSamples.Keys) { $pageResults[$label] = Get-Statistics ([double[]]$pageSamples[$label].ToArray()) }
	$fixtures.Add([ordered]@{
		id = $fixture.id; expected_pages = $fixture.expected.pages; observed_pages = [int]$opens[0].Groups[3].Value
		large_document = ($fixture.id -eq 'pathogenic_bacteria_1896')
		open = Get-Statistics ([double[]]@($opens | ForEach-Object { [double]::Parse($_.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture) }))
		decode = $pageResults; peak_working_set_bytes = $peak
	})
}

[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$fixtureResults = @($fixtures | ForEach-Object { $_ })
$report = [ordered]@{ schema_version = 1; commit_sha = $commit; platform = 'x64'; configuration = 'Release'; runs = $Runs; coldish_run = 0; repeated_runs = $Runs - 1; fixtures = $fixtureResults }
$jsonPath = Join-Path $OutputDirectory 'djvu-performance-baseline.json'
$textPath = Join-Path $OutputDirectory 'djvu-performance-baseline.txt'
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("DjVu performance baseline: Release x64, commit $commit")
$lines.Add("Runs per fixture: $Runs (run 0 cold-ish; subsequent runs warm)")
foreach ($fixture in $fixtures) {
	$lines.Add("")
	$lines.Add("$($fixture.id): pages=$($fixture.observed_pages), peak_working_set_bytes=$($fixture.peak_working_set_bytes)")
	$lines.Add(('  open ms: cold={0:N3}, min={1:N3}, median={2:N3}, max={3:N3}' -f $fixture.open.cold_ms, $fixture.open.min_ms, $fixture.open.median_ms, $fixture.open.max_ms))
	foreach ($label in $fixture.decode.Keys) { $sample = $fixture.decode[$label]; $lines.Add(('  {0} decode ms: cold={1:N3}, min={2:N3}, median={3:N3}, max={4:N3}' -f $label, $sample.cold_ms, $sample.min_ms, $sample.median_ms, $sample.max_ms)) }
}
$lines | Set-Content -LiteralPath $textPath -Encoding UTF8
Write-Host "DjVu performance baseline: PASS ($($fixtures.Count) fixtures)"
Write-Host "JSON: $jsonPath"
Write-Host "Text: $textPath"

[CmdletBinding()]
param(
	[string]$OutputDir,
	[string]$DjVuDumpPath,
	[switch]$Force
)

$ErrorActionPreference = 'Stop'

function Get-DjVuMagic {
	param([string]$Path)
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
	$bytes = [System.IO.File]::ReadAllBytes($Path)
	if ($bytes.Length -lt 8) { return $false }
	return [System.Text.Encoding]::ASCII.GetString($bytes, 0, 8) -eq 'AT&TFORM'
}

function Get-DjVuDumpExecutable {
	param([string]$RequestedPath)
	if ($RequestedPath) {
		if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
			throw "DjVuDumpPath was not found: $RequestedPath"
		}
		return (Resolve-Path -LiteralPath $RequestedPath).Path
	}
	$command = Get-Command djvudump.exe, djvudump -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($command) { return $command.Source }
	return $null
}

function Write-Utf8Lf {
	param([string]$Path, [string]$Content)
	$normalized = $Content -replace "`r?`n", "`n"
	[System.IO.File]::WriteAllText($Path, $normalized, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-DjVuDumpData {
	param([string]$Executable, [string]$FilePath, [string]$DumpPath)
	$output = & $Executable $FilePath 2>&1 | Out-String
	$output | Set-Content -LiteralPath $DumpPath -Encoding UTF8
	if ($LASTEXITCODE -ne 0) { throw "djvudump failed with exit code $LASTEXITCODE" }
	$chunkTypes = @('DIRM', 'NAVM', 'INFO', 'Sjbz', 'Djbz', 'BG44', 'FG44', 'TH44', 'TXTz', 'TXTa', 'ANTz', 'ANTa', 'INCL')
	$chunks = @($chunkTypes | Where-Object { $output -match ("(?m)\\b" + [regex]::Escape($_) + "\\b") })
	$pageCount = ([regex]::Matches($output, '(?m)FORM:DJVU\\b')).Count
	if ($pageCount -eq 0) { $pageCount = $null }
	return [PSCustomObject]@{
		page_count = $pageCount
		chunks = $chunks
		features = [PSCustomObject][ordered]@{
			jb2_mask = ($chunks -contains 'Sjbz')
			shared_jb2_dictionary = ($chunks -contains 'Djbz')
			iw44_background = ($chunks -contains 'BG44')
			iw44_foreground = ($chunks -contains 'FG44')
			text_layer = (($chunks -contains 'TXTz') -or ($chunks -contains 'TXTa'))
			annotations = (($chunks -contains 'ANTz') -or ($chunks -contains 'ANTa'))
			navigation_outline = ($chunks -contains 'NAVM')
			indirect_includes = ($chunks -contains 'INCL')
		}
	}
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = $PSScriptRoot }
$root = [System.IO.Path]::GetFullPath($OutputDir)
$filesDir = Join-Path $root 'files'
$manifestsDir = Join-Path $root 'manifests'
$dumpsDir = Join-Path $root 'dumps'
foreach ($directory in @($root, $filesDir, $manifestsDir, $dumpsDir)) {
	[System.IO.Directory]::CreateDirectory($directory) | Out-Null
}

$manifestPath = Join-Path $root 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
	$seedManifestPath = Join-Path $PSScriptRoot 'manifest.json'
	if (-not (Test-Path -LiteralPath $seedManifestPath -PathType Leaf)) {
		throw "Manifest was not found: $seedManifestPath"
	}
	Copy-Item -LiteralPath $seedManifestPath -Destination $manifestPath
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$djvudump = Get-DjVuDumpExecutable $DjVuDumpPath
$failed = New-Object System.Collections.Generic.List[string]

foreach ($entry in $manifest.files) {
	$filePath = Join-Path $root $entry.file
	$partPath = "$filePath.part"
	$fileName = [System.IO.Path]::GetFileName($filePath)
	$expectedHash = $entry.actual.sha256
	try {
		$needDownload = $Force -or -not (Test-Path -LiteralPath $filePath -PathType Leaf)
		if (-not $needDownload) {
			if (-not (Get-DjVuMagic $filePath)) { $needDownload = $true }
			elseif ($entry.actual.sha256) {
				$existingHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
				if ($existingHash -ne $entry.actual.sha256.ToLowerInvariant()) { $needDownload = $true }
			}
		}
		if ($needDownload) {
			Remove-Item -LiteralPath $partPath -Force -ErrorAction SilentlyContinue
			Write-Host "Downloading $fileName"
			Invoke-WebRequest -Uri $entry.download.url -OutFile $partPath -UseBasicParsing
			if (-not (Get-DjVuMagic $partPath)) { throw 'download is empty or does not start with AT&TFORM' }
			if ($expectedHash) {
				$partHash = (Get-FileHash -LiteralPath $partPath -Algorithm SHA256).Hash.ToLowerInvariant()
				if ($partHash -ne $expectedHash.ToLowerInvariant()) {
					throw "SHA-256 mismatch: expected $expectedHash, got $partHash"
				}
			}
			Move-Item -LiteralPath $partPath -Destination $filePath -Force
		}
		if (-not (Get-DjVuMagic $filePath)) { throw 'file does not start with AT&TFORM' }
		$entry.actual.size_bytes = (Get-Item -LiteralPath $filePath).Length
		$actualHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($expectedHash -and $actualHash -ne $expectedHash.ToLowerInvariant()) {
			throw "SHA-256 mismatch: expected $expectedHash, got $actualHash"
		}
		$entry.actual.sha256 = $actualHash
		$entry.validation.magic = 'pass'
		if ($djvudump) {
			$dump = Get-DjVuDumpData $djvudump $filePath (Join-Path $dumpsDir ($entry.id + '.djvudump.txt'))
			$entry.actual.page_count = $dump.page_count
			$entry.actual.chunks = $dump.chunks
			$entry.actual.features = $dump.features
			$entry.validation.djvudump = 'pass'
			if ($null -ne $entry.expected.pages) {
				$entry.validation.page_count = if ($dump.page_count -eq $entry.expected.pages) { 'pass' } else { 'fail' }
				if ($entry.validation.page_count -eq 'fail') { throw "page count is $($dump.page_count), expected $($entry.expected.pages)" }
			} else { $entry.validation.page_count = 'not_checked' }
		} else {
			$entry.validation.djvudump = 'not_checked'
			$entry.validation.page_count = 'not_checked'
		}
		Write-Host "PASS $fileName"
	} catch {
		Remove-Item -LiteralPath $partPath -Force -ErrorAction SilentlyContinue
		$failed.Add("${fileName}: $($_.Exception.Message)")
		Write-Error "FAIL ${fileName}: $($_.Exception.Message)" -ErrorAction Continue
	}
}

$json = $manifest | ConvertTo-Json -Depth 16
Write-Utf8Lf $manifestPath ($json + "`n")
$sumLines = foreach ($entry in $manifest.files) {
	if ($entry.actual.sha256) { "$($entry.actual.sha256)  $($entry.file.Replace('\\', '/'))" }
}
Write-Utf8Lf (Join-Path $root 'SHA256SUMS.txt') (($sumLines -join "`n") + "`n")
foreach ($entry in $manifest.files) {
	$entryJson = $entry | ConvertTo-Json -Depth 16
	Write-Utf8Lf (Join-Path $manifestsDir ($entry.id + '.json')) ($entryJson + "`n")
}

if ($failed.Count -gt 0) {
	Write-Host "Corpus fetch failed for: $($failed -join '; ')"
	exit 1
}
Write-Host "Corpus fetch completed: $($manifest.files.Count) fixtures"

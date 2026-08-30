[CmdletBinding()]
param(
	[string]$OutputDir,
	[string]$DjVuDumpPath,
	[switch]$Force,
	[switch]$ParserSelfTest
)

$ErrorActionPreference = 'Stop'

function Get-DjVuMagic {
	param([string]$Path)
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
	$bytes = New-Object byte[] 8
	$stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
	try {
		$bytesRead = $stream.Read($bytes, 0, $bytes.Length)
	} finally {
		$stream.Dispose()
	}
	return $bytesRead -eq 8 -and [System.Text.Encoding]::ASCII.GetString($bytes) -eq 'AT&TFORM'
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

function ConvertFrom-DjVuDumpOutput {
	param([string]$Output)
	$chunkTypes = @('DIRM', 'NAVM', 'INFO', 'Sjbz', 'Djbz', 'BG44', 'FG44', 'TH44', 'TXTz', 'TXTa', 'ANTz', 'ANTa', 'INCL')
	$chunks = @($chunkTypes | Where-Object { $Output -match ("(?m)\b" + [regex]::Escape($_) + "\b") })
	$pageCount = ([regex]::Matches($Output, '(?m)\bFORM:DJVU\b')).Count
	if ($pageCount -eq 0) { $pageCount = $null }
	$firstInfoMatch = [regex]::Match($Output, '(?m)^[^\r\n]*\bINFO\b[^\r\n]*$')
	$firstInfo = if ($firstInfoMatch.Success) { $firstInfoMatch.Value.Trim() } else { $null }
	return [PSCustomObject]@{
		page_count = $pageCount
		chunks = $chunks
		first_info = $firstInfo
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

function Get-DjVuDumpData {
	param([string]$Executable, [string]$FilePath, [string]$DumpPath)
	$output = & $Executable $FilePath 2>&1 | Out-String
	$output | Set-Content -LiteralPath $DumpPath -Encoding UTF8
	if ($LASTEXITCODE -ne 0) { throw "djvudump failed with exit code $LASTEXITCODE" }
	return ConvertFrom-DjVuDumpOutput $output
}

function Invoke-DjVuDumpParserSelfTest {
	$sample = @'
FORM:DJVM [123]
  DIRM [42]
  NAVM [17]
  FORM:DJVU [100]
    INFO [100x200, 300 dpi]
    Sjbz [12]
    BG44 [34]
    TXTz [56]
  FORM:DJVU [200]
    INFO [400x500, 300 dpi]
    Djbz [78]
    FG44 [90]
    INCL [12]
'@
	$data = ConvertFrom-DjVuDumpOutput $sample
	if ($data.page_count -ne 2) { throw "Parser self-test: expected 2 pages, got $($data.page_count)" }
	foreach ($chunk in @('Sjbz', 'Djbz', 'BG44', 'FG44', 'TXTz', 'NAVM', 'INCL')) {
		if ($data.chunks -notcontains $chunk) { throw "Parser self-test: missing chunk $chunk" }
	}
	if ($data.first_info -ne 'INFO [100x200, 300 dpi]') { throw "Parser self-test: unexpected first INFO '$($data.first_info)'" }
	if (-not $data.features.jb2_mask -or -not $data.features.shared_jb2_dictionary -or -not $data.features.iw44_background -or -not $data.features.iw44_foreground -or -not $data.features.text_layer -or -not $data.features.navigation_outline -or -not $data.features.indirect_includes) {
		throw 'Parser self-test: feature flags are incomplete'
	}
	Write-Host 'DjVuDump parser self-test: PASS'
}

function Set-BigEndianUInt32 {
	param([byte[]]$Bytes, [int]$Offset, [uint32]$Value)
	$Bytes[$Offset] = [byte](($Value -shr 24) -band 0xff)
	$Bytes[$Offset + 1] = [byte](($Value -shr 16) -band 0xff)
	$Bytes[$Offset + 2] = [byte](($Value -shr 8) -band 0xff)
	$Bytes[$Offset + 3] = [byte]($Value -band 0xff)
}

function Find-AsciiBytes {
	param([byte[]]$Bytes, [string]$Text)
	$needle = [Text.Encoding]::ASCII.GetBytes($Text)
	for ($offset = 0; $offset -le $Bytes.Length - $needle.Length; ++$offset) {
		$matches = $true
		for ($index = 0; $index -lt $needle.Length; ++$index) {
			if ($Bytes[$offset + $index] -ne $needle[$index]) { $matches = $false; break }
		}
		if ($matches) { return $offset }
	}
	return -1
}

function New-GeneratedFixture {
	param($Entry, [string]$Root)
	$sourcePath = Join-Path $Root $Entry.generation.source
	$filePath = Join-Path $Root $Entry.file
	if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
		throw "generation source is missing: $($Entry.generation.source)"
	}
	[IO.Directory]::CreateDirectory((Split-Path -Parent $filePath)) | Out-Null
	$sourceBytes = [IO.File]::ReadAllBytes($sourcePath)
	$mode = [string]$Entry.generation.mode
	switch ($mode) {
		'copy' { $result = $sourceBytes }
		'truncate' { $result = $sourceBytes[0..([Math]::Min(31, $sourceBytes.Length - 1))] }
		'invalid_form_length' {
			$result = $sourceBytes.Clone()
			Set-BigEndianUInt32 $result 8 ([uint32]0x7fffffff)
		}
		'invalid_chunk_length' {
			$result = $sourceBytes.Clone()
			$offset = Find-AsciiBytes $result 'INFO'
			if ($offset -lt 0 -or $offset + 8 -gt $result.Length) { throw 'INFO chunk was not found for corruption' }
			Set-BigEndianUInt32 $result ($offset + 4) ([uint32]0x7fffffff)
		}
		'missing_incl' {
			$result = $sourceBytes.Clone()
			$offset = Find-AsciiBytes $result 'INCL'
			if ($offset -lt 0) { throw 'INCL chunk was not found for corruption' }
			[Text.Encoding]::ASCII.GetBytes('JUNK').CopyTo($result, $offset)
		}
		default { throw "unknown fixture generation mode: $mode" }
	}
	[IO.File]::WriteAllBytes($filePath, $result)
}

if ($ParserSelfTest) {
	Invoke-DjVuDumpParserSelfTest
	exit 0
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
	if (-not $entry.actual.PSObject.Properties['first_info']) {
		$entry.actual | Add-Member -NotePropertyName first_info -NotePropertyValue $null
	}
	$filePath = Join-Path $root $entry.file
	$partPath = "$filePath.part"
	$fileName = [System.IO.Path]::GetFileName($filePath)
	$expectedHash = $entry.actual.sha256
	try {
		if ($entry.PSObject.Properties['generation']) {
			New-GeneratedFixture $entry $root
			$entry.actual.size_bytes = (Get-Item -LiteralPath $filePath).Length
			$actualHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
			if ($expectedHash -and $actualHash -ne $expectedHash.ToLowerInvariant()) {
				throw "generated SHA-256 mismatch: expected $expectedHash, got $actualHash"
			}
			$entry.actual.sha256 = $actualHash
			$entry.validation.magic = if (Get-DjVuMagic $filePath) { 'pass' } else { 'expected_fail' }
			$entry.validation.djvudump = 'not_checked'
			$entry.validation.page_count = 'not_checked'
			Write-Host "PASS generated $fileName"
			continue
		}
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
			$entry.actual.first_info = $dump.first_info
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

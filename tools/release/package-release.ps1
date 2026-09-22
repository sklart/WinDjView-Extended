[CmdletBinding()]
param([ValidateSet('Win32', 'x64')][string] $Platform = 'Win32', [string] $OutputDirectory = (Join-Path $PSScriptRoot '..\..\out\release'))

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$versionHeader = Join-Path $root 'src\Version.h'
$versionText = Get-Content -LiteralPath $versionHeader -Raw
function Get-VersionPart([string] $name) {
    $match = [regex]::Match($versionText, "(?m)^#define\s+$name\s+(\d+)\s*$")
    if (-not $match.Success) { throw "Missing $name in $versionHeader" }
    $match.Groups[1].Value
}
$version = '{0}.{1}.{2}' -f (Get-VersionPart 'WINDJVIEW_VERSION_MAJOR'), (Get-VersionPart 'WINDJVIEW_VERSION_MINOR'), (Get-VersionPart 'WINDJVIEW_VERSION_PATCH')
$sourceDirectory = if ($Platform -eq 'x64') { Join-Path $root 'src\Release_x64' } else { Join-Path $root 'src\Release' }
$exe = Join-Path $sourceDirectory 'WinDjView.exe'
$ruDll = Join-Path $sourceDirectory "WinDjViewRU-$Platform.dll"
foreach ($path in @($exe, $ruDll)) { if (-not (Test-Path -LiteralPath $path)) { throw "Missing Release input: $path" } }
function Get-PeMachine([string] $path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw "Not a PE file: $path" }
    $offset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 0 -or $offset + 6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $offset) -ne 0x00004550) { throw "Invalid PE header: $path" }
    [BitConverter]::ToUInt16($bytes, $offset + 4)
}
$expectedMachine = if ($Platform -eq 'x64') { 0x8664 } else { 0x014c }
foreach ($path in @($exe, $ruDll)) { if ((Get-PeMachine $path) -ne $expectedMachine) { throw "Wrong $Platform architecture: $path" } }
foreach ($property in @('FileVersion', 'ProductVersion')) { if ((Get-Item -LiteralPath $exe).VersionInfo.$property -ne $version) { throw "$property of $exe does not match $version" } }
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($dumpbin) {
    $imports = & $dumpbin.Source /imports $exe
    if ($LASTEXITCODE -ne 0) { throw "dumpbin /imports failed for $exe" }
    if ($imports -match '(?im)^\s*(?:lib)?(?:jpeg|djvu)[^\s]*\.dll\s*$') { throw 'Release executable imports an external JPEG/DjVu DLL' }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$name = "WinDjView-Extended-$version-$Platform"; $staging = Join-Path $OutputDirectory "$name.staging"
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging | Out-Null
Copy-Item -LiteralPath $exe, $ruDll, (Join-Path $root 'README.md') -Destination $staging
Copy-Item -LiteralPath (Join-Path $root 'src\license') -Destination (Join-Path $staging 'LICENSE-GPL-2.0.txt')
Copy-Item -LiteralPath (Join-Path $root 'src\third_party\libjpeg-turbo\LICENSE.md') -Destination (Join-Path $staging 'THIRD-PARTY-libjpeg-turbo-LICENSE.md')
$zip = Join-Path $OutputDirectory "$name.zip"; if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $staging | ForEach-Object FullName) -DestinationPath $zip -CompressionLevel Optimal
if (-not (Test-Path -LiteralPath $zip) -or (Get-Item -LiteralPath $zip).Length -eq 0) { throw "Failed to create $zip" }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try { foreach ($entry in @('WinDjView.exe', "WinDjViewRU-$Platform.dll", 'README.md', 'LICENSE-GPL-2.0.txt', 'THIRD-PARTY-libjpeg-turbo-LICENSE.md')) { if (-not ($archive.Entries.Name -contains $entry)) { throw "Missing $entry in $zip" } } } finally { $archive.Dispose() }
Remove-Item -LiteralPath $staging -Recurse -Force
Write-Host "Release package verified: $zip"

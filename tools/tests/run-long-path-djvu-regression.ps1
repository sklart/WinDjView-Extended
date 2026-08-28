param(
    [Parameter(Mandatory = $true)] [string] $TestExecutable,
    [Parameter(Mandatory = $true)] [string] $Fixture
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $TestExecutable -PathType Leaf)) {
    throw "Test executable is missing: $TestExecutable"
}
if (-not (Test-Path -LiteralPath $Fixture -PathType Leaf)) {
    throw "DjVu fixture is missing: $Fixture"
}

$root = Join-Path $env:TEMP ('WinDjView DjVu ' + [char]0x0416)
$segments = 1..12 | ForEach-Object { 'long directory {0:D2} Unicode' -f $_ }
$directory = $root
try {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    foreach ($segment in $segments) {
        $directory = Join-Path $directory $segment
        [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    $document = Join-Path $directory 'minimal.djvu'
    [System.IO.File]::Copy($Fixture, $document, $true)
    if ($document.Length -le 260) {
        throw "Regression path is not long enough: $($document.Length)"
    }
    & $TestExecutable $document
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

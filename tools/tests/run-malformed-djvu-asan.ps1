param(
    [Parameter(Mandatory = $true)][string]$TestExecutable,
    [Parameter(Mandatory = $true)][string]$CorpusRoot,
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
$fixtures = @(
    @{ Path = 'files/watchmaker.djvu'; Mode = 'positive' },
    @{ Path = 'files/generated/corrupt_truncated.djvu'; Mode = 'malformed' },
    @{ Path = 'files/generated/corrupt_form_length.djvu'; Mode = 'malformed' },
    @{ Path = 'files/generated/corrupt_chunk_length.djvu'; Mode = 'malformed' },
    @{ Path = 'files/generated/corrupt_missing_incl.djvu'; Mode = 'malformed' }
)

function Save-Diagnostic([string]$FixturePath, [string]$Text) {
    $directory = Join-Path $PSScriptRoot 'artifacts\malformed-djvu-asan'
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $name = ([IO.Path]::GetFileNameWithoutExtension($FixturePath) + '.log')
    [IO.File]::WriteAllText((Join-Path $directory $name), $Text)
}

foreach ($fixture in $fixtures) {
    $path = Join-Path $CorpusRoot $fixture.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Fixture is missing: $path"
    }

    $arguments = '"{0}" {1}' -f $path.Replace('"', '""'), $fixture.Mode
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $TestExecutable
    $startInfo.Arguments = $arguments
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Could not start fixture: $($fixture.Path)" }
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        Save-Diagnostic $fixture.Path "Timed out after $TimeoutSeconds seconds."
        throw "Timed out after $TimeoutSeconds seconds: $($fixture.Path)"
    }
    $exitCode = [int]$process.ExitCode
    $output = $process.StandardOutput.ReadToEnd() + $process.StandardError.ReadToEnd()
    Write-Host "[$($fixture.Path)]"
    Write-Host $output
    if ($output -match '(?im)addresssanitizer|asan:|error: address sanitizer|heap-buffer-overflow|use-after-free|double-free|stack-buffer-overflow') {
        Save-Diagnostic $fixture.Path $output
        throw "AddressSanitizer reported an error: $($fixture.Path)"
    }
    $expected = if ($fixture.Mode -eq 'positive') { 'ASAN_RESULT: PASS' } else { 'ASAN_RESULT: CONTROLLED_FAILURE' }
    if ($exitCode -ne 0 -or $output -notmatch [regex]::Escape($expected)) {
        Save-Diagnostic $fixture.Path $output
        throw "Unexpected result for $($fixture.Path), exit code $exitCode"
    }
}

Write-Host 'Malformed DjVu ASan regression PASS'

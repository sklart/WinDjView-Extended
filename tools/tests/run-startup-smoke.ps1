param(
	[Parameter(Mandatory = $true)]
	[string]$ExecutablePath,
	[int]$TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
	throw "Startup smoke executable was not found: $ExecutablePath"
}

$process = Start-Process -FilePath $ExecutablePath -ArgumentList '/StartupSmokeTest' -PassThru -WindowStyle Hidden
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	Stop-Process -Id $process.Id -Force
	throw "Startup smoke timed out after $TimeoutSeconds seconds: $ExecutablePath"
}

if ($process.ExitCode -ne 0) {
	throw "Startup smoke exited with $($process.ExitCode): $ExecutablePath"
}

Write-Host "Startup smoke passed: $ExecutablePath"

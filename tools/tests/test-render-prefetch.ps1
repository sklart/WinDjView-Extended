[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Require-SourcePattern {
	param([string]$Path, [string]$Pattern, [string]$Description)
	$source = Get-Content -LiteralPath (Join-Path $root $Path) -Raw
	if ($source -notmatch $Pattern) { throw "Missing $Description in $Path" }
}

Require-SourcePattern 'src\RenderThread.h' 'AddPrefetchJob\s*\(int nPage\)' 'prefetch job API'
Require-SourcePattern 'src\RenderThread.h' 'enum JobType \{ RENDER, DECODE, PREFETCH_DECODE, READINFO, CLEANUP \}' 'prefetch job type'
Require-SourcePattern 'src\RenderThread.cpp' 'job\.type == PREFETCH_DECODE.*existing->type == RENDER' 'visible-job priority guard'
Require-SourcePattern 'src\RenderThread.cpp' 'if \(job\.type == PREFETCH_DECODE\)\s*\{\s*m_jobs\.push_back\(job\)' 'low-priority queue insertion'
Require-SourcePattern 'src\RenderThread.cpp' 'RemoveFromQueue\(job\.nPage\)' 'per-page job de-duplication'
Require-SourcePattern 'src\DjVuView.cpp' 'AddPrefetchPage\(nLastVisible \+ 1, add, remove\);\s*AddPrefetchPage\(nFirstVisible - 1, add, remove\);' 'next/previous prefetch order'
Require-SourcePattern 'src\DjVuView.cpp' 'remove\.erase\(std::remove\(remove\.begin\(\), remove\.end\(\), nPage\), remove\.end\(\)\)' 'prefetch cache retention'
Require-SourcePattern 'src\DjVuView.cpp' 'm_pRenderThread->RemoveAllJobs\(\);' 'stale queued job cleanup'
Require-SourcePattern 'src\DjVuView.cpp' 'ScheduleAdjacentPrefetch\(add, remove\);\s*// Notify the source' 'prefetch observer update'

Write-Host 'Render prefetch source regression: PASS'

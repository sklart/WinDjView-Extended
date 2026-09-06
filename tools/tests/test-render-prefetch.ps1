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
Require-SourcePattern 'src\RenderThread.h' 'enum JobPriority \{ CurrentPageRender, VisibleRender, Decode, AdjacentPrefetch, Background \}' 'explicit scheduler priorities'
Require-SourcePattern 'src\RenderThread.cpp' 'existing->priority < AdjacentPrefetch' 'visible-job priority guard'
Require-SourcePattern 'src\RenderThread.cpp' 'insertAt->priority <= job\.priority' 'priority queue insertion'
Require-SourcePattern 'src\RenderThread.cpp' 'RemoveFromQueue\(job\.nPage\)' 'per-page job de-duplication'
Require-SourcePattern 'src\RenderThread.cpp' 'HasSameRenderIdentity\(job, \*existing\)' 'render identity replacement'
Require-SourcePattern 'src\RenderThread.h' 'struct JobWindows' 'job-aware reconciliation windows'
Require-SourcePattern 'src\RenderThread.cpp' 'void CRenderThread::ReconcileJobs\(const JobWindows& windows\)' 'job-aware reconciliation API'
Require-SourcePattern 'src\DjVuView.cpp' 'm_pRenderThread->ReconcileJobs\(windows\);' 'production job-aware viewport cleanup'
Require-SourcePattern 'src\RenderThread.cpp' 'case CLEANUP: pages = &windows\.cleanupPages' 'cleanup reconciliation rule'
Require-SourcePattern 'src\RenderThread.cpp' 'case READINFO: pages = &windows\.readInfoPages' 'read-info reconciliation rule'
Require-SourcePattern 'src\DjVuView.cpp' 'UpdatePageCache\(rcViewport\.Size\(\), nLastPage, bUpdateImages, add, remove, renderPages, decodePages, readInfoPages, cleanupPages, true\);' 'continuous largest-visible foreground priority'
Require-SourcePattern 'src\RenderThread.cpp' 'bool CRenderThread::GetCurrentJobInfo\(JobInfo& job\)' 'live-worker scheduler inspection'
Require-SourcePattern 'src\RenderThread.cpp' 'm_pSource->StartPrefetch\(job\.nPage\)' 'non-blocking speculative decode'
Require-SourcePattern 'src\RenderThread.cpp' 'case DECODE:\s*pThread->m_pSource->GetPage\(job\.nPage, pThread->m_pOwner\);' 'synchronous visible decode'
Require-SourcePattern 'src\RenderThread.cpp' 'm_pSource->CancelPrefetches\(\)' 'prefetch cancellation on navigation and close'
Require-SourcePattern 'src\DjVuView.cpp' 'AddPrefetchPage\(nNextPage, add, remove, prefetchPages\);\s*AddPrefetchPage\(nPreviousPage, add, remove, prefetchPages\);' 'next/previous prefetch order'
Require-SourcePattern 'src\DjVuView.cpp' 'remove\.erase\(std::remove\(remove\.begin\(\), remove\.end\(\), nPage\), remove\.end\(\)\)' 'prefetch cache retention'
Require-SourcePattern 'src\DjVuView.cpp' 'readInfoPages\.insert\(nPage\)' 'read-info job window tracking'
Require-SourcePattern 'src\DjVuView.cpp' 'renderPages\.insert\(nPage\)' 'render job window tracking'
Require-SourcePattern 'src\DjVuView.cpp' 'decodePages\.insert\(nPage\)' 'decode job window tracking'
Require-SourcePattern 'src\DjVuView.cpp' 'cleanupPages\.insert\(nPage\)' 'cleanup job window tracking'
Require-SourcePattern 'src\DjVuView.cpp' 'prefetchPages\.insert\(nPage\)' 'prefetch job window tracking'
Require-SourcePattern 'src\DjVuView.cpp' 'ScheduleAdjacentPrefetch\(add, remove, windows\.prefetchPages\);[\s\S]*?m_pSource->ChangeObservedPages\(this, add, remove\);' 'prefetch observer update'
Require-SourcePattern 'src\DjVuView.cpp' 'Job windows are recorded at each actual scheduling decision above\.\s*// add/remove remain strictly observer/cache ownership changes\.' 'independent production scheduler windows'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->resume_decode\(false\)' 'asynchronous prefetch start'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->stop_decode\(false\)' 'asynchronous prefetch cancellation'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->resume_decode\(false\);\s*// CancelPrefetches\(\) can run[\s\S]*?file->stop_decode\(false\);' 'post-resume cancellation race guard'
Require-SourcePattern 'src\DjVuSource.cpp' 'bool DjVuSource::IsPrefetchActive\(int nPage\)' 'active prefetch runtime inspection'

Write-Host 'Render prefetch source regression: PASS'

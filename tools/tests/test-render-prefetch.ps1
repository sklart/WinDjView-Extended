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
Require-SourcePattern 'src\RenderScheduler.h' 'enum JobType \{ RENDER, DECODE, PREFETCH_DECODE, READINFO, CLEANUP(?:, TILE_RENDER)? \}' 'prefetch job type'
Require-SourcePattern 'src\RenderScheduler.h' 'enum JobPriority \{ CurrentPageRender, VisibleRender, Decode, AdjacentPrefetch, Background \}' 'explicit scheduler priorities'
Require-SourcePattern 'src\RenderScheduler.cpp' 'existing->priority < AdjacentPrefetch' 'visible-job priority guard'
Require-SourcePattern 'src\RenderScheduler.cpp' 'insertAt->priority <= job\.priority' 'priority queue insertion'
Require-SourcePattern 'src\RenderScheduler.cpp' 'RemoveFromQueue\(nPage\)' 'per-page job de-duplication'
Require-SourcePattern 'src\RenderScheduler.cpp' 'HasSameRenderIdentity\(job, \*existing\)' 'render identity replacement'
Require-SourcePattern 'src\RenderScheduler.cpp' 'existing->type == RENDER && job\.type == DECODE' 'render-to-decode semantic replacement'
Require-SourcePattern 'src\RenderScheduler.cpp' 'const bool queueCleanupAfterCurrent = job\.type == CLEANUP' 'cleanup queueing behind running foreground work'
Require-SourcePattern 'src\RenderScheduler.cpp' 'The same render became useful again before completion' 'same-identity running-render revival'
Require-SourcePattern 'src\RenderScheduler.cpp' 'm_bRejectCurrentJob = false' 're-entry clears only the exact render rejection'
Require-SourcePattern 'src\RenderScheduler.cpp' 'return left\.request == right\.request' 'single RenderRequest identity comparison'
Require-SourcePattern 'src\RenderScheduler.h' 'struct JobWindows' 'job-aware reconciliation windows'
Require-SourcePattern 'src\RenderThread.cpp' 'm_scheduler\.Reconcile\(windows\)' 'job-aware reconciliation API'
Require-SourcePattern 'src\DjVuView.cpp' 'm_pRenderThread->ReconcileJobs\(windows\);' 'production job-aware viewport cleanup'
Require-SourcePattern 'src\RenderScheduler.cpp' 'cleanupPages contains requests made by this update, not a whitelist' 'persistent cleanup lifecycle rule'
Require-SourcePattern 'src\RenderScheduler.cpp' 'it->type == CLEANUP \|\|' 'cleanup retention outside the current request set'
Require-SourcePattern 'src\RenderScheduler.cpp' 'windows\.readInfoPages\.find\(it->GetPage\(\)\)' 'read-info cleanup cancellation on re-entry'
Require-SourcePattern 'src\RenderScheduler.cpp' 'case READINFO: pages = &windows\.readInfoPages' 'read-info reconciliation rule'
Require-SourcePattern 'src\DjVuView.cpp' 'PageWorkingSet::BuildVisits\(layout, m_nPageCount, m_nPage, topPage, bottomPage,\s*foregroundPage' 'continuous largest-visible foreground visit'
Require-SourcePattern 'src\DjVuView.cpp' 'visit\.foreground \|\| nPage == m_nPage \? CRenderThread::CurrentPageRender' 'continuous largest-visible render priority'
Require-SourcePattern 'src\RenderThread.cpp' 'bool CRenderThread::GetCurrentJobInfo\(JobInfo& job\)' 'live-worker scheduler inspection'
Require-SourcePattern 'src\RenderThread.cpp' 'm_pSource->StartPrefetch\(job\.GetPage\(\)\)' 'non-blocking speculative decode'
Require-SourcePattern 'src\RenderThread.cpp' 'case DECODE:\s*pThread->m_pSource->GetPage\(job\.GetPage\(\), pThread->m_pOwner\);' 'synchronous visible decode'
Require-SourcePattern 'src\RenderThread.cpp' 'm_pSource->CancelPrefetches\(\)' 'prefetch cancellation on navigation and close'
Require-SourcePattern 'src\DjVuView.cpp' 'AddPrefetchPage\(nNextPage, add, remove, prefetchPages\);\s*AddPrefetchPage\(nPreviousPage, add, remove, prefetchPages\);' 'next/previous prefetch order'
Require-SourcePattern 'src\PageWorkingSet.cpp' 'result\.removeObserved\.erase\(std::remove\(' 'prefetch cache retention'
Require-SourcePattern 'src\PageWorkingSet.cpp' 'result\.readInfoPages\.insert\(page\)' 'read-info job window tracking'
Require-SourcePattern 'src\PageWorkingSet.cpp' 'result\.renderPages\.insert\(page\)' 'render job window tracking'
Require-SourcePattern 'src\PageWorkingSet.cpp' 'result\.decodePages\.insert\(page\)' 'decode job window tracking'
Require-SourcePattern 'src\PageWorkingSet.cpp' 'result\.cleanupPages\.insert\(page\)' 'cleanup job window tracking'
Require-SourcePattern 'src\PageWorkingSet.cpp' 'result\.prefetchPages\.insert\(page\)' 'prefetch job window tracking'
Require-SourcePattern 'src\DjVuView.cpp' 'ScheduleAdjacentPrefetch\(add, remove, windows\.prefetchPages\);[\s\S]*?m_pSource->ChangeObservedPages\(this, add, remove\);' 'prefetch observer update'
Require-SourcePattern 'src\DjVuView.cpp' 'Job windows are recorded at each actual scheduling decision above\.\s*// add/remove remain strictly observer/cache ownership changes\.' 'independent production scheduler windows'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->resume_decode\(false\)' 'asynchronous prefetch start'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->stop_decode\(false\)' 'asynchronous prefetch cancellation'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->resume_decode\(false\);\s*// CancelPrefetches\(\) can run[\s\S]*?file->stop_decode\(false\);' 'post-resume cancellation race guard'
Require-SourcePattern 'src\DjVuSource.cpp' 'bool DjVuSource::IsPrefetchActive\(int nPage\)' 'active prefetch runtime inspection'

Write-Host 'Render prefetch source regression: PASS'

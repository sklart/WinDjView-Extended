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
Require-SourcePattern 'src\RenderThread.cpp' 'DiscardJobsOutside\(const set<int>& pages\)' 'obsolete job removal API'
Require-SourcePattern 'src\DjVuView.cpp' 'set<int> schedulerPages\(add\.begin\(\), add\.end\(\)\);\s*m_pRenderThread->DiscardJobsOutside\(schedulerPages\);' 'production scheduler-aware viewport cleanup'
Require-SourcePattern 'src\DjVuView.cpp' 'UpdatePageCache\(rcViewport\.Size\(\), nLastPage, bUpdateImages, add, remove, true\);' 'continuous largest-visible foreground priority'
Require-SourcePattern 'src\RenderThread.cpp' 'bool CRenderThread::GetCurrentJobInfo\(JobInfo& job\)' 'live-worker scheduler inspection'
Require-SourcePattern 'src\RenderThread.cpp' 'm_pSource->StartPrefetch\(job\.nPage\)' 'non-blocking speculative decode'
Require-SourcePattern 'src\RenderThread.cpp' 'case DECODE:\s*pThread->m_pSource->GetPage\(job\.nPage, pThread->m_pOwner\);' 'synchronous visible decode'
Require-SourcePattern 'src\RenderThread.cpp' 'm_pSource->CancelPrefetches\(\)' 'prefetch cancellation on navigation and close'
Require-SourcePattern 'src\DjVuView.cpp' 'AddPrefetchPage\(nLastVisible \+ 1, add, remove\);\s*AddPrefetchPage\(nFirstVisible - 1, add, remove\);' 'next/previous prefetch order'
Require-SourcePattern 'src\DjVuView.cpp' 'remove\.erase\(std::remove\(remove\.begin\(\), remove\.end\(\), nPage\), remove\.end\(\)\)' 'prefetch cache retention'
Require-SourcePattern 'src\DjVuView.cpp' 'ScheduleAdjacentPrefetch\(add, remove\);[\s\S]*?m_pSource->ChangeObservedPages\(this, add, remove\);' 'prefetch observer update'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->resume_decode\(false\)' 'asynchronous prefetch start'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->stop_decode\(false\)' 'asynchronous prefetch cancellation'
Require-SourcePattern 'src\DjVuSource.cpp' 'file->resume_decode\(false\);\s*// CancelPrefetches\(\) can run[\s\S]*?file->stop_decode\(false\);' 'post-resume cancellation race guard'
Require-SourcePattern 'src\DjVuSource.cpp' 'bool DjVuSource::IsPrefetchActive\(int nPage\)' 'active prefetch runtime inspection'

Write-Host 'Render prefetch source regression: PASS'

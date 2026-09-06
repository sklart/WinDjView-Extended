#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
#include "../../src/libdjvu/DataPool.h"
#include "../../src/libdjvu/DjVuFile.h"
#include <process.h>
#include <stdio.h>

namespace
{
	class RegressionApplication : public IApplication
	{
	public:
		virtual bool LoadDocSettings(const CString&, DocSettings*) { return false; }
		virtual bool GetCropPages() { return false; }
		virtual DictionaryInfo* GetDictionaryInfo(const CString&, bool) { return NULL; }
		virtual void ReportFatalError() { }
	};

	class RegressionObserver : public Observer
	{
	public:
		RegressionObserver() : m_event(::CreateEvent(NULL, TRUE, FALSE, NULL)),
			m_renderEvent(::CreateEvent(NULL, TRUE, FALSE, NULL)), m_page(-1), m_renderPage(-1), m_renderCount(0) { }
		virtual ~RegressionObserver() { ::CloseHandle(m_event); ::CloseHandle(m_renderEvent); }

		virtual void OnUpdate(const Observable*, const Message* message)
		{
			if (message != NULL && message->code == PAGE_DECODED)
			{
				const PageMsg* page = static_cast<const PageMsg*>(message);
				InterlockedExchange(&m_page, page->nPage);
				::SetEvent(m_event);
			}
			else if (message != NULL && message->code == PAGE_RENDERED)
			{
				const BitmapMsg* bitmap = static_cast<const BitmapMsg*>(message);
				delete bitmap->pDIB;
				InterlockedExchange(&m_renderPage, bitmap->nPage);
				InterlockedIncrement(&m_renderCount);
				::SetEvent(m_renderEvent);
			}
		}

		void Reset() { InterlockedExchange(&m_page, -1); ::ResetEvent(m_event); }
		bool WaitForDecode(int page, DWORD timeout)
		{
			return ::WaitForSingleObject(m_event, timeout) == WAIT_OBJECT_0 &&
				InterlockedCompareExchange(&m_page, 0, 0) == page;
		}
		void ResetRender()
		{
			InterlockedExchange(&m_renderPage, -1);
			InterlockedExchange(&m_renderCount, 0);
			::ResetEvent(m_renderEvent);
		}
		bool WaitForRender(int page, DWORD timeout)
		{
			return ::WaitForSingleObject(m_renderEvent, timeout) == WAIT_OBJECT_0 &&
				InterlockedCompareExchange(&m_renderPage, 0, 0) == page;
		}
		long GetRenderCount() { return InterlockedCompareExchange(&m_renderCount, 0, 0); }

	private:
		HANDLE m_event;
		HANDLE m_renderEvent;
		volatile LONG m_page;
		volatile LONG m_renderPage, m_renderCount;
	};

	bool expect(bool condition, const char* description)
	{
		if (!condition)
			fprintf(stderr, "prefetch regression failed: %s\n", description);
		return condition;
	}

	struct PrefetchRace
	{
		DjVuSource* source;
		int page;
		HANDLE started;
	};

	unsigned int __stdcall StartPrefetchProc(void* data)
	{
		PrefetchRace* race = static_cast<PrefetchRace*>(data);
		::SetEvent(race->started);
		race->source->StartPrefetch(race->page);
		return 0;
	}

	bool WaitForDecodeStop(DjVuSource* source, int page, DWORD timeout)
	{
		GP<DjVuFile> file = source->GetDjVuDoc()->get_djvu_file(page);
		if (!file)
			return false;
		const DWORD start = ::GetTickCount();
		while (file->is_decoding())
		{
			if (::GetTickCount() - start >= timeout)
				return false;
			::Sleep(1);
		}
		return true;
	}
}

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0) || argc != 2)
		return 2;

	RegressionApplication application;
	RegressionObserver observer;
	DjVuSource::SetApplication(&application);
	DjVuSource* source = DjVuSource::FromFile(argv[1]);
	if (source == NULL || source->GetPageCount() == 0)
		return 1;

	const int first = 0;
	const int last = source->GetPageCount() - 1;
	const int adjacent = source->GetPageCount() > 1 ? 1 : 0;
	const int distant = last;
	CRenderThread* thread = new CRenderThread(source, &observer);
	thread->PauseJobs();

	bool passed = true;
	const CDisplaySettings displaySettings;
	// Queue order is semantic rather than insertion order: foreground must
	// always run before normal decode, speculative prefetch, and maintenance.
	if (source->GetPageCount() >= 5)
	{
		thread->RemoveAllJobs();
		thread->ResetSchedulerMetrics();
		thread->AddPrefetchJob(3);
		thread->AddReadInfoJob(4);
		thread->AddDecodeJob(2);
		thread->AddJob(1, 0, CSize(800, 1000), displaySettings,
			CDjVuView::Color, CRenderThread::VisibleRender);
		thread->AddJob(0, 0, CSize(800, 1000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		vector<CRenderThread::JobInfo> jobs;
		thread->GetQueuedJobInfo(jobs);
		passed &= expect(jobs.size() == 5 && jobs[0].priority == CRenderThread::CurrentPageRender &&
			jobs[1].priority == CRenderThread::VisibleRender && jobs[2].priority == CRenderThread::Decode &&
			jobs[3].priority == CRenderThread::AdjacentPrefetch && jobs[4].priority == CRenderThread::Background,
			"current and visible renders must precede decode, prefetch, and maintenance");

		// Reconciliation is semantic: leaving work is removed by type, while
		// cleanup/read-info remain when their own windows still require them.
		thread->RemoveAllJobs();
		thread->ResetSchedulerMetrics();
		thread->AddJob(0, 0, CSize(800, 1000), displaySettings);
		thread->AddDecodeJob(1);
		thread->AddPrefetchJob(2);
		thread->AddReadInfoJob(3);
		thread->AddCleanupJob(4);
		CRenderThread::JobWindows windows;
		windows.decodePages.insert(1);
		windows.prefetchPages.insert(2);
		windows.readInfoPages.insert(3);
		windows.cleanupPages.insert(4);
		thread->ReconcileJobs(windows);
		thread->GetQueuedJobInfo(jobs);
		CRenderThread::SchedulerMetrics metrics;
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(jobs.size() == 4 && metrics.obsoleteJobsRemoved == 1,
			"obsolete render must be removed while needed decode/prefetch/read-info/cleanup remain");

		// Re-entering the cache window cancels a queued cleanup before it can
		// remove a page that is again required by foreground work.
		windows = CRenderThread::JobWindows();
		windows.renderPages.insert(4);
		thread->ReconcileJobs(windows);
		thread->GetQueuedJobInfo(jobs);
		thread->GetSchedulerMetrics(metrics);
		bool cleanupCancelled = true;
		for (size_t job = 0; job < jobs.size(); ++job)
			cleanupCancelled &= !(jobs[job].nPage == 4 && jobs[job].type == CRenderThread::CLEANUP);
		passed &= expect(cleanupCancelled,
			"page re-entry must cancel its stale cleanup request");
		thread->RemoveAllJobs();

		// cleanupPages contains new requests, not the complete lifetime of
		// queued cleanup. An unrelated update must not silently drop it.
		GP<DjVuImage> cachedPage = source->GetPage(4, &observer);
		passed &= expect(cachedPage != NULL && source->IsPageCached(4, &observer),
			"cleanup regression fixture page must be cached by the observer");
		thread->ResetSchedulerMetrics();
		thread->AddCleanupJob(4);
		windows = CRenderThread::JobWindows();
		windows.renderPages.insert(0); // Unrelated viewport/cache work.
		thread->ReconcileJobs(windows);
		thread->GetQueuedJobInfo(jobs);
		bool cleanupRetained = false;
		for (size_t job = 0; job < jobs.size(); ++job)
			cleanupRetained |= jobs[job].nPage == 4 && jobs[job].type == CRenderThread::CLEANUP;
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(cleanupRetained && metrics.obsoleteJobsRemoved == 0,
			"unrelated reconciliation must retain pending cleanup without obsolete metrics");
		for (int page = 0; page < 3; ++page)
		{
			thread->AddJob(page, 0, CSize(800, 1000), displaySettings,
				CDjVuView::Color, CRenderThread::VisibleRender);
			windows = CRenderThread::JobWindows();
			windows.renderPages.insert(page);
			thread->ReconcileJobs(windows);
		}
		thread->GetQueuedJobInfo(jobs);
		cleanupRetained = false;
		for (size_t job = 0; job < jobs.size(); ++job)
			cleanupRetained |= jobs[job].nPage == 4 && jobs[job].type == CRenderThread::CLEANUP;
		passed &= expect(cleanupRetained && jobs.size() == 2 &&
			jobs[0].type == CRenderThread::RENDER && jobs[1].type == CRenderThread::CLEANUP,
			"fast scroll must retain cleanup, bound the queue, and keep foreground first");
		thread->ResumeJobs();
		bool cleanupExecuted = false;
		for (int attempt = 0; attempt < 30000 && !cleanupExecuted; ++attempt)
		{
			thread->GetQueuedJobInfo(jobs);
			bool queuedCleanup = false;
			for (size_t job = 0; job < jobs.size(); ++job)
				queuedCleanup |= jobs[job].nPage == 4 && jobs[job].type == CRenderThread::CLEANUP;
			CRenderThread::JobInfo current;
			bool runningCleanup = thread->GetCurrentJobInfo(current) &&
				current.nPage == 4 && current.type == CRenderThread::CLEANUP;
			cleanupExecuted = !source->IsPageCached(4, &observer) && !queuedCleanup && !runningCleanup;
			if (!cleanupExecuted)
				::Sleep(1);
		}
		thread->PauseJobs();
		passed &= expect(cleanupExecuted,
			"retained cleanup must execute RemoveFromCache and then leave the queue");
		thread->AddReadInfoJob(3);
		thread->AddCleanupJob(4);
		thread->ResetSchedulerMetrics();
		thread->RemoveAllJobs();
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(metrics.obsoleteJobsRemoved == 0,
			"RemoveAllJobs must not count read-info or cleanup as obsolete work");

		// Exercise the live worker path as well: a render that has already left
		// the queue is not interrupted, but a viewport jump rejects its result
		// and leaves the new foreground page first in the queue.
		const int runningPage = source->GetPageCount() > 11 ? 10 : 0;
		const int targetPage = source->GetPageCount() > 11 ? 11 : last;
		thread->ResetSchedulerMetrics();
		thread->AddJob(runningPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->ResumeJobs();
		CRenderThread::JobInfo current;
		bool running = false;
		for (int attempt = 0; attempt < 5000 && !running; ++attempt)
		{
			running = thread->GetCurrentJobInfo(current) && current.nPage == runningPage &&
				current.type == CRenderThread::RENDER;
			if (!running)
				::Sleep(1);
		}
		windows = CRenderThread::JobWindows();
		windows.renderPages.insert(targetPage);
		thread->ReconcileJobs(windows);
		thread->PauseJobs();
		thread->AddJob(targetPage, 0, CSize(800, 1000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->GetQueuedJobInfo(jobs);
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(running && !jobs.empty() && jobs[0].nPage == targetPage &&
			jobs[0].priority == CRenderThread::CurrentPageRender && metrics.obsoleteJobsRejected > 0,
			"live jump must reject the stale render and queue the new foreground page next");
		thread->RemoveAllJobs();
		bool drained = false;
		for (int attempt = 0; attempt < 30000 && !drained; ++attempt)
		{
			drained = !thread->GetCurrentJobInfo(current);
			if (!drained)
				::Sleep(1);
		}
		passed &= expect(drained, "rejected render must finish before the next live-worker scenario");

		// A changed identity must still enqueue a replacement when the old
		// running request was CurrentPageRender and the new one is visible-only.
		thread->ResetSchedulerMetrics();
		thread->AddJob(runningPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->ResumeJobs();
		running = false;
		for (int attempt = 0; attempt < 5000 && !running; ++attempt)
		{
			running = thread->GetCurrentJobInfo(current) && current.nPage == runningPage &&
				current.type == CRenderThread::RENDER;
			if (!running)
				::Sleep(1);
		}
		thread->PauseJobs();
		thread->AddJob(runningPage, 1, CSize(800, 1000), displaySettings,
			CDjVuView::Color, CRenderThread::VisibleRender);
		thread->GetQueuedJobInfo(jobs);
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(running && jobs.size() == 1 && jobs[0].nPage == runningPage &&
			jobs[0].priority == CRenderThread::VisibleRender && metrics.obsoleteJobsRejected > 0,
			"changed current render identity must queue its visible replacement");
		thread->RemoveAllJobs();
		drained = false;
		for (int attempt = 0; attempt < 30000 && !drained; ++attempt)
		{
			drained = !thread->GetCurrentJobInfo(current);
			if (!drained)
				::Sleep(1);
		}
		passed &= expect(drained, "replacement scenario must release the worker before later tests");

		// A stale render can still be physically running when its page leaves the
		// cache window. Cleanup must queue behind it and release the cached image
		// once the rejected render exits the worker.
		const int cleanupPage = runningPage;
		GP<DjVuImage> cleanupImage = source->GetPage(cleanupPage, &observer);
		passed &= expect(cleanupImage != NULL && source->IsPageCached(cleanupPage, &observer),
			"running-cleanup regression page must start cached");
		thread->ResetSchedulerMetrics();
		thread->AddJob(cleanupPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->ResumeJobs();
		running = false;
		for (int attempt = 0; attempt < 5000 && !running; ++attempt)
		{
			running = thread->GetCurrentJobInfo(current) && current.nPage == cleanupPage &&
				current.type == CRenderThread::RENDER;
			if (!running)
				::Sleep(1);
		}
		windows = CRenderThread::JobWindows();
		thread->ReconcileJobs(windows); // Page left every active/cache window.
		thread->PauseJobs();
		thread->AddCleanupJob(cleanupPage);
		thread->ReconcileJobs(windows);
		thread->GetQueuedJobInfo(jobs);
		bool queuedCleanup = false;
		for (size_t job = 0; job < jobs.size(); ++job)
			queuedCleanup |= jobs[job].nPage == cleanupPage && jobs[job].type == CRenderThread::CLEANUP;
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(running && queuedCleanup && metrics.obsoleteJobsRejected > 0,
			"running obsolete render must retain cleanup behind the rejected job");
		thread->ResumeJobs();
		cleanupExecuted = false;
		for (int attempt = 0; attempt < 30000 && !cleanupExecuted; ++attempt)
		{
			thread->GetQueuedJobInfo(jobs);
			queuedCleanup = false;
			for (size_t job = 0; job < jobs.size(); ++job)
				queuedCleanup |= jobs[job].nPage == cleanupPage && jobs[job].type == CRenderThread::CLEANUP;
			bool runningCleanup = thread->GetCurrentJobInfo(current) &&
				current.nPage == cleanupPage && current.type == CRenderThread::CLEANUP;
			cleanupExecuted = !source->IsPageCached(cleanupPage, &observer) &&
				!queuedCleanup && !runningCleanup;
			if (!cleanupExecuted)
				::Sleep(1);
		}
		thread->PauseJobs();
		passed &= expect(cleanupExecuted,
			"cleanup queued behind a stale render must execute RemoveFromCache");

		// If the page re-enters before the worker reaches cleanup, reconciliation
		// cancels maintenance and leaves the observed image cached.
		cleanupImage = source->GetPage(cleanupPage, &observer);
		passed &= expect(cleanupImage != NULL && source->IsPageCached(cleanupPage, &observer),
			"re-entry regression page must be cached before its second render");
		thread->ResetSchedulerMetrics();
		thread->AddJob(cleanupPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->ResumeJobs();
		running = false;
		for (int attempt = 0; attempt < 5000 && !running; ++attempt)
		{
			running = thread->GetCurrentJobInfo(current) && current.nPage == cleanupPage &&
				current.type == CRenderThread::RENDER;
			if (!running)
				::Sleep(1);
		}
		windows = CRenderThread::JobWindows();
		thread->ReconcileJobs(windows);
		thread->PauseJobs();
		thread->AddCleanupJob(cleanupPage);
		windows.renderPages.insert(cleanupPage); // Page re-enters before cleanup runs.
		thread->ReconcileJobs(windows);
		thread->GetQueuedJobInfo(jobs);
		queuedCleanup = false;
		for (size_t job = 0; job < jobs.size(); ++job)
			queuedCleanup |= jobs[job].nPage == cleanupPage && jobs[job].type == CRenderThread::CLEANUP;
		passed &= expect(running && !queuedCleanup,
			"re-entry must cancel cleanup queued behind a running render");
		thread->ResumeJobs();
		drained = false;
		for (int attempt = 0; attempt < 30000 && !drained; ++attempt)
		{
			drained = !thread->GetCurrentJobInfo(current);
			if (!drained)
				::Sleep(1);
		}
		thread->PauseJobs();
		passed &= expect(drained && source->IsPageCached(cleanupPage, &observer),
			"cancelled cleanup must not remove a page that re-entered the cache window");
		thread->RemoveAllJobs();

		// Re-entry with exactly the same identity revives the running render.
		// A CurrentPage upgrade and a following Visible downgrade must not create
		// replacement work or leave the result rejected.
		observer.ResetRender();
		thread->ResetSchedulerMetrics();
		thread->AddJob(cleanupPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::VisibleRender);
		thread->ResumeJobs();
		running = false;
		for (int attempt = 0; attempt < 5000 && !running; ++attempt)
		{
			running = thread->GetCurrentJobInfo(current) && current.nPage == cleanupPage &&
				current.type == CRenderThread::RENDER;
			if (!running)
				::Sleep(1);
		}
		windows = CRenderThread::JobWindows();
		thread->ReconcileJobs(windows);
		thread->PauseJobs();
		thread->AddCleanupJob(cleanupPage);
		windows.renderPages.insert(cleanupPage);
		thread->AddJob(cleanupPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->ReconcileJobs(windows);
		thread->AddJob(cleanupPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::VisibleRender);
		thread->GetQueuedJobInfo(jobs);
		thread->GetSchedulerMetrics(metrics);
		bool hasCleanupOrReplacement = false;
		for (size_t job = 0; job < jobs.size(); ++job)
			hasCleanupOrReplacement |= jobs[job].nPage == cleanupPage;
		passed &= expect(running && !thread->IsCurrentJobRejected() && !hasCleanupOrReplacement &&
			metrics.obsoleteJobsRejected == 1,
			"same-identity re-entry must revive the rejected render without duplicate work");
		thread->ResumeJobs();
		passed &= expect(observer.WaitForRender(cleanupPage, 30000) && observer.GetRenderCount() == 1,
			"revived same-identity render must deliver its bitmap result exactly once");
		thread->PauseJobs();

		// A changed identity cannot revive the old raster. The old render remains
		// rejected, cleanup is cancelled by re-entry, and one replacement wins.
		observer.ResetRender();
		thread->ResetSchedulerMetrics();
		thread->AddJob(cleanupPage, 0, CSize(4000, 4000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->ResumeJobs();
		running = false;
		for (int attempt = 0; attempt < 5000 && !running; ++attempt)
		{
			running = thread->GetCurrentJobInfo(current) && current.nPage == cleanupPage &&
				current.type == CRenderThread::RENDER;
			if (!running)
				::Sleep(1);
		}
		windows = CRenderThread::JobWindows();
		thread->ReconcileJobs(windows);
		thread->PauseJobs();
		thread->AddCleanupJob(cleanupPage);
		CDisplaySettings changedSettings = displaySettings;
		changedSettings.bInvertColors = !changedSettings.bInvertColors;
		windows.renderPages.insert(cleanupPage);
		thread->AddJob(cleanupPage, 1, CSize(800, 1000), changedSettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->ReconcileJobs(windows);
		thread->GetQueuedJobInfo(jobs);
		int replacementCount = 0;
		bool cleanupQueued = false;
		for (size_t job = 0; job < jobs.size(); ++job)
		{
			replacementCount += jobs[job].nPage == cleanupPage && jobs[job].type == CRenderThread::RENDER;
			cleanupQueued |= jobs[job].nPage == cleanupPage && jobs[job].type == CRenderThread::CLEANUP;
		}
		passed &= expect(running && thread->IsCurrentJobRejected() && replacementCount == 1 && !cleanupQueued,
			"changed identity must retain rejection and queue exactly one replacement");
		thread->ResumeJobs();
		passed &= expect(observer.WaitForRender(cleanupPage, 30000) && observer.GetRenderCount() == 1,
			"changed-identity replacement must discard A and deliver only B");
		thread->PauseJobs();
		thread->RemoveAllJobs();
	}

	thread->AddPrefetchJob(adjacent);
	thread->AddPrefetchJob(adjacent);
	passed &= expect(thread->GetQueuedJobCount() == 1 && thread->IsPrefetchQueued(adjacent),
		"duplicate prefetch jobs must coalesce");

	thread->AddDecodeJob(adjacent);
	passed &= expect(thread->GetQueuedJobCount() == 1 && !thread->IsPrefetchQueued(adjacent),
		"visible decode must replace queued prefetch");

	thread->AddPrefetchJob(first);
	thread->AddPrefetchJob(last);
	passed &= expect(thread->GetQueuedJobCount() <= 3, "first/last prefetch must remain bounded");

	thread->RemoveAllJobs();
	passed &= expect(thread->GetQueuedJobCount() == 0, "navigation must discard stale prefetch jobs");

	thread->AddPrefetchJob(adjacent);
	thread->RemoveAllJobs();
	thread->AddDecodeJob(distant);
	passed &= expect(thread->GetQueuedJobCount() == 1 && !thread->IsPrefetchQueued(distant),
		"distant jump must not wait behind speculative work");

	// Let one speculative request actually reach DjVuLibre, then cancel it
	// through the same navigation cleanup used by the view.
	thread->RemoveAllJobs();
	thread->ResumeJobs();
	thread->AddPrefetchJob(adjacent);
	bool active = false;
	for (int attempt = 0; attempt < 5000 && !active; ++attempt)
	{
		active = source->IsPrefetchActive(adjacent);
		if (!active)
			::Sleep(1);
	}
	passed &= expect(active, "prefetch must reach the asynchronous decoder");
	thread->RemoveAllJobs();
	passed &= expect(!source->IsPrefetchActive(adjacent),
		"navigation cleanup must cancel active prefetch");
	passed &= expect(WaitForDecodeStop(source, adjacent, 5000),
		"cancelled prefetch must not keep DjVuLibre decoding");

	// Race the producer against cancellation repeatedly.  Cancellation may land
	// before or after resume_decode(false); both paths must leave no active job.
	for (int attempt = 0; attempt < 32; ++attempt)
	{
		HANDLE started = ::CreateEvent(NULL, TRUE, FALSE, NULL);
		PrefetchRace race = { source, adjacent, started };
		uintptr_t handle = _beginthreadex(NULL, 0, StartPrefetchProc, &race, 0, NULL);
		passed &= expect(handle != 0 && ::WaitForSingleObject(started, 5000) == WAIT_OBJECT_0,
			"prefetch race worker must start");
		for (int cancel = 0; cancel < 8; ++cancel)
		{
			source->CancelPrefetches();
			::Sleep(0);
		}
		if (handle != 0)
		{
			passed &= expect(::WaitForSingleObject((HANDLE)handle, 5000) == WAIT_OBJECT_0,
				"prefetch race worker must finish");
			::CloseHandle((HANDLE)handle);
		}
		::CloseHandle(started);
		source->CancelPrefetches();
		passed &= expect(!source->IsPrefetchActive(adjacent),
			"raced cancellation must clear prefetch registration");
		passed &= expect(WaitForDecodeStop(source, adjacent, 5000),
			"raced cancellation must stop speculative decode");
	}

	observer.Reset();
	thread->AddDecodeJob(adjacent);
	passed &= expect(observer.WaitForDecode(adjacent, 30000),
		"visible decode must complete after prefetch cancellation");

	thread->Stop();
	source->Release();
	DataPool::close_all();
	DjVuSource::SetApplication(NULL);
	puts(passed ? "Render prefetch queue regression: PASS" : "Render prefetch queue regression: FAIL");
	return passed ? 0 : 1;
}

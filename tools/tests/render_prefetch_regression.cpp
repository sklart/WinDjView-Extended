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
		RegressionObserver() : m_event(::CreateEvent(NULL, TRUE, FALSE, NULL)), m_page(-1) { }
		virtual ~RegressionObserver() { ::CloseHandle(m_event); }

		virtual void OnUpdate(const Observable*, const Message* message)
		{
			if (message != NULL && message->code == PAGE_DECODED)
			{
				const PageMsg* page = static_cast<const PageMsg*>(message);
				InterlockedExchange(&m_page, page->nPage);
				::SetEvent(m_event);
			}
		}

		void Reset() { InterlockedExchange(&m_page, -1); ::ResetEvent(m_event); }
		bool WaitForDecode(int page, DWORD timeout)
		{
			return ::WaitForSingleObject(m_event, timeout) == WAIT_OBJECT_0 &&
				InterlockedCompareExchange(&m_page, 0, 0) == page;
		}

	private:
		HANDLE m_event;
		volatile LONG m_page;
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

		// Zoom/rotation-style replacement keeps only the latest identity for the
		// page, and a distant jump drops all obsolete queued foreground work.
		thread->RemoveAllJobs();
		thread->ResetSchedulerMetrics();
		thread->AddJob(0, 0, CSize(800, 1000), displaySettings);
		thread->AddJob(0, 1, CSize(1200, 1500), displaySettings);
		thread->GetQueuedJobInfo(jobs);
		CRenderThread::SchedulerMetrics metrics;
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(jobs.size() == 1 && jobs[0].size == CSize(1200, 1500) &&
			metrics.obsoleteJobsRemoved >= 1,
			"new render identity must replace the obsolete queued render");

		thread->AddJob(1, 0, CSize(800, 1000), displaySettings);
		thread->AddJob(2, 0, CSize(800, 1000), displaySettings);
		set<int> currentWindow;
		currentWindow.insert(2);
		thread->DiscardJobsOutside(currentWindow);
		thread->GetQueuedJobInfo(jobs);
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(jobs.size() == 1 && jobs[0].nPage == 2 && metrics.obsoleteJobsRemoved >= 3,
			"A -> B -> C and distant jump must discard obsolete queued renders");
		thread->RemoveAllJobs();

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
		set<int> jumpedWindow;
		jumpedWindow.insert(targetPage);
		thread->DiscardJobsOutside(jumpedWindow);
		thread->PauseJobs();
		thread->AddJob(targetPage, 0, CSize(800, 1000), displaySettings,
			CDjVuView::Color, CRenderThread::CurrentPageRender);
		thread->GetQueuedJobInfo(jobs);
		thread->GetSchedulerMetrics(metrics);
		passed &= expect(running && !jobs.empty() && jobs[0].nPage == targetPage &&
			jobs[0].priority == CRenderThread::CurrentPageRender && metrics.obsoleteJobsRejected > 0,
			"live jump must reject the stale render and queue the new foreground page next");
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

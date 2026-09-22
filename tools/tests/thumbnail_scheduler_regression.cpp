#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/ThumbnailsThread.h"
#include "../../src/Drawing.h"
#include "../../src/libdjvu/DataPool.h"
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
		RegressionObserver() : m_event(::CreateEvent(NULL, TRUE, FALSE, NULL)), m_page(-1), m_count(0) { }
		virtual ~RegressionObserver() { ::CloseHandle(m_event); }
		virtual void OnUpdate(const Observable*, const Message* message)
		{
			if (message != NULL && message->code == THUMBNAIL_RENDERED)
			{
				const BitmapMsg* bitmap = static_cast<const BitmapMsg*>(message);
				delete bitmap->pDIB;
				InterlockedExchange(&m_page, bitmap->nPage);
				InterlockedIncrement(&m_count);
				::SetEvent(m_event);
			}
		}
		void Reset() { InterlockedExchange(&m_page, -1); InterlockedExchange(&m_count, 0); ::ResetEvent(m_event); }
		bool WaitForPage(int page, DWORD timeout)
		{
			return ::WaitForSingleObject(m_event, timeout) == WAIT_OBJECT_0 && InterlockedCompareExchange(&m_page, 0, 0) == page;
		}
		long Count() { return InterlockedCompareExchange(&m_count, 0, 0); }
	private:
		HANDLE m_event;
		volatile LONG m_page, m_count;
	};

	bool expect(bool condition, const char* description)
	{
		if (!condition) fprintf(stderr, "thumbnail scheduler regression failed: %s\n", description);
		return condition;
	}

	bool WaitForCurrent(CThumbnailsThread* thread, int page, DWORD timeout)
	{
		DWORD start = ::GetTickCount();
		CThumbnailsThread::JobInfo job;
		bool rejected = false;
		while (::GetTickCount() - start < timeout)
		{
			if (thread->GetCurrentJobInfo(job, rejected) && job.nPage == page) return true;
			::Sleep(1);
		}
		return false;
	}

	bool WaitForIdle(CThumbnailsThread* thread, DWORD timeout)
	{
		DWORD start = ::GetTickCount();
		CThumbnailsThread::JobInfo job;
		bool rejected = false;
		while (::GetTickCount() - start < timeout)
		{
			if (!thread->GetCurrentJobInfo(job, rejected) && thread->GetQueuedJobCount() == 0) return true;
			::Sleep(1);
		}
		return false;
	}
}

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0) || argc != 2) return 2;
	RegressionApplication application;
	RegressionObserver observer;
	DjVuSource::SetApplication(&application);
	DjVuSource* source = DjVuSource::FromFile(argv[1]);
	if (source == NULL || source->GetPageCount() == 0) return 1;

	const int page = 0;
	const CDisplaySettings settings;
	bool passed = true;
	CThumbnailsThread* thread = new CThumbnailsThread(source, &observer);
	thread->PauseJobs();

	thread->AddJob(page, 0, CSize(100, 100), settings, CThumbnailsThread::Background);
	thread->AddJob(page, 0, CSize(100, 100), settings, CThumbnailsThread::Background);
	thread->AddJob(page, 0, CSize(120, 120), settings, CThumbnailsThread::Adjacent);
	thread->AddJob(page, 0, CSize(140, 140), settings, CThumbnailsThread::Visible);
	vector<CThumbnailsThread::JobInfo> jobs;
	thread->GetQueuedJobInfo(jobs);
	CThumbnailsThread::Metrics metrics = thread->GetMetrics();
	passed &= expect(jobs.size() == 3 && jobs[0].priority == CThumbnailsThread::Visible && jobs[1].priority == CThumbnailsThread::Adjacent && jobs[2].priority == CThumbnailsThread::Background && metrics.deduplicated == 1,
		"duplicate jobs must coalesce and visible work must lead the queue");
	thread->RemoveAllJobs();
	thread->AddJob(page, 0, CSize(110, 110), settings, CThumbnailsThread::Adjacent);
	thread->AddJob(page, 0, CSize(110, 110), settings, CThumbnailsThread::Visible);
	thread->GetQueuedJobInfo(jobs);
	passed &= expect(jobs.size() == 1 && jobs[0].priority == CThumbnailsThread::Visible,
		"a visible request must promote an identical queued adjacent job");
	thread->RemoveAllJobs();
	thread->AddJob(page, 0, CSize(115, 115), settings, CThumbnailsThread::Background);
	thread->AddJob(page, 0, CSize(115, 115), settings, CThumbnailsThread::Adjacent);
	thread->GetQueuedJobInfo(jobs);
	passed &= expect(jobs.size() == 1 && jobs[0].priority == CThumbnailsThread::Adjacent,
		"an identical background job must promote to adjacent");
	thread->AddJob(page, 0, CSize(115, 115), settings, CThumbnailsThread::Visible);
	thread->GetQueuedJobInfo(jobs);
	passed &= expect(jobs.size() == 1 && jobs[0].priority == CThumbnailsThread::Visible,
		"window transitions must retain only the highest-priority identity");
	thread->RemoveAllJobs();

	set<int> visiblePages, adjacentPages, backgroundPages;
	thread->AddJob(page, 0, CSize(100, 100), settings, CThumbnailsThread::Background);
	thread->AddJob(page, 0, CSize(120, 120), settings, CThumbnailsThread::Adjacent);
	thread->AddJob(page, 0, CSize(140, 140), settings, CThumbnailsThread::Visible);
	visiblePages.insert(page);
	thread->ReconcileJobs(visiblePages, adjacentPages, backgroundPages);
	thread->GetQueuedJobInfo(jobs);
	metrics = thread->GetMetrics();
	passed &= expect(jobs.size() == 1 && jobs[0].priority == CThumbnailsThread::Visible && metrics.obsoleteRemoved == 2,
		"viewport jump must remove stale adjacent/background work");
	thread->RemoveAllJobs();
	thread->AddJob(page, 0, CSize(160, 160), settings, CThumbnailsThread::Adjacent);
	set<int> emptyPages;
	thread->ReconcileJobs(emptyPages, emptyPages, emptyPages);
	thread->AddJob(page, 0, CSize(160, 160), settings, CThumbnailsThread::Adjacent);
	passed &= expect(thread->GetQueuedJobCount() == 1, "a page that re-enters the thumbnail window must be queued again");
	thread->RemoveAllJobs();

	for (int i = 0; i < 256; ++i)
		thread->AddJob(page, 0, CSize(40 + i, 100 + i), settings, CThumbnailsThread::Background);
	metrics = thread->GetMetrics();
	passed &= expect(thread->GetQueuedJobCount() <= 128 && metrics.peakQueueLength <= 128,
		"thumbnail queue must remain bounded under rapid background submissions");
	thread->RemoveAllJobs();

	observer.Reset();
	thread->AddJob(page, 0, CSize(180, 180), settings, CThumbnailsThread::Visible);
	thread->ResumeJobs();
	passed &= expect(observer.WaitForPage(page, 30000) && observer.Count() == 1, "thumbnail bitmap must be delivered exactly once");
	thread->PauseJobs();
	passed &= expect(WaitForIdle(thread, 5000), "successful thumbnail job must leave scheduler state");
	observer.Reset();
	thread->AddJob(page, 0, CSize(180, 180), settings, CThumbnailsThread::Visible);
	thread->ResumeJobs();
	passed &= expect(observer.WaitForPage(page, 30000) && observer.Count() == 1, "a job that becomes current again must be accepted again");
	thread->PauseJobs();

	// The identical current request is promoted in place: Background -> Visible
	// must not enqueue a second render and its result remains publishable.
	thread->RemoveAllJobs();
	observer.Reset();
	thread->AddJob(page, 0, CSize(6000, 6000), settings, CThumbnailsThread::Background);
	thread->ResumeJobs();
	bool running = WaitForCurrent(thread, page, 5000);
	thread->AddJob(page, 0, CSize(6000, 6000), settings, CThumbnailsThread::Visible);
	CThumbnailsThread::JobInfo current;
	bool rejected = false;
	bool promotedCurrent = thread->GetCurrentJobInfo(current, rejected) &&
		current.priority == CThumbnailsThread::Visible && !rejected && thread->GetQueuedJobCount() == 0;
	passed &= expect(running && promotedCurrent, "running background thumbnail must promote to visible without a duplicate");
	passed &= expect(observer.WaitForPage(page, 30000) && observer.Count() == 1,
		"promoted current thumbnail must publish exactly once");
	thread->PauseJobs();

	// A stale current request is revived by the same identity before Render()
	// completes. It must clear rejection rather than schedule replacement work.
	thread->RemoveAllJobs();
	observer.Reset();
	thread->AddJob(page, 0, CSize(6000, 6000), settings, CThumbnailsThread::Visible);
	thread->ResumeJobs();
	running = WaitForCurrent(thread, page, 5000);
	thread->ReconcileJobs(emptyPages, emptyPages, emptyPages);
	bool staleRejected = thread->GetCurrentJobInfo(current, rejected) && rejected;
	metrics = thread->GetMetrics();
	passed &= expect(running && staleRejected && metrics.obsoleteRejected != 0,
		"running stale thumbnail must be rejected before publication");
	thread->AddJob(page, 0, CSize(6000, 6000), settings, CThumbnailsThread::Visible);
	bool revived = thread->GetCurrentJobInfo(current, rejected) && !rejected &&
		current.priority == CThumbnailsThread::Visible && thread->GetQueuedJobCount() == 0;
	passed &= expect(revived, "matching re-entry must revive rejected current thumbnail without a duplicate");
	passed &= expect(observer.WaitForPage(page, 30000) && observer.Count() == 1,
		"revived thumbnail must publish exactly once");
	thread->PauseJobs();

	// A request that does not re-enter stays rejected and cannot publish.
	thread->RemoveAllJobs();
	observer.Reset();
	thread->AddJob(page, 0, CSize(6000, 6000), settings, CThumbnailsThread::Visible);
	thread->ResumeJobs();
	running = WaitForCurrent(thread, page, 5000);
	thread->ReconcileJobs(emptyPages, emptyPages, emptyPages);
	staleRejected = thread->GetCurrentJobInfo(current, rejected) && rejected;
	passed &= expect(running && staleRejected, "non-returning stale thumbnail must remain rejected");
	passed &= expect(WaitForIdle(thread, 30000) && observer.Count() == 0,
		"stale running thumbnail must complete without publishing a bitmap");

	thread->PauseJobs();
	thread->AddJob(page, 0, CSize(200, 200), settings, CThumbnailsThread::Visible);
	thread->Stop();
	source->Release();
	DataPool::close_all();
	DjVuSource::SetApplication(NULL);
	puts(passed ? "Thumbnail scheduler regression: PASS" : "Thumbnail scheduler regression: FAIL");
	return passed ? 0 : 1;
}

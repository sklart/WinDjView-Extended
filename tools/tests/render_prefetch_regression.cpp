#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
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

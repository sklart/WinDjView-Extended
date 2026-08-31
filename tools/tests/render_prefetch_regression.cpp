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
		virtual void OnUpdate(const Observable*, const Message*) { }
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

	thread->Stop();
	source->Release();
	DataPool::close_all();
	DjVuSource::SetApplication(NULL);
	puts(passed ? "Render prefetch queue regression: PASS" : "Render prefetch queue regression: FAIL");
	return passed ? 0 : 1;
}

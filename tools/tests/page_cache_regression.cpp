// Production cache-update regression and informational benchmark.
// It invokes CDjVuView::UpdatePagesCache* directly with a paused production
// CRenderThread.  The DjVuSource page table is expanded synthetically, so no
// decoder work is needed while the actual cache-selection code is exercised.
#include "../../src/stdafx.h"
#define protected public
#include "../../src/DjVuView.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
#undef protected

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

class CacheView : public CDjVuView
{
public:
	CacheView() : CDjVuView() { }
};

bool Expect(bool condition, const char* text)
{
	if (!condition)
		fprintf(stderr, "page-cache regression failed: %s\n", text);
	return condition;
}

struct CacheHarness
{
	DjVuSource* source;
	CacheView view;
	CRenderThread* thread;

	CacheHarness(const CString& fixture) : source(DjVuSource::FromFile(fixture)), thread(NULL)
	{
		// The paused thread only records production jobs. Its source page table
		// can safely be synthetic because no queued job is allowed to execute.
		source->m_nPageCount = 4096;
		source->m_pages.resize(4096);
		for (int i = 0; i < 4096; ++i)
			source->m_pages[i].info.bDecoded = true;
		view.m_pSource = source;
		thread = new CRenderThread(source, &view);
		thread->PauseJobs();
		view.m_pRenderThread = thread;
	}

	~CacheHarness()
	{
		ClearObserved();
		view.m_pRenderThread = NULL;
		thread->Stop(); // The worker releases itself after draining its stop event.
		view.m_pSource = NULL;
		source->Release();
	}

	void ClearObserved()
	{
		vector<int> add, remove;
		for (set<int>::const_iterator it = view.m_observedPages.begin(); it != view.m_observedPages.end(); ++it)
			remove.push_back(*it);
		if (!remove.empty())
			source->ChangeObservedPages(&view, add, remove);
		view.m_observedPages.clear();
	}

	void Configure(int count, int layout, int page, int scrollY, int viewportHeight)
	{
		ClearObserved();
		thread->RemoveAllJobs();
		source->m_nPageCount = count;
		view.m_nPageCount = count;
		view.m_nLayout = layout;
		view.m_nPage = page;
		view.m_bUpdateBitmaps = true;
		view.m_ptScrollPos = CPoint(0, scrollY);
		view.m_szViewport = CSize(800, viewportHeight);
		view.m_pages.assign(count, CDjVuView::Page());
		for (int i = 0; i < count; ++i)
		{
			CDjVuView::Page& pageData = view.m_pages[i];
			pageData.info.bDecoded = true;
			pageData.info.szPage = CSize(800, 1000);
			pageData.info.nDPI = 300;
			pageData.szBitmap = CSize(800, 1000);
			pageData.ptOffset = CPoint(0, i*1000);
			pageData.rcDisplay = CRect(0, i*1000, 800, (i + 1)*1000);
		}
	}

	void ApplyObservation(const vector<int>& add, const vector<int>& remove)
	{
		for (size_t i = 0; i < remove.size(); ++i)
			view.m_observedPages.erase(remove[i]);
		for (size_t i = 0; i < add.size(); ++i)
			view.m_observedPages.insert(add[i]);
		source->ChangeObservedPages(&view, add, remove);
	}

	void Update(vector<int>& add, vector<int>& remove, bool clearJobs = true)
	{
		add.clear();
		remove.clear();
		thread->ResetSubmittedJobCounts();
		if (clearJobs)
			thread->RemoveAllJobs();
		if (view.m_nLayout == CDjVuView::SinglePage)
			view.UpdatePagesCacheSingle(true, add, remove);
		else if (view.m_nLayout == CDjVuView::Facing)
			view.UpdatePagesCacheFacing(true, add, remove);
		else
			view.UpdatePagesCacheContinuous(true, add, remove);
		view.ScheduleAdjacentPrefetch(add, remove);
	}
};

bool Contains(const vector<int>& pages, int page)
{
	return find(pages.begin(), pages.end(), page) != pages.end();
}

bool HasLegacyWorkingWindow(const CacheView& view, const vector<int>& add)
{
	const int scrollTop = view.GetScrollPosition().y;
	const int viewportHeight = view.GetViewportSize().cy;
	for (int page = 0; page < view.m_nPageCount; ++page)
	{
		bool expected;
		if (view.m_nLayout == CDjVuView::SinglePage || view.m_nLayout == CDjVuView::Facing)
			expected = abs(page - view.m_nPage) <= 10 || page == 0 || page == view.m_nPageCount - 1;
		else
			expected = (view.m_pages[page].rcDisplay.top < scrollTop + 11*viewportHeight &&
				view.m_pages[page].rcDisplay.bottom > scrollTop - 10*viewportHeight) ||
				page == 0 || page == view.m_nPageCount - 1;
		if (expected && !Contains(add, page))
			return false;
	}
	return true;
}

bool RunRegression(CacheHarness& harness)
{
	bool passed = true;
	const int layouts[] = { CDjVuView::SinglePage, CDjVuView::Facing,
		CDjVuView::Continuous, CDjVuView::ContinuousFacing };
	const int counts[] = { 1, 2, 500, 4096 };

	for (int layoutIndex = 0; layoutIndex < 4; ++layoutIndex)
	{
		for (int countIndex = 0; countIndex < 4; ++countIndex)
		{
			const int count = counts[countIndex];
			const int page = count == 1 ? 0 : count / 2;
			harness.Configure(count, layouts[layoutIndex], page, page*1000, 900);
			vector<int> add, remove;
			harness.Update(add, remove);
			passed &= Expect(!add.empty(), "working cache window must observe pages");
			passed &= Expect((int)(add.size() + remove.size()) < min(count + 3, 64),
				"ordinary update must not scan the entire document");
			passed &= Expect(Contains(add, page), "current page must be observed");
			passed &= Expect(HasLegacyWorkingWindow(harness.view, add),
				"cache selection must retain every legacy render/decode-window page");
			harness.ApplyObservation(add, remove);

			// A resize and a sequential scroll keep only a bounded current window.
			harness.Configure(count, layouts[layoutIndex], page, page*1000 + 500, 1300);
			harness.Update(add, remove);
			passed &= Expect((int)(add.size() + remove.size()) < min(count + 3, 64),
				"resize/scroll update must remain bounded");
		}
	}

	// A distant jump must explicitly release the former observed window.
	harness.Configure(4096, CDjVuView::Continuous, 8, 8000, 900);
	vector<int> add, remove;
	harness.Update(add, remove, false);
	harness.ApplyObservation(add, remove);
	passed &= Expect(harness.view.m_observedPages.find(8) != harness.view.m_observedPages.end(), "initial page must be observed");
	harness.view.m_nPage = 3500;
	harness.view.m_ptScrollPos.y = 3500000;
	harness.Update(add, remove);
	passed &= Expect(Contains(remove, 8), "distant jump must release old observed page");
	harness.ApplyObservation(add, remove);
	passed &= Expect(!Contains(vector<int>(harness.view.m_observedPages.begin(), harness.view.m_observedPages.end()), 8),
		"old observed page must be removed from local tracking");

	// Repeating the same production update must replace, not duplicate, jobs.
	harness.Configure(500, CDjVuView::SinglePage, 250, 0, 900);
	harness.Update(add, remove);
	int render1, decode1, prefetch1;
	harness.thread->GetQueuedJobCounts(render1, decode1, prefetch1);
	harness.Update(add, remove, false);
	int render2, decode2, prefetch2;
	harness.thread->GetQueuedJobCounts(render2, decode2, prefetch2);
	passed &= Expect(render1 == render2 && decode1 == decode2 && prefetch1 == prefetch2,
		"repeated update must not duplicate render/decode/prefetch jobs");
	return passed;
}

void RunBenchmark(CacheHarness& harness)
{
	const int updates = 100;
	int totalEntries = 0, totalRender = 0, totalDecode = 0, totalPrefetch = 0;
	LARGE_INTEGER frequency, begin, end;
	QueryPerformanceFrequency(&frequency);
	harness.Configure(4096, CDjVuView::ContinuousFacing, 0, 0, 900);
	QueryPerformanceCounter(&begin);
	for (int i = 0; i < updates; ++i)
	{
		const int page = i; // 100 consecutive viewport updates
		harness.view.m_nPage = page;
		harness.view.m_ptScrollPos.y = page*1000;
		harness.view.m_szViewport.cy = (i & 1) ? 900 : 1300;
		vector<int> add, remove;
		harness.Update(add, remove);
		totalEntries += (int)(add.size() + remove.size());
		int render, decode, prefetch;
		harness.thread->GetSubmittedJobCounts(render, decode, prefetch);
		totalRender += render; totalDecode += decode; totalPrefetch += prefetch;
		harness.ApplyObservation(add, remove);
	}
	for (int i = 0; i < updates; ++i)
	{
		const int page = (i * 37) % 4096; // fast scroll
		harness.view.m_nPage = page;
		harness.view.m_ptScrollPos.y = page*1000;
		harness.view.m_szViewport.cy = 900;
		vector<int> add, remove;
		harness.Update(add, remove);
		totalEntries += (int)(add.size() + remove.size());
		int render, decode, prefetch;
		harness.thread->GetSubmittedJobCounts(render, decode, prefetch);
		totalRender += render; totalDecode += decode; totalPrefetch += prefetch;
		harness.ApplyObservation(add, remove);
	}
	for (int i = 0; i < updates; ++i)
	{
		const int page = (i * 977) % 4096; // deterministic random distant jumps
		harness.view.m_nLayout = CDjVuView::Continuous;
		harness.view.m_nPage = page;
		harness.view.m_ptScrollPos.y = page*1000;
		harness.view.m_szViewport.cy = 900;
		vector<int> add, remove;
		harness.Update(add, remove);
		totalEntries += (int)(add.size() + remove.size());
		int render, decode, prefetch;
		harness.thread->GetSubmittedJobCounts(render, decode, prefetch);
		totalRender += render; totalDecode += decode; totalPrefetch += prefetch;
		harness.ApplyObservation(add, remove);
	}
	QueryPerformanceCounter(&end);
	const double elapsed = 1000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart;
	printf("PAGE_CACHE_BENCHMARK updates=%d elapsed_ms=%.3f processed_entries=%d render_jobs=%d decode_jobs=%d prefetch_jobs=%d\n",
		updates * 3, elapsed, totalEntries, totalRender, totalDecode, totalPrefetch);
}
}

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0) || argc < 2 || argc > 3)
		return 2;
	const bool benchmarkOnly = argc == 3 && _tcscmp(argv[2], _T("--benchmark")) == 0;
	RegressionApplication application;
	DjVuSource::SetApplication(&application);
	CacheHarness harness(argv[1]);
	if (harness.source == NULL)
		return 1;
	if (benchmarkOnly)
		RunBenchmark(harness);
	else if (!RunRegression(harness))
		return 1;
	DjVuSource::SetApplication(NULL);
	puts(benchmarkOnly ? "PAGE_CACHE_BENCHMARK_RESULT: PASS" : "Page cache production regression: PASS");
	return 0;
}

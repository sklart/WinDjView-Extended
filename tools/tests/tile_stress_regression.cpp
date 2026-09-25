// Long-running production-worker tile lifecycle regression. No UI is required.
#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
#include "../../src/Drawing.h"
#include <psapi.h>
#include <stdio.h>

namespace
{
	class StressApplication : public IApplication
	{
	public:
		virtual bool LoadDocSettings(const CString&, DocSettings*) { return false; }
		virtual bool GetCropPages() { return false; }
		virtual DictionaryInfo* GetDictionaryInfo(const CString&, bool) { return NULL; }
		virtual void ReportFatalError() { }
	};

	bool Check(bool ok, const char* reason)
	{
		if (!ok) fprintf(stderr, "tile stress regression failed: %s\n", reason);
		return ok;
	}

	class ResultObserver : public Observer
	{
	public:
		ResultObserver() : event(::CreateEvent(NULL, TRUE, FALSE, NULL)), publications(0), stale(0) {}
		~ResultObserver() { Reset(); ::CloseHandle(event); }
		void Expect(const RenderRequest& request)
		{
			lock.Lock();
			ResetLocked();
			expected = request;
			::ResetEvent(event);
			lock.Unlock();
		}
		virtual void OnUpdate(const Observable*, const Message* message)
		{
			if (message == NULL || message->code != PAGE_RENDERED) return;
			const BitmapMsg* result = static_cast<const BitmapMsg*>(message);
			lock.Lock();
			const RenderRequest* identity = static_cast<const RenderRequest*>(result->pIdentity);
			if (identity == NULL || *identity != expected) ++stale;
			++publications;
			delete bitmap;
			bitmap = result->pDIB;
			::SetEvent(event);
			lock.Unlock();
		}
		bool ExactlyOne()
		{
			lock.Lock();
			const bool ok = publications == 1 && stale == 0 && bitmap != NULL &&
				bitmap->IsValid() && bitmap->GetSize() == expected.size;
			lock.Unlock();
			return ok;
		}
		void Reset() { lock.Lock(); ResetLocked(); lock.Unlock(); }
		HANDLE event;
	private:
		void ResetLocked()
		{
			delete bitmap;
			bitmap = NULL;
			publications = stale = 0;
		}
		CCriticalSection lock;
		RenderRequest expected;
		CDIB* bitmap = NULL;
		int publications, stale;
	};

	RenderRequest Request(int page, int size = 3072, int rotation = 0,
		int mode = CDjVuView::BlackAndWhite, bool crop = false, bool adjust = false)
	{
		CDisplaySettings settings;
		settings.bCropPages = crop;
		settings.bAdjustDisplay = adjust;
		if (adjust) settings.nBrightness = 12;
		return RenderRequest(page, CSize(size, size), rotation, mode, settings);
	}

	bool WaitForActive(CRenderThread* worker, int minimum)
	{
		for (int i = 0; i < 5000; ++i)
		{
			CRenderThread::TileWorkerMetrics metrics;
			worker->GetTileWorkerMetrics(metrics);
			if (metrics.activeTileWorkers >= minimum) return true;
			::Sleep(1);
		}
		return false;
	}

	bool Drain(CRenderThread* worker, ResultObserver& observer)
	{
		if (::WaitForSingleObject(observer.event, 60000) != WAIT_OBJECT_0) return false;
		for (int i = 0; i < 30000; ++i)
		{
			CRenderThread::TileWorkerMetrics metrics;
			CRenderThread::JobInfo current;
			worker->GetTileWorkerMetrics(metrics);
			if (metrics.activeTileWorkers == 0 && metrics.liveBatches == 0 &&
				worker->GetQueuedJobCount() == 0 && !worker->GetCurrentJobInfo(current))
			{
				::Sleep(10); // allow a callback already in flight to finish
				return observer.ExactlyOne() && metrics.batchesCreated == metrics.batchesDestroyed &&
					metrics.tileFallbacks <= metrics.batchesCreated &&
					metrics.peakActiveTileWorkers <= metrics.configuredTileWorkers;
			}
			::Sleep(1);
		}
		return false;
	}

	bool Single(CRenderThread* worker, ResultObserver& observer, const RenderRequest& request)
	{
		CRenderThread::TileWorkerMetrics before, after;
		worker->GetTileWorkerMetrics(before);
		observer.Expect(request);
		worker->AddViewportJob(request, CRenderThread::CurrentPageRender);
		CRenderThread::JobWindows windows;
		windows.renderPages.insert(request.page);
		worker->ReconcileJobs(windows);
		const bool drained = Drain(worker, observer);
		worker->GetTileWorkerMetrics(after);
		return drained && after.tileFallbacks - before.tileFallbacks <= 1;
	}

	bool Burst(CRenderThread* worker, ResultObserver& observer)
	{
		const RenderRequest a = Request(0);
		const RenderRequest b = Request(1);
		const RenderRequest c = Request(0, 3584, 1, CDjVuView::BlackAndWhite, true, true);
		HANDLE gate = ::CreateEvent(NULL, TRUE, FALSE, NULL);
		if (gate == NULL) return false;
		observer.Expect(a);
		worker->SetTileStartGateForRegression(gate);
		worker->AddViewportJob(a, CRenderThread::CurrentPageRender);
		bool ok = WaitForActive(worker, 2);
		if (ok)
		{
			const RenderRequest changes[] = { b, a, c, a };
			for (int n = 0; n < 4; ++n)
			{
				worker->AddViewportJob(changes[n], CRenderThread::CurrentPageRender);
				CRenderThread::JobWindows windows;
				windows.renderPages.insert(changes[n].page);
				worker->ReconcileJobs(windows);
			}
		}
		::SetEvent(gate);
		worker->SetTileStartGateForRegression(NULL);
		const bool drained = ok && Drain(worker, observer);
		::CloseHandle(gate);
		if (!drained) return false;
		CRenderThread::TileWorkerMetrics metrics;
		worker->GetTileWorkerMetrics(metrics);
		return metrics.rejectedTileResults > 0;
	}

	size_t PrivateBytes()
	{
		PROCESS_MEMORY_COUNTERS_EX memory = {};
		return ::GetProcessMemoryInfo(::GetCurrentProcess(),
			reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ?
			memory.PrivateUsage : 0;
	}

	bool StopAndWait(CRenderThread* worker)
	{
		HANDLE finished = worker->DuplicateThreadHandleForRegression();
		if (finished == NULL) return false;
		worker->Stop();
		const bool ok = ::WaitForSingleObject(finished, 30000) == WAIT_OBJECT_0;
		::CloseHandle(finished);
		return ok;
	}
}

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0) || argc != 2) return 2;
	StressApplication application;
	DjVuSource::SetApplication(&application);
	const CString path(argv[1]);
	size_t peak = 0, steady = 0, warm = 0;
	DWORD handlesBefore = 0, handlesAfter = 0, steadyHandles = 0;
	::GetProcessHandleCount(::GetCurrentProcess(), &handlesBefore);
	bool ok = true;
	for (int reopen = 0; reopen < 3 && ok; ++reopen)
	{
		DjVuSource* source = DjVuSource::FromFile(path);
		if (!Check(source != NULL && source->GetPageCount() >= 2, "open two-page source")) return 1;
		ResultObserver observer;
		CRenderThread* worker = new CRenderThread(source, &observer, 4);
		HANDLE closeGate = NULL;
		CRenderThread::TileWorkerMetrics metrics;
		worker->GetTileWorkerMetrics(metrics);
		ok &= Check(metrics.configuredTileWorkers >= 2 && metrics.configuredTileWorkers <= 4,
			"bounded worker configuration");
		for (int cycle = 0; cycle < 4 && ok; ++cycle)
		{
			ok &= Check(Burst(worker, observer), "A-B-A-C-A active-tile reconciliation");
			const RenderRequest requests[] = {
				Request(1), Request(0, 3584), Request(1, 3072, 1),
				Request(0, 3072, 0, CDjVuView::BlackAndWhite, true),
				Request(1, 3072, 0, CDjVuView::Foreground),
				Request(0, 3072, 0, CDjVuView::BlackAndWhite, false, true),
				Request(1, 2560), Request(0) };
			for (size_t n = 0; n < sizeof(requests)/sizeof(requests[0]) && ok; ++n)
				ok &= Check(Single(worker, observer, requests[n]),
					"scroll/zoom/rotation/crop/display-mode publication and drain");
			observer.Reset();
			steady = PrivateBytes();
			peak = max(peak, steady);
			if (reopen == 0 && cycle == 0) warm = steady;
		}
		worker->GetTileWorkerMetrics(metrics);
		printf("reopen=%d workers=%d peak_workers=%d tiles=%d rejected=%d fallbacks=%d batches=%d/%d live=%d private_bytes=%llu\n",
			reopen, metrics.configuredTileWorkers, metrics.peakActiveTileWorkers,
			metrics.completedTileJobs, metrics.rejectedTileResults, metrics.tileFallbacks,
			metrics.batchesCreated, metrics.batchesDestroyed, metrics.liveBatches,
			static_cast<unsigned long long>(steady));
		ok &= Check(metrics.activeTileWorkers == 0 && metrics.liveBatches == 0 &&
			metrics.batchesCreated == metrics.batchesDestroyed && worker->GetQueuedJobCount() == 0,
			"no outstanding jobs or batches after long cycle");
		if (ok && reopen == 1)
		{
			// Close while real workers are active, then open a fresh document.
			closeGate = ::CreateEvent(NULL, TRUE, FALSE, NULL);
			worker->SetTileStartGateForRegression(closeGate);
			worker->AddViewportJob(Request(0), CRenderThread::CurrentPageRender);
			ok &= Check(WaitForActive(worker, 2), "active tiles before document close");
		}
		ok &= Check(StopAndWait(worker), "worker and handles terminate on close");
		if (closeGate != NULL) ::CloseHandle(closeGate);
		source->Release();
		DWORD cycleHandles = 0;
		::GetProcessHandleCount(::GetCurrentProcess(), &cycleHandles);
		printf("reopen=%d handles_after_close=%lu\n", reopen, cycleHandles);
		if (reopen == 0) steadyHandles = cycleHandles;
		else ok &= Check(cycleHandles <= steadyHandles + 2,
			"thread handles do not accumulate after first document close");
	}
	printf("private_bytes_warm=%llu private_bytes_peak=%llu private_bytes_steady=%llu\n",
		static_cast<unsigned long long>(warm), static_cast<unsigned long long>(peak),
		static_cast<unsigned long long>(steady));
	::GetProcessHandleCount(::GetCurrentProcess(), &handlesAfter);
	printf("handles_before=%lu handles_after=%lu\n", handlesBefore, handlesAfter);
	ok &= Check(handlesAfter <= steadyHandles + 2,
		"thread handles return to first-close steady state");
	ok &= Check(steady <= warm + 64ULL * 1024 * 1024,
		"private memory does not grow unbounded after warm-up");
	printf("Tile stress regression: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}

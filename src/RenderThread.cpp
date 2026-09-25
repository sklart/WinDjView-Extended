//	WinDjView
//	Copyright (C) 2004-2012 Andrew Zhezherun
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program; if not, write to the Free Software Foundation, Inc.,
//	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//	http://www.gnu.org/copyleft/gpl.html

#include "stdafx.h"
#include "WinDjView.h"

#include "RenderThread.h"
#include "Drawing.h"
#include "DjVuDoc.h"
#include "DjVuView.h"
#include "Scaling.h"
#include "TileRequest.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CRenderThread class

CRenderThread::CRenderThread(DjVuSource* pSource, Observer* pOwner,
	int tileWorkersForBenchmark)
	: m_stop(FALSE, TRUE), m_pOwner(pOwner), m_pSource(pSource), m_nPaused(0),
	  m_scheduler(pSource->GetPageCount()), m_nTileRegionRenders(0), m_nTileFallbacks(0),
	  m_nTileWorkerLimit(2), m_nActiveTileWorkers(0),
	  m_nextTileBatchGeneration(1), m_tileStartGateForRegression(NULL)
{
	m_pSource->AddRef();
	SYSTEM_INFO systemInfo;
	::GetSystemInfo(&systemInfo);
	if (systemInfo.dwNumberOfProcessors > 2)
		m_nTileWorkerLimit = static_cast<int>(min(4u, systemInfo.dwNumberOfProcessors));
	if (tileWorkersForBenchmark == 1 || tileWorkersForBenchmark == 2 ||
		tileWorkersForBenchmark == 4)
		m_nTileWorkerLimit = tileWorkersForBenchmark;
	// The primary worker also renders tiles. Auxiliary workers never take
	// non-tile jobs, and all of them share the same scheduler under m_lock.
	for (int i = 1; i < m_nTileWorkerLimit; ++i)
	{
		UINT tileThreadId;
		HANDLE tileThread = (HANDLE)_beginthreadex(NULL, 0, TileThreadProc, this, 0, &tileThreadId);
		if (tileThread == NULL) break;
		m_tileThreads.push_back(tileThread);
		::SetThreadPriority(tileThread, THREAD_PRIORITY_BELOW_NORMAL);
		theApp.ThreadStarted();
	}
	m_nTileWorkerLimit = 1 + static_cast<int>(m_tileThreads.size());

	UINT dwThreadId;
	m_hThread = (HANDLE)_beginthreadex(NULL, 0, RenderThreadProc, this, 0, &dwThreadId);
	::SetThreadPriority(m_hThread, THREAD_PRIORITY_BELOW_NORMAL);
	theApp.ThreadStarted();
}

CRenderThread::TileBatch::TileBatch(const RenderRequest& request_, unsigned long long generation_)
	: request(request_), generation(generation_), grid(request_.size.cx, request_.size.cy),
	  completion(grid), assembled(NULL), fallbackStarted(false) {}

CRenderThread::TileBatch::~TileBatch()
{
	delete assembled;
}

void CRenderThread::ClearTileBatches()
{
	for (map<int, TileBatch*>::iterator it = m_tileBatches.begin(); it != m_tileBatches.end(); ++it)
	{
		delete it->second;
		++m_tileWorkerMetrics.batchesDestroyed;
	}
	m_tileBatches.clear();
}

void CRenderThread::DropTileBatch(int nPage)
{
	map<int, TileBatch*>::iterator it = m_tileBatches.find(nPage);
	if (it != m_tileBatches.end())
	{
		delete it->second;
		m_tileBatches.erase(it);
		++m_tileWorkerMetrics.batchesDestroyed;
	}
}

CRenderThread::~CRenderThread()
{
	ClearTileBatches();
	for (size_t i = 0; i < m_tileThreads.size(); ++i)
		::CloseHandle(m_tileThreads[i]);
	::CloseHandle(m_hThread);
	m_pSource->Release();
	theApp.ThreadTerminated();
}

void CRenderThread::Stop()
{
	m_stopping.Lock();

	m_stop.SetEvent();

	m_lock.Lock();
	m_scheduler.Clear();
	m_scheduler.RejectParallelTiles();
	ClearTileBatches();
	m_jobReady.ResetEvent();
	m_scheduler.RejectCurrent();
	m_tileReady.SetEvent();
	PauseJobs();
	m_lock.Unlock();
	m_pSource->CancelPrefetches();

	m_stopping.Unlock();
}

unsigned int __stdcall CRenderThread::RenderThreadProc(void* pvData)
{
	::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	CRenderThread* pThread = reinterpret_cast<CRenderThread*>(pvData);

	HANDLE hEvents[] = { pThread->m_jobReady.m_hObject, pThread->m_stop.m_hObject };
	while (::WaitForMultipleObjects(2, hEvents, false, INFINITE) == WAIT_OBJECT_0)
	{
		pThread->m_lock.Lock();

		if (!pThread->m_scheduler.CanTakeNext())
		{
			pThread->m_lock.Unlock();
			continue;
		}

		RenderScheduler::Job job;
		pThread->m_scheduler.TakeNext(job);
		HANDLE tileGate = job.type == TILE_RENDER ? pThread->m_tileStartGateForRegression : NULL;
		if (job.type == TILE_RENDER)
		{
			++pThread->m_nActiveTileWorkers;
			pThread->m_tileWorkerMetrics.activeTileWorkers = pThread->m_nActiveTileWorkers;
			pThread->m_tileWorkerMetrics.peakActiveTileWorkers = max(
				pThread->m_tileWorkerMetrics.peakActiveTileWorkers, pThread->m_nActiveTileWorkers);
		}
		pThread->SignalJobs();
		pThread->m_lock.Unlock();

		CDIB* pBitmap = NULL;
		bool tileFallback = false;

		switch (job.type)
		{
		case RENDER:
			pBitmap = pThread->Render(job);
			break;

		case TILE_RENDER:
			if (tileGate != NULL)
			{
				HANDLE gateEvents[] = { pThread->m_stop.m_hObject, tileGate };
				if (::WaitForMultipleObjects(2, gateEvents, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
					break;
			}
			pBitmap = pThread->RenderTileJob(job, tileFallback);
			break;

		case DECODE:
			pThread->m_pSource->GetPage(job.GetPage(), pThread->m_pOwner);
			break;

		case PREFETCH_DECODE:
			pThread->m_pSource->StartPrefetch(job.GetPage());
			break;

		case READINFO:
			pThread->m_pSource->GetPageInfo(job.GetPage());
			break;

		case CLEANUP:
			pThread->m_pSource->RemoveFromCache(job.GetPage(), pThread->m_pOwner);
			break;
		}

		if (job.type == TILE_RENDER)
		{
			pThread->FinishTile(job, 0, pBitmap, tileFallback);
			continue;
		}

		// Cannot stop while the owner is being updated
		pThread->m_stopping.Lock();
		pThread->m_lock.Lock();
		bool bNotify = pThread->m_scheduler.CompleteCurrent(::GetTickCount());
		pThread->SignalJobs();
		pThread->m_lock.Unlock();

		if (bNotify)
		{
			switch (job.type)
			{
			case RENDER:
				{
					RenderResult result(job.request, pBitmap);
					pThread->m_pOwner->OnUpdate(NULL, &BitmapMsg(PAGE_RENDERED, job.GetPage(),
						result.bitmap, &result.request));
				}
				break;
			case DECODE:
			case READINFO:
				pThread->m_pOwner->OnUpdate(NULL, &PageMsg(PAGE_DECODED, job.GetPage()));
				break;
			}
		}
		else
		{
			delete pBitmap;
		}

		pThread->m_stopping.Unlock();
	}

	::CoUninitialize();

	// Ensure that a call to Stop() is finished
	pThread->m_stopping.Lock();
	pThread->m_stopping.Unlock();
	if (!pThread->m_tileThreads.empty())
		::WaitForMultipleObjects(static_cast<DWORD>(pThread->m_tileThreads.size()),
			&pThread->m_tileThreads[0], TRUE, INFINITE);

	// Clean page cache
	for (int nPage = 0; nPage < pThread->m_pSource->GetPageCount(); ++nPage)
		pThread->m_pSource->RemoveFromCache(nPage, pThread->m_pOwner);

	delete pThread;

	return 0;
}

void CRenderThread::PauseJobs()
{
	InterlockedExchange(&m_nPaused, 1);
}

void CRenderThread::ResumeJobs()
{
	m_lock.Lock();

	InterlockedExchange(&m_nPaused, 0);
	SignalJobs();

	m_lock.Unlock();
}

bool CRenderThread::IsPaused()
{
	return (InterlockedExchangeAdd(&m_nPaused, 0) == 1);
}

int CRenderThread::GetQueuedJobCount()
{
	m_lock.Lock();
	int count = m_scheduler.GetQueuedJobCount();
	m_lock.Unlock();
	return count;
}

bool CRenderThread::IsPrefetchQueued(int nPage)
{
	m_lock.Lock();
	bool queued = m_scheduler.IsPrefetchQueued(nPage);
	m_lock.Unlock();
	return queued;
}

void CRenderThread::GetQueuedJobCounts(int& render, int& decode, int& prefetchDecode)
{
	m_lock.Lock();
	m_scheduler.GetQueuedJobCounts(render, decode, prefetchDecode);
	m_lock.Unlock();
}

void CRenderThread::ResetSubmittedJobCounts()
{
	m_lock.Lock();
	m_scheduler.ResetSubmittedJobCounts();
	m_lock.Unlock();
}

void CRenderThread::GetSubmittedJobCounts(int& render, int& decode, int& prefetchDecode)
{
	m_lock.Lock();
	m_scheduler.GetSubmittedJobCounts(render, decode, prefetchDecode);
	m_lock.Unlock();
}

void CRenderThread::GetQueuedJobInfo(vector<JobInfo>& jobs)
{
	m_lock.Lock();
	m_scheduler.GetQueuedJobInfo(jobs);
	m_lock.Unlock();
}

bool CRenderThread::GetCurrentJobInfo(JobInfo& job)
{
	m_lock.Lock();
	bool active = m_scheduler.GetCurrentJobInfo(job);
	m_lock.Unlock();
	return active;
}

bool CRenderThread::IsCurrentJobRejected()
{
	m_lock.Lock();
	bool rejected = m_scheduler.IsCurrentJobRejected();
	m_lock.Unlock();
	return rejected;
}

void CRenderThread::ResetSchedulerMetrics()
{
	m_lock.Lock();
	m_scheduler.ResetMetrics();
	m_lock.Unlock();
}

void CRenderThread::GetSchedulerMetrics(SchedulerMetrics& metrics)
{
	m_lock.Lock();
	metrics = m_scheduler.GetMetrics();
	m_lock.Unlock();
}

void CRenderThread::GetTileRenderStats(int& regions, int& fallbacks)
{
	m_lock.Lock();
	regions = m_nTileRegionRenders;
	fallbacks = m_nTileFallbacks;
	m_lock.Unlock();
}

void CRenderThread::GetTileWorkerMetrics(TileWorkerMetrics& metrics)
{
	m_lock.Lock();
	metrics = m_tileWorkerMetrics;
	metrics.configuredTileWorkers = m_nTileWorkerLimit;
	metrics.liveBatches = static_cast<int>(m_tileBatches.size());
	m_lock.Unlock();
}

HANDLE CRenderThread::DuplicateThreadHandleForRegression()
{
	HANDLE duplicate = NULL;
	::DuplicateHandle(::GetCurrentProcess(), m_hThread, ::GetCurrentProcess(),
		&duplicate, SYNCHRONIZE, FALSE, 0);
	return duplicate;
}

void CRenderThread::ResetTileWorkerMetricsForBenchmark()
{
	m_lock.Lock();
	// The benchmark calls this only between completed page requests.
	m_tileWorkerMetrics = TileWorkerMetrics();
	m_tileWorkerMetrics.activeTileWorkers = m_nActiveTileWorkers;
	m_tileWorkerMetrics.peakActiveTileWorkers = m_nActiveTileWorkers;
	m_tileWorkerMetrics.liveBatches = static_cast<int>(m_tileBatches.size());
	m_lock.Unlock();
}

void CRenderThread::SetTileStartGateForRegression(HANDLE gate)
{
	m_lock.Lock();
	m_tileStartGateForRegression = gate;
	m_lock.Unlock();
}

void CRenderThread::SignalJobs()
{
	// Caller holds m_lock. Each successful take wakes the next worker so an
	// auto-reset event can fill, but never exceed, the bounded worker pool.
	if (IsPaused() || !m_scheduler.HasQueuedJobs()) return;
	m_jobReady.SetEvent();
	m_tileReady.SetEvent();
}

unsigned int __stdcall CRenderThread::TileThreadProc(void* pvData)
{
	::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	CRenderThread* thread = reinterpret_cast<CRenderThread*>(pvData);
	HANDLE events[] = { thread->m_tileReady.m_hObject, thread->m_stop.m_hObject };
	while (::WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0)
	{
		thread->m_lock.Lock();
		RenderScheduler::Job job;
		unsigned long long token = 0;
		if (!thread->m_scheduler.TakeNextParallelTile(job, token))
		{
			thread->m_lock.Unlock();
			continue;
		}
		++thread->m_nActiveTileWorkers;
		thread->m_tileWorkerMetrics.activeTileWorkers = thread->m_nActiveTileWorkers;
		thread->m_tileWorkerMetrics.peakActiveTileWorkers = max(
			thread->m_tileWorkerMetrics.peakActiveTileWorkers, thread->m_nActiveTileWorkers);
		HANDLE tileGate = thread->m_tileStartGateForRegression;
		thread->SignalJobs();
		thread->m_lock.Unlock();

		bool fallback = false;
		CDIB* tile = NULL;
		if (tileGate != NULL)
		{
			HANDLE gateEvents[] = { thread->m_stop.m_hObject, tileGate };
			if (::WaitForMultipleObjects(2, gateEvents, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
				tile = thread->RenderTileJob(job, fallback);
		}
		else
			tile = thread->RenderTileJob(job, fallback);
		thread->FinishTile(job, token, tile, fallback);
	}
	::CoUninitialize();
	theApp.ThreadTerminated();
	return 0;
}

void CRenderThread::FinishTile(const RenderScheduler::Job& job,
	unsigned long long token, CDIB*& tile, bool fallback)
{
	m_stopping.Lock();
	m_lock.Lock();
	CDIB* completed = NULL;
	bool retryFallback = false;
	bool done = false;
	const bool rejected = ::WaitForSingleObject(m_stop.m_hObject, 0) == WAIT_OBJECT_0 ||
		(token == 0 ? m_scheduler.IsCurrentJobRejected() :
		m_scheduler.IsParallelTileRejected(token));
	++m_tileWorkerMetrics.completedTileJobs;
	if (rejected)
		++m_tileWorkerMetrics.rejectedTileResults;
	else
	{
		if (!fallback) ++m_nTileRegionRenders;
		done = AcceptTileResult(job, token, tile, fallback, retryFallback, completed);
	}
	if (retryFallback)
	{
		++m_nTileFallbacks;
		++m_tileWorkerMetrics.tileFallbacks;
		m_lock.Unlock();
		m_stopping.Unlock();
		RenderScheduler::Job pageJob;
		pageJob.type = RenderScheduler::RENDER;
		pageJob.request = job.tile.key.render;
		delete tile;
		tile = Render(pageJob, true);
		m_stopping.Lock();
		m_lock.Lock();
		const bool fallbackRejected = ::WaitForSingleObject(m_stop.m_hObject, 0) == WAIT_OBJECT_0 ||
			(token == 0 ? m_scheduler.IsCurrentJobRejected() :
			m_scheduler.IsParallelTileRejected(token));
		if (!fallbackRejected)
		{
			bool unusedRetry = false;
			done = AcceptTileResult(job, token, tile, true, unusedRetry, completed);
		}
		else if (!rejected)
			++m_tileWorkerMetrics.rejectedTileResults;
	}
	const bool accept = token == 0 ? m_scheduler.CompleteCurrent(::GetTickCount(), done) :
		m_scheduler.CompleteParallelTile(token, ::GetTickCount(), done);
	if (done) m_scheduler.CancelTiles(job.GetPage());
	--m_nActiveTileWorkers;
	m_tileWorkerMetrics.activeTileWorkers = m_nActiveTileWorkers;
	SignalJobs();
	m_lock.Unlock();
	if (accept && done)
	{
		RenderResult result(job.tile.key.render, completed);
		m_pOwner->OnUpdate(NULL, &BitmapMsg(PAGE_RENDERED, job.GetPage(),
			result.bitmap, &result.request));
	}
	else
		delete completed;
	delete tile;
	tile = NULL;
	m_stopping.Unlock();
}

void CRenderThread::ReconcileJobs(const JobWindows& windows)
{
	m_lock.Lock();
	bool cancelPrefetches = m_scheduler.Reconcile(windows);
	for (map<int, TileBatch*>::iterator it = m_tileBatches.begin(); it != m_tileBatches.end(); )
	{
		if (windows.renderPages.find(it->first) != windows.renderPages.end())
		{
			++it;
			continue;
		}
		delete it->second;
		it = m_tileBatches.erase(it);
		++m_tileWorkerMetrics.batchesDestroyed;
	}
	m_lock.Unlock();

	// Prefetch decoding belongs to DjVuLibre.  It is safe to cancel only when
	// this viewport update actually discarded speculative work.
	if (cancelPrefetches)
		m_pSource->CancelPrefetches();
}

CDIB* CRenderThread::Render(RenderScheduler::Job& job, bool fullPageSource)
{
	GP<DjVuImage> pImage = m_pSource->GetPage(job.request.page, m_pOwner);
	CDIB* pBitmap = NULL;

	if (pImage != NULL)
	{
		if (job.request.size.cx > 0 && job.request.size.cy > 0)
		{
			PageInfo& pInfo = m_pSource->GetPageInfo(job.request.page);
			CSize szPage(pInfo.szPage);
			if (job.request.rotation % 2 != 0)
				swap(szPage.cx, szPage.cy);
			CRect rcCrop = pInfo.GetCropRect((job.request.rotation + pInfo.nInitialRotate) % 4);
			int nX = rcCrop.left + rcCrop.right;
			int nY = rcCrop.bottom + rcCrop.top;
			double fScaleX = job.request.displaySettings.bCropPages ? 1.0 * job.request.size.cx / szPage.cx : 0;
			double fScaleY = job.request.displaySettings.bCropPages ? 1.0 * job.request.size.cy / szPage.cy : 0;
			CPoint pt = CPoint(static_cast<int>(nX * fScaleX), static_cast<int>(nY * fScaleY));

			RenderRequest request(job.request);
			request.size = job.request.size + pt;
			pBitmap = fullPageSource ? RenderFullPage(pImage, request) : Render(pImage, request);
			if (pBitmap != NULL && job.request.displaySettings.bCropPages && (nX + nY > 1))
			{		
				CRect rcCrop2 = CRect(static_cast<int>(rcCrop.left * fScaleX), static_cast<int>(rcCrop.top * fScaleY),
					static_cast<int>(rcCrop.left * fScaleX + job.request.size.cx), static_cast<int>(rcCrop.top * fScaleY + job.request.size.cy));
				CDIB* pCropped = pBitmap->Crop(rcCrop2);
				delete pBitmap;
				pBitmap = pCropped;
			}
		}
	}

	if (pBitmap == NULL || pBitmap->m_hObject == NULL)
	{
		delete pBitmap;
		pBitmap = NULL;
	}

	return pBitmap;
}

// DjVuLibre's (rect, all) overload renders only rect while retaining the
// full-target scaling coordinates. PnmScaleFixed needs the complete source
// raster, so those requests use the established full-page fallback instead.
CDIB* CRenderThread::RenderTileJob(const RenderScheduler::Job& job, bool& fallback)
{
	fallback = false;
	const RenderRequest& identity = job.tile.key.render;
	const TileRect& tile = job.tile.key.rect;
	CDIB* bitmap = NULL;
	GP<DjVuImage> image = m_pSource->GetPage(identity.page, m_pOwner);
	if (image != NULL && tile.width > 0 && tile.height > 0)
	{
		// Layered color paths can vary by a channel when DjVuLibre is
		// asked for an isolated region. Keep their exact full-page pixels.
		const bool layeredColor = image->get_bg44() != NULL ||
			image->get_bgpm() != NULL || image->get_fgpm() != NULL;
		if (identity.displayMode == CDjVuView::BlackAndWhite || !layeredColor)
		{
		try
		{
			PageInfo info = m_pSource->GetPageInfo(identity.page);
			CSize pageSize(info.szPage);
			if (identity.rotation % 2 != 0) swap(pageSize.cx, pageSize.cy);
			const CRect crop = info.GetCropRect((identity.rotation + info.nInitialRotate) % 4);
			const double sx = identity.displaySettings.bCropPages && pageSize.cx > 0 ?
				static_cast<double>(identity.size.cx) / pageSize.cx : 0.0;
			const double sy = identity.displaySettings.bCropPages && pageSize.cy > 0 ?
				static_cast<double>(identity.size.cy) / pageSize.cy : 0.0;
			const CSize expanded = identity.size + CPoint(
				static_cast<int>((crop.left + crop.right) * sx),
				static_cast<int>((crop.top + crop.bottom) * sy));
			const int cropLeft = static_cast<int>(crop.left * sx);
			const int cropTop = static_cast<int>(crop.top * sy);
			const int x = cropLeft + tile.x;
			const int y = expanded.cy - cropTop - identity.size.cy + tile.y;
			const int rotation = GetTotalRotate(image, identity.rotation);
			CSize scaled(expanded);
			if (rotation % 2 != 0) swap(scaled.cx, scaled.cy);
			const CSize sourceSize(image->get_width(), image->get_height());
			const bool regionScale = identity.displaySettings.bScaleSubpix ?
				(scaled.cx >= sourceSize.cx && scaled.cy >= sourceSize.cy) :
				(scaled.cx >= sourceSize.cx / 2 || scaled.cy >= sourceSize.cy / 2);
			if (pageSize.cx > 0 && pageSize.cy > 0 && expanded.cx > 0 && expanded.cy > 0 &&
				x >= 0 && y >= 0 && x + tile.width <= expanded.cx &&
				y + tile.height <= expanded.cy && regionScale)
			{
				TileRect region(x, y, tile.width, tile.height);
				if (rotation == 1)
					region = TileRect(y, scaled.cy - x - tile.width, tile.height, tile.width);
				else if (rotation == 2)
					region = TileRect(scaled.cx - x - tile.width,
						scaled.cy - y - tile.height, tile.width, tile.height);
				else if (rotation == 3)
					region = TileRect(scaled.cx - y - tile.height, x, tile.height, tile.width);
				const GRect full(0, 0, scaled.cx, scaled.cy);
				const GRect rect(region.x, region.y, region.width, region.height);
				GP<GPixmap> pixmap;
				GP<GBitmap> mask;
				switch (identity.displayMode)
				{
				case CDjVuView::BlackAndWhite:
					mask = image->get_bitmap(rect, full, 4); break;
				case CDjVuView::Foreground:
					pixmap = image->get_fg_pixmap(rect, full);
					if (pixmap == NULL) mask = image->get_bitmap(rect, full, 4);
					break;
				case CDjVuView::Background:
					pixmap = image->get_bg_pixmap(rect, full); break;
				case CDjVuView::Color:
					pixmap = image->get_pixmap(rect, full);
					if (pixmap == NULL) mask = image->get_bitmap(rect, full, 4);
					break;
				}
				if (pixmap != NULL)
				{
					if (rotation != 0) pixmap = pixmap->rotate(rotation);
					bitmap = RenderPixmap(*pixmap, identity.displaySettings);
				}
				else if (mask != NULL)
				{
					if (rotation != 0) mask = mask->rotate(rotation);
					bitmap = RenderBitmap(*mask, identity.displaySettings);
				}
				else
					bitmap = RenderEmpty(CSize(tile.width, tile.height), identity.displaySettings);
				if (bitmap != NULL && bitmap->IsValid() &&
					bitmap->GetSize() == CSize(tile.width, tile.height))
				{
					bitmap->SetDPI(image->get_dpi());
					return bitmap;
				}
			}
		}
		catch (CMemoryException* error) { error->Delete(); }
		catch (GException&) {}
		catch (...) {}
		}
	}
	delete bitmap;
	fallback = true;
	return NULL;
}

// Called with m_lock held after a tile job has physically finished.
bool CRenderThread::AcceptTileResult(const RenderScheduler::Job& job,
	unsigned long long token, CDIB*& tile, bool fallback,
	bool& retryFallback, CDIB*& completed)
{
	completed = NULL;
	retryFallback = false;
	map<int, TileBatch*>::iterator it = m_tileBatches.find(job.GetPage());
	if (it == m_tileBatches.end() || it->second->request != job.tile.key.render ||
		it->second->generation != job.batchGeneration)
	{
		delete tile;
		tile = NULL;
		return false;
	}
	TileBatch* batch = it->second;
	if (fallback)
	{
		if (!batch->fallbackStarted)
		{
			batch->fallbackStarted = true;
			batch->fallbackOwner = job.tile.key;
			m_scheduler.CancelTileSiblings(job.GetPage(), token);
			retryFallback = true;
			return false;
		}
		completed = tile;
		tile = NULL;
		DropTileBatch(job.GetPage());
		return true;
	}
	if (batch->fallbackStarted)
	{
		delete tile;
		tile = NULL;
		return false;
	}
	const TileKey& key = job.tile.key;
	if (tile == NULL || !tile->IsValid() ||
		!(batch->grid.At(key.column, key.row) == key.rect) ||
		tile->GetSize() != CSize(key.rect.width, key.rect.height) ||
		batch->completion.Has(key.column, key.row))
	{
		delete tile;
		tile = NULL;
		batch->fallbackStarted = true;
		batch->fallbackOwner = job.tile.key;
		m_scheduler.CancelTileSiblings(job.GetPage(), token);
		retryFallback = true;
		return false;
	}
	const int bpp = tile->GetBitsPerPixel();
	if (bpp != 8 && bpp != 24)
	{
		delete tile;
		tile = NULL;
		batch->fallbackStarted = true;
		batch->fallbackOwner = job.tile.key;
		m_scheduler.CancelTileSiblings(job.GetPage(), token);
		retryFallback = true;
		return false;
	}
	if (batch->assembled == NULL)
	{
		try
		{
			const BITMAPINFO* tileInfo = tile->GetBitmapInfo();
			const size_t infoBytes = sizeof(BITMAPINFOHEADER) +
				static_cast<size_t>(tileInfo->bmiHeader.biClrUsed) * sizeof(RGBQUAD);
			vector<BYTE> info(infoBytes);
			memcpy(&info[0], tileInfo, infoBytes);
			BITMAPINFO* target = reinterpret_cast<BITMAPINFO*>(&info[0]);
			target->bmiHeader.biWidth = batch->request.size.cx;
			target->bmiHeader.biHeight = batch->request.size.cy;
			batch->assembled = CDIB::CreateDIB(target);
		}
		catch (CMemoryException* error) { error->Delete(); }
		catch (...) {}
	}
	if (batch->assembled == NULL || !batch->assembled->IsValid() ||
		batch->assembled->GetBitsPerPixel() != bpp)
	{
		delete tile;
		tile = NULL;
		batch->fallbackStarted = true;
		batch->fallbackOwner = job.tile.key;
		m_scheduler.CancelTileSiblings(job.GetPage(), token);
		retryFallback = true;
		return false;
	}
	const size_t targetStride = (static_cast<size_t>(batch->request.size.cx) * bpp + 31) / 32 * 4;
	const size_t tileStride = (static_cast<size_t>(key.rect.width) * bpp + 31) / 32 * 4;
	const size_t bytes = static_cast<size_t>(key.rect.width) * bpp / 8;
	for (int y = 0; y < key.rect.height; ++y)
		memcpy(batch->assembled->GetBits() + static_cast<size_t>(key.rect.y + y) * targetStride +
			static_cast<size_t>(key.rect.x) * bpp / 8,
			tile->GetBits() + static_cast<size_t>(y) * tileStride, bytes);
	delete tile;
	tile = NULL;
	batch->completion.Mark(key.column, key.row);
	if (!batch->completion.Complete())
		return false;
	completed = batch->assembled;
	batch->assembled = NULL;
	DropTileBatch(job.GetPage());
	return true;
}

CDIB* CRenderThread::Render(GP<DjVuImage> pImage, const CSize& size,
		const CDisplaySettings& displaySettings, int nDisplayMode,
		int nRotate, bool bThumbnail)
{
	// Direct callers (thumbnails, saving/exporting, selection and print-related
	// paths) retain the established full-page renderer. Only viewport jobs use
	// the RenderRequest overload that can select the tiled path.
	return RenderFullPage(pImage, RenderRequest(-1, size, nRotate, nDisplayMode, displaySettings), bThumbnail);
}

namespace
{
	CDIB* RenderTile(GPixmap& source, const TileRect& rect, const CDisplaySettings& settings)
	{
		return RenderPixmap(source, CRect(rect.x, rect.y, rect.x + rect.width,
			rect.y + rect.height), settings);
	}

	CDIB* RenderTile(GBitmap& source, const TileRect& rect, const CDisplaySettings& settings)
	{
		return RenderBitmap(source, CRect(rect.x, rect.y, rect.x + rect.width,
			rect.y + rect.height), settings);
	}

	template <typename T>
	CDIB* AssembleTiles(T& source, const RenderRequest& request)
	{
		const int width = source.columns();
		const int height = source.rows();
		TileGrid grid(width, height);
		CDIB* result = NULL;
		CDIB* currentTile = NULL;
		try
		{
			for (int row = 0; row < grid.Rows(); ++row)
			{
				for (int column = 0; column < grid.Columns(); ++column)
				{
					TileKey key(request, grid.At(column, row), column, row);
					TileRequest tileRequest(key);
					currentTile = RenderTile(source, tileRequest.key.rect, request.displaySettings);
					TileResult tile(key, currentTile);
					if (tile.bitmap == NULL || !tile.bitmap->IsValid())
					{
						delete tile.bitmap;
						currentTile = NULL;
						delete result;
						return NULL;
					}
					const int bpp = tile.bitmap->GetBitsPerPixel();
					if (bpp != 8 && bpp != 24)
					{
						delete tile.bitmap;
						currentTile = NULL;
						delete result;
						return NULL;
					}
					if (result == NULL)
					{
						const BITMAPINFO* tileInfo = tile.bitmap->GetBitmapInfo();
						const size_t infoBytes = sizeof(BITMAPINFOHEADER) +
							static_cast<size_t>(tileInfo->bmiHeader.biClrUsed) * sizeof(RGBQUAD);
						vector<BYTE> info(infoBytes);
						memcpy(&info[0], tileInfo, infoBytes);
						BITMAPINFO* destinationInfo = reinterpret_cast<BITMAPINFO*>(&info[0]);
						destinationInfo->bmiHeader.biWidth = width;
						destinationInfo->bmiHeader.biHeight = height;
						result = CDIB::CreateDIB(destinationInfo);
						if (result == NULL || !result->IsValid())
						{
							delete tile.bitmap;
							currentTile = NULL;
							delete result;
							return NULL;
						}
					}
					const size_t destinationStride = (static_cast<size_t>(width) * bpp + 31) / 32 * 4;
					const size_t tileStride = (static_cast<size_t>(tile.key.rect.width) * bpp + 31) / 32 * 4;
					const size_t copyBytes = static_cast<size_t>(tile.key.rect.width) * bpp / 8;
					for (int y = 0; y < tile.key.rect.height; ++y)
						memcpy(result->GetBits() + (static_cast<size_t>(tile.key.rect.y + y) * destinationStride) +
							static_cast<size_t>(tile.key.rect.x) * bpp / 8,
							tile.bitmap->GetBits() + static_cast<size_t>(y) * tileStride, copyBytes);
					delete tile.bitmap;
					currentTile = NULL;
				}
			}
		}
		catch (CMemoryException* error)
		{
			error->Delete();
			delete currentTile;
			delete result;
			return NULL;
		}
		catch (GException&)
		{
			delete currentTile;
			delete result;
			return NULL;
		}
		catch (...)
		{
			delete currentTile;
			delete result;
			return NULL;
		}
		return result;
	}
}

CDIB* CRenderThread::Render(GP<DjVuImage> pImage, const RenderRequest& request, bool bThumbnail)
{
	return RenderInternal(pImage, request, bThumbnail, true, false);
}

CDIB* CRenderThread::RenderFullPage(GP<DjVuImage> pImage, const RenderRequest& request, bool bThumbnail)
{
	return RenderInternal(pImage, request, bThumbnail, false, false);
}

CDIB* CRenderThread::RenderTiled(GP<DjVuImage> pImage, const RenderRequest& request)
{
	return RenderInternal(pImage, request, false, true, true);
}

CDIB* CRenderThread::RenderInternal(GP<DjVuImage> pImage, const RenderRequest& request,
	bool bThumbnail, bool bAllowTiles, bool bRequireTiles)
{
	if (request.size.cx <= 0 || request.size.cy <= 0)
		return NULL;
	const bool bTiledRequest = bAllowTiles &&
		TileGeometry::ShouldTile(request.size.cx, request.size.cy, bThumbnail) &&
		(request.displayMode == CDjVuView::Color ||
		 request.displayMode == CDjVuView::BlackAndWhite ||
		 request.displayMode == CDjVuView::Foreground ||
		 request.displayMode == CDjVuView::Background);
	if (bRequireTiles && !bTiledRequest)
		return NULL;
	CSize szImage(pImage->get_width(), pImage->get_height());
	int nTotalRotate = GetTotalRotate(pImage, request.rotation);

	CSize szScaled(request.size);
	if (nTotalRotate % 2 != 0)
		swap(szScaled.cx, szScaled.cy);

	GRect rect(0, 0, szScaled.cx, szScaled.cy);

	bool bScalePnmFixed = true;
	bool bScaleSubpix = false;


	// Use fast scaling for thumbnails.
	if (bThumbnail)
		bScalePnmFixed = false;

	if (request.displaySettings.bScaleSubpix)
	{
		// Use subpixel scaling in most cases, unless scaled image size
		// is too small or if we are upscaling.
		if ((szScaled.cx < 100 || szScaled.cy < 100)
				|| (szScaled.cx >= szImage.cx || szScaled.cy >= szImage.cy))
			bScalePnmFixed = false;
	}
	else
	{
		// Results from PnmScaleFixed are comparable to libdjvu scaling,
		// when zoom factor is greater than 0.5, so use default faster scaling
		// in this case. Additionally, use the default scaling when requested
		// image size is small, since quality does not matter at this
		// scale. NOTE: this also deals with the special case of size (1, 1),
		// which can force PnmScaleFixed into an infinite loop.
		if ((szScaled.cx < 100 || szScaled.cy < 100)
				|| (szScaled.cx >= szImage.cx / 2 || szScaled.cy >= szImage.cy / 2))
			bScalePnmFixed = false;

		// Disable PnmFixed scaling if we perform an integer reduction of the image.
		for (int nReduction = 1; nReduction <= 15; ++nReduction)
		{
			if (szScaled.cx*nReduction > szImage.cx - nReduction
					&& szScaled.cx*nReduction < szImage.cx + nReduction
					&& szScaled.cy*nReduction > szImage.cy - nReduction
					&& szScaled.cy*nReduction < szImage.cy + nReduction)
			{
				bScalePnmFixed = false;
				break;
			}
		}
	}

	// Disable PnmFixed scaling for color images according to settings
	GP<IW44Image> bg44 = pImage->get_bg44();
	GP<GPixmap> bgpm = pImage->get_bgpm();
	GP<GPixmap> fgpm = pImage->get_fgpm();
	if (!request.displaySettings.bScaleColorPnm && (bg44 != NULL || bgpm != NULL || fgpm != NULL)
			&& request.displayMode != CDjVuView::BlackAndWhite)
		bScalePnmFixed = false;

	if (bScalePnmFixed && request.displaySettings.bScaleSubpix)
		bScaleSubpix = true;

	if (bScalePnmFixed)
		rect = GRect(0, 0, szImage.cx, szImage.cy);

	GP<GBitmap> pGBitmap;
	GP<GPixmap> pGPixmap;

	try
	{
		switch (request.displayMode)
		{
		case CDjVuView::BlackAndWhite:
			pGBitmap = pImage->get_bitmap(rect, rect, 4);
			break;

		case CDjVuView::Foreground:
			pGPixmap = pImage->get_fg_pixmap(rect, rect);
			if (pGPixmap == NULL)
				pGBitmap = pImage->get_bitmap(rect, rect, 4);
			break;

		case CDjVuView::Background:
			pGPixmap = pImage->get_bg_pixmap(rect, rect);
			break;

		case CDjVuView::Color:
		default:
			pGPixmap = pImage->get_pixmap(rect, rect);
			if (pGPixmap == NULL)
				pGBitmap = pImage->get_bitmap(rect, rect, 4);
		}
	}
	catch (GException&)
	{
		return NULL;
	}
	catch (CMemoryException*)
	{
		return NULL;
	}
	catch (...)
	{
		theApp.ReportFatalError();
	}

	CDIB* pBitmap = NULL;

	if (pGPixmap != NULL)
	{
		if (nTotalRotate != 0)
			pGPixmap = pGPixmap->rotate(nTotalRotate);

		if (bScalePnmFixed)
		{
			if (bScaleSubpix)
				pGPixmap = RescalePnm_subpix(pGPixmap, request.size.cx, request.size.cy);
			else
				pGPixmap = RescalePnm(pGPixmap, request.size.cx, request.size.cy);
		}

		if (bTiledRequest)
			pBitmap = AssembleTiles(*pGPixmap, request);
		if (pBitmap == NULL && !bRequireTiles)
			pBitmap = RenderPixmap(*pGPixmap, request.displaySettings);
	}
	else if (pGBitmap != NULL)
	{
		if (nTotalRotate != 0)
			pGBitmap = pGBitmap->rotate(nTotalRotate);

		if (bScalePnmFixed)
		{
			if (bScaleSubpix)
				pGPixmap = RescalePnm_subpix(pGBitmap, request.size.cx, request.size.cy);
			else
				pGBitmap = RescalePnm(pGBitmap, request.size.cx, request.size.cy);
		}

		if (pGPixmap)
		{
			if (bTiledRequest)
				pBitmap = AssembleTiles(*pGPixmap, request);
			if (pBitmap == NULL && !bRequireTiles)
				pBitmap = RenderPixmap(*pGPixmap, request.displaySettings);
		}
		else
		{
			if (bTiledRequest)
				pBitmap = AssembleTiles(*pGBitmap, request);
			if (pBitmap == NULL && !bRequireTiles)
				pBitmap = RenderBitmap(*pGBitmap, request.displaySettings);
		}
	}
	else
	{
		if (!bRequireTiles)
			pBitmap = RenderEmpty(request.size, request.displaySettings);
	}

	if (pBitmap != NULL)
		pBitmap->SetDPI(pImage->get_dpi());

	return pBitmap;
}

void CRenderThread::AddJob(int nPage, int nRotate, const CSize& size,
		const CDisplaySettings& displaySettings, int nDisplayMode, JobPriority priority)
{
	AddJob(RenderRequest(nPage, size, nRotate, nDisplayMode, displaySettings), priority);
}

void CRenderThread::AddJob(const RenderRequest& request, JobPriority priority)
{
	RenderScheduler::Job job;
	job.request = request;
	job.type = RenderScheduler::RENDER;
	job.priority = (RenderScheduler::JobPriority)priority;
	m_lock.Lock();
	DropTileBatch(request.page);
	bool queued = m_scheduler.Submit(job, ::GetTickCount());
	if (queued) SignalJobs();
	m_lock.Unlock();
}

void CRenderThread::AddViewportJob(const RenderRequest& request, JobPriority priority)
{
	TileGrid grid(request.size.cx, request.size.cy);
	const bool supported = request.displayMode == CDjVuView::Color ||
		request.displayMode == CDjVuView::BlackAndWhite ||
		request.displayMode == CDjVuView::Foreground ||
		request.displayMode == CDjVuView::Background;
	if (!supported || !TileGeometry::ShouldTile(request.size.cx, request.size.cy, false) ||
		grid.Count() > 1024)
	{
		AddJob(request, priority);
		return;
	}
	m_lock.Lock();
	bool queued = false;
	try
	{
		map<int, TileBatch*>::iterator existing = m_tileBatches.find(request.page);
		if (existing != m_tileBatches.end() && existing->second->request != request)
			DropTileBatch(request.page);
		if (m_tileBatches.find(request.page) == m_tileBatches.end())
		{
			TileBatch* created = new TileBatch(request, m_nextTileBatchGeneration++);
			try { m_tileBatches.insert(make_pair(request.page, created)); }
			catch (...) { delete created; throw; }
			++m_tileWorkerMetrics.batchesCreated;
		}
		TileBatch* batch = m_tileBatches.find(request.page)->second;
		if (batch->fallbackStarted)
		{
			RenderScheduler::Job owner;
			owner.type = RenderScheduler::TILE_RENDER;
			owner.tile = TileRequest(batch->fallbackOwner);
			owner.batchGeneration = batch->generation;
			owner.priority = (RenderScheduler::JobPriority)priority;
			m_scheduler.Submit(owner, ::GetTickCount());
			m_lock.Unlock();
			return;
		}
		for (int row = 0; row < batch->grid.Rows(); ++row)
		{
			for (int column = 0; column < batch->grid.Columns(); ++column)
			{
				if (batch->completion.Has(column, row)) continue;
				RenderScheduler::Job job;
				job.type = RenderScheduler::TILE_RENDER;
				job.tile = TileRequest(TileKey(request, batch->grid.At(column, row), column, row));
				job.batchGeneration = batch->generation;
				job.priority = (RenderScheduler::JobPriority)priority;
				queued = m_scheduler.Submit(job, ::GetTickCount()) || queued;
			}
		}
	}
	catch (CMemoryException* error)
	{
		error->Delete();
		m_scheduler.CancelTiles(request.page);
		DropTileBatch(request.page);
		m_lock.Unlock();
		AddJob(request, priority);
		return;
	}
	catch (...)
	{
		m_scheduler.CancelTiles(request.page);
		DropTileBatch(request.page);
		m_lock.Unlock();
		AddJob(request, priority);
		return;
	}
	if (queued) SignalJobs();
	m_lock.Unlock();
}

void CRenderThread::AddDecodeJob(int nPage)
{
	RenderScheduler::Job job;
	job.nPage = nPage;
	job.type = RenderScheduler::DECODE;
	job.priority = RenderScheduler::Decode;

	AddJob(job);
}

void CRenderThread::AddPrefetchJob(int nPage)
{
	RenderScheduler::Job job;
	job.nPage = nPage;
	job.type = RenderScheduler::PREFETCH_DECODE;
	job.priority = RenderScheduler::AdjacentPrefetch;

	AddJob(job);
}

void CRenderThread::AddReadInfoJob(int nPage)
{
	RenderScheduler::Job job;
	job.nPage = nPage;
	job.type = RenderScheduler::READINFO;
	job.priority = RenderScheduler::Background;

	AddJob(job);
}

void CRenderThread::AddCleanupJob(int nPage)
{
	RenderScheduler::Job job;
	job.nPage = nPage;
	job.type = RenderScheduler::CLEANUP;
	job.priority = RenderScheduler::Background;

	AddJob(job);
}

void CRenderThread::AddJob(const RenderScheduler::Job& job)
{
	m_lock.Lock();
	bool queued = m_scheduler.Submit(job, ::GetTickCount());
	if (queued) SignalJobs();

	m_lock.Unlock();
}

void CRenderThread::RemoveAllJobs()
{
	m_lock.Lock();
	m_scheduler.Clear();
	if (!m_tileBatches.empty())
	{
		ClearTileBatches();
		m_scheduler.RejectParallelTiles();
		JobInfo current;
		if (m_scheduler.GetCurrentJobInfo(current) && current.type == TILE_RENDER)
			m_scheduler.RejectCurrent();
	}

	m_lock.Unlock();

	// A speculative decode is owned by DjVuLibre, not this worker. Cancel it
	// after dropping queued jobs so a distant navigation cannot consume CPU
	// ahead of the next visible render.
	m_pSource->CancelPrefetches();
}

void CRenderThread::RejectCurrentJob()
{
	m_lock.Lock();
	m_scheduler.RejectCurrent();
	m_lock.Unlock();
}

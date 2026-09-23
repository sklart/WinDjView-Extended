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

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CRenderThread class

CRenderThread::CRenderThread(DjVuSource* pSource, Observer* pOwner)
	: m_pOwner(pOwner), m_pSource(pSource), m_nPaused(0),
	  m_scheduler(pSource->GetPageCount())
{
	m_pSource->AddRef();

	UINT dwThreadId;
	m_hThread = (HANDLE)_beginthreadex(NULL, 0, RenderThreadProc, this, 0, &dwThreadId);
	::SetThreadPriority(m_hThread, THREAD_PRIORITY_BELOW_NORMAL);
	theApp.ThreadStarted();
}

CRenderThread::~CRenderThread()
{
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
	m_jobReady.ResetEvent();
	m_scheduler.RejectCurrent();
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

		if (!pThread->m_scheduler.HasQueuedJobs())
		{
			pThread->m_lock.Unlock();
			continue;
		}

		RenderScheduler::Job job;
		pThread->m_scheduler.TakeNext(job);
		pThread->m_lock.Unlock();

		CDIB* pBitmap = NULL;

		switch (job.type)
		{
		case RENDER:
			pBitmap = pThread->Render(job);
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

		// Cannot stop while the owner is being updated
		pThread->m_stopping.Lock();

		pThread->m_lock.Lock();
		bool bNotify = pThread->m_scheduler.CompleteCurrent(::GetTickCount());
		if (pThread->m_scheduler.HasQueuedJobs() && !pThread->IsPaused())
			pThread->m_jobReady.SetEvent();
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
	if (m_scheduler.HasQueuedJobs())
		m_jobReady.SetEvent();

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

void CRenderThread::ReconcileJobs(const JobWindows& windows)
{
	m_lock.Lock();
	bool cancelPrefetches = m_scheduler.Reconcile(windows);
	m_lock.Unlock();

	// Prefetch decoding belongs to DjVuLibre.  It is safe to cancel only when
	// this viewport update actually discarded speculative work.
	if (cancelPrefetches)
		m_pSource->CancelPrefetches();
}

CDIB* CRenderThread::Render(RenderScheduler::Job& job)
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
			pBitmap = Render(pImage, request);
			if (job.request.displaySettings.bCropPages && (nX + nY > 1))
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

CDIB* CRenderThread::Render(GP<DjVuImage> pImage, const CSize& size,
		const CDisplaySettings& displaySettings, int nDisplayMode,
		int nRotate, bool bThumbnail)
{
	return Render(pImage, RenderRequest(-1, size, nRotate, nDisplayMode, displaySettings), bThumbnail);
}

CDIB* CRenderThread::Render(GP<DjVuImage> pImage, const RenderRequest& request, bool bThumbnail)
{
	if (request.size.cx <= 0 || request.size.cy <= 0)
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
			pBitmap = RenderPixmap(*pGPixmap, request.displaySettings);
		else
			pBitmap = RenderBitmap(*pGBitmap, request.displaySettings);
	}
	else
	{
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

	AddJob(job);
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
	if (queued && !IsPaused())
		m_jobReady.SetEvent();

	m_lock.Unlock();
}

void CRenderThread::RemoveAllJobs()
{
	m_lock.Lock();
	m_scheduler.Clear();

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

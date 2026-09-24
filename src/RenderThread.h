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

#pragma once

#include "Global.h"
#include "DjVuView.h"
#include "RenderRequest.h"
#include "RenderScheduler.h"
class DjVuSource;
class CDIB;


class CRenderThread
{
public:
	// Compatibility aliases preserve the established CRenderThread API while
	// all queue policy lives in RenderScheduler.
	enum JobType { RENDER = RenderScheduler::RENDER, DECODE = RenderScheduler::DECODE,
		PREFETCH_DECODE = RenderScheduler::PREFETCH_DECODE, READINFO = RenderScheduler::READINFO,
		CLEANUP = RenderScheduler::CLEANUP, TILE_RENDER = RenderScheduler::TILE_RENDER };
	enum JobPriority { CurrentPageRender = RenderScheduler::CurrentPageRender,
		VisibleRender = RenderScheduler::VisibleRender, Decode = RenderScheduler::Decode,
		AdjacentPrefetch = RenderScheduler::AdjacentPrefetch, Background = RenderScheduler::Background };
	typedef RenderScheduler::JobInfo JobInfo;
	typedef RenderScheduler::Metrics SchedulerMetrics;
	typedef RenderScheduler::JobWindows JobWindows;

	CRenderThread(DjVuSource* pSource, Observer* pOwner);
	void Stop();

	void AddJob(int nPage, int nRotate, const CSize& size, const CDisplaySettings& displaySettings,
		int nDisplayMode = CDjVuView::Color, JobPriority priority = VisibleRender);
	void AddJob(const RenderRequest& request, JobPriority priority = VisibleRender);
	void AddViewportJob(const RenderRequest& request, JobPriority priority = VisibleRender);
	void AddDecodeJob(int nPage);
	void AddPrefetchJob(int nPage);
	void AddReadInfoJob(int nPage);
	void AddCleanupJob(int nPage);
	void RemoveAllJobs();

	static CDIB* Render(GP<DjVuImage> pImage, const CSize& size,
		const CDisplaySettings& displaySettings, int nDisplayMode,
		int nRotate, bool bThumbnail = false);
	static CDIB* Render(GP<DjVuImage> pImage, const RenderRequest& request,
		bool bThumbnail = false);
	// Explicit compatibility path for pixel-equivalence tests and fallback.
	static CDIB* RenderFullPage(GP<DjVuImage> pImage, const RenderRequest& request,
		bool bThumbnail = false);
	// Returns NULL instead of falling back; used to verify the tiled algorithm.
	static CDIB* RenderTiled(GP<DjVuImage> pImage, const RenderRequest& request);

	void PauseJobs();
	void ResumeJobs();
	bool IsPaused();
	int GetQueuedJobCount();
	bool IsPrefetchQueued(int nPage);
	// Snapshot/reset the real worker queue and accepted job submissions. The
	// production cache regression keeps the worker paused while measuring these.
	void GetQueuedJobCounts(int& render, int& decode, int& prefetchDecode);
	void ResetSubmittedJobCounts();
	void GetSubmittedJobCounts(int& render, int& decode, int& prefetchDecode);
	void GetQueuedJobInfo(vector<JobInfo>& jobs);
	bool GetCurrentJobInfo(JobInfo& job);
	bool IsCurrentJobRejected();
	void ResetSchedulerMetrics();
	void GetSchedulerMetrics(SchedulerMetrics& metrics);
	void ReconcileJobs(const JobWindows& windows);

	void RejectCurrentJob();

private:
	static CDIB* RenderInternal(GP<DjVuImage> pImage, const RenderRequest& request,
		bool bThumbnail, bool bAllowTiles, bool bRequireTiles);
	HANDLE m_hThread, m_hStopThread;
	CCriticalSection m_lock;
	CCriticalSection m_stopping;
	CEvent m_stop;
	CEvent m_jobReady;
	Observer* m_pOwner;
	DjVuSource* m_pSource;
	long m_nPaused;
	RenderScheduler m_scheduler;
	struct TileBatch
	{
		explicit TileBatch(const RenderRequest& request_);
		~TileBatch();
		RenderRequest request;
		TileGrid grid;
		TileCompletion completion;
		CDIB* source;
		CDIB* assembled;
		bool fallback;
	};
	map<int, TileBatch*> m_tileBatches;

	static unsigned int __stdcall RenderThreadProc(void* pvData);
	CDIB* Render(RenderScheduler::Job& job, bool fullPageSource = false);
	void ClearTileBatches();
	void DropTileBatch(int nPage);
	bool AcceptTileResult(const RenderScheduler::Job& job, CDIB*& source,
		CDIB*& completed);
	void AddJob(const RenderScheduler::Job& job);
	~CRenderThread();
};

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
class DjVuSource;
class CDIB;


class CRenderThread
{
public:
	enum JobType { RENDER, DECODE, PREFETCH_DECODE, READINFO, CLEANUP };
	enum JobPriority { CurrentPageRender, VisibleRender, Decode, AdjacentPrefetch, Background };

	struct JobInfo
	{
		int nPage;
		int type;
		int priority;
		CSize size;
	};

	struct SchedulerMetrics
	{
		int peakQueueLength;
		int submittedRenderJobs;
		int submittedDecodeJobs;
		int submittedPrefetchJobs;
		int obsoleteJobsRemoved;
		int obsoleteJobsRejected;
		int jobsExecutedBeforeCurrentPage;
		DWORD currentPageResultElapsedMs;
	};

	struct JobWindows
	{
		set<int> renderPages;
		set<int> decodePages;
		set<int> prefetchPages;
		set<int> readInfoPages;
		set<int> cleanupPages;
	};

	CRenderThread(DjVuSource* pSource, Observer* pOwner);
	void Stop();

	void AddJob(int nPage, int nRotate, const CSize& size, const CDisplaySettings& displaySettings,
		int nDisplayMode = CDjVuView::Color, JobPriority priority = VisibleRender);
	void AddDecodeJob(int nPage);
	void AddPrefetchJob(int nPage);
	void AddReadInfoJob(int nPage);
	void AddCleanupJob(int nPage);
	void RemoveAllJobs();

	static CDIB* Render(GP<DjVuImage> pImage, const CSize& size,
		const CDisplaySettings& displaySettings, int nDisplayMode,
		int nRotate, bool bThumbnail = false);

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
	void ResetSchedulerMetrics();
	void GetSchedulerMetrics(SchedulerMetrics& metrics);
	void ReconcileJobs(const JobWindows& windows);

	void RejectCurrentJob();

private:
	HANDLE m_hThread, m_hStopThread;
	CCriticalSection m_lock;
	CCriticalSection m_stopping;
	CEvent m_stop;
	CEvent m_jobReady;
	Observer* m_pOwner;
	DjVuSource* m_pSource;
	long m_nPaused;
	int m_nSubmittedRenderJobs, m_nSubmittedDecodeJobs, m_nSubmittedPrefetchJobs;
	int m_nPeakQueueLength, m_nObsoleteJobsRemoved, m_nObsoleteJobsRejected;
	int m_nJobsExecutedBeforeCurrentPage;
	DWORD m_dwCurrentPageRequest, m_dwCurrentPageResultElapsed;
	bool m_bAwaitingCurrentPageResult;

	struct Job
	{
		int nPage;
		int nRotate;
		int nDisplayMode;
		CDisplaySettings displaySettings;
		CSize size;
		JobType type;
		JobPriority priority;
	};
	list<Job> m_jobs;
	vector<list<Job>::iterator> m_pages;
	Job m_currentJob;
	bool m_bRejectCurrentJob;

	static unsigned int __stdcall RenderThreadProc(void* pvData);
	CDIB* Render(Job& job);
	void AddJob(const Job& job);
	void RemoveFromQueue(int nPage);
	bool HasSameRenderIdentity(const Job& left, const Job& right) const;
	~CRenderThread();
};

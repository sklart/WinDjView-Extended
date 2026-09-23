//	WinDjView
//	Copyright (C) 2026 sklart
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#pragma once

#include "Global.h"
#include "RenderRequest.h"

// Pure queue policy for CRenderThread. This class owns no worker, DjVuSource,
// UI object, or bitmap; callers execute the Job returned by TakeNext().
class RenderScheduler
{
public:
	enum JobType { RENDER, DECODE, PREFETCH_DECODE, READINFO, CLEANUP };
	enum JobPriority { CurrentPageRender, VisibleRender, Decode, AdjacentPrefetch, Background };

	struct Job
	{
		Job() : nPage(-1), type(DECODE), priority(Background) {}
		int GetPage() const { return type == RENDER ? request.page : nPage; }
		bool IsActive() const { return GetPage() >= 0; }

		// nPage is retained only for non-render jobs. Render jobs use request.
		int nPage;
		RenderRequest request;
		JobType type;
		JobPriority priority;
	};

	struct JobInfo
	{
		int nPage;
		int type;
		int priority;
		CSize size;
	};

	struct Metrics
	{
		Metrics() : peakQueueLength(0), submittedRenderJobs(0), submittedDecodeJobs(0),
			submittedPrefetchJobs(0), obsoleteJobsRemoved(0), obsoleteJobsRejected(0),
			jobsExecutedBeforeCurrentPage(0), currentPageResultElapsedMs(0) {}

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

	RenderScheduler(int nPageCount = 0);
	void SetPageCount(int nPageCount);

	// Returns true only if a new queued job was accepted. now is supplied by
	// the worker so scheduler tests do not depend on a clock or a thread.
	bool Submit(const Job& job, DWORD now);
	bool TakeNext(Job& job);
	// Returns whether the completed job remains valid for publication.
	bool CompleteCurrent(DWORD now);
	void RejectCurrent();

	// Returns true when removed speculative work requires the caller to cancel
	// its external prefetch side effect.
	bool Reconcile(const JobWindows& windows);
	bool Clear();

	bool HasQueuedJobs() const { return !m_jobs.empty(); }
	int GetQueuedJobCount() const { return (int)m_jobs.size(); }
	bool IsPrefetchQueued(int nPage) const;
	void GetQueuedJobCounts(int& render, int& decode, int& prefetchDecode) const;
	void GetQueuedJobInfo(vector<JobInfo>& jobs) const;
	bool GetCurrentJobInfo(JobInfo& job) const;
	bool IsCurrentJobRejected() const { return m_currentJob.IsActive() && m_bRejectCurrentJob; }

	void ResetSubmittedJobCounts();
	void GetSubmittedJobCounts(int& render, int& decode, int& prefetchDecode) const;
	void ResetMetrics();
	Metrics GetMetrics() const { return m_metrics; }

private:
	list<Job> m_jobs;
	vector<list<Job>::iterator> m_pages;
	Job m_currentJob;
	bool m_bRejectCurrentJob;
	Metrics m_metrics;
	DWORD m_dwCurrentPageRequest;
	bool m_bAwaitingCurrentPageResult;

	void RemoveFromQueue(int nPage);
	bool HasSameRenderIdentity(const Job& left, const Job& right) const;
};

//	WinDjView
//	Copyright (C) 2026 sklart
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include "stdafx.h"
#include "RenderScheduler.h"

RenderScheduler::RenderScheduler(int nPageCount)
	: m_bRejectCurrentJob(false), m_dwCurrentPageRequest(0),
	  m_bAwaitingCurrentPageResult(false)
{
	SetPageCount(nPageCount);
}

void RenderScheduler::SetPageCount(int nPageCount)
{
	m_jobs.clear();
	m_pages.assign(max(nPageCount, 0), m_jobs.end());
	m_currentJob = Job();
	m_bRejectCurrentJob = false;
	ResetMetrics();
	ResetSubmittedJobCounts();
}

bool RenderScheduler::HasSameRenderIdentity(const Job& left, const Job& right) const
{
	return left.request == right.request;
}

bool RenderScheduler::Submit(const Job& job, DWORD now)
{
	const int nPage = job.GetPage();
	if (nPage < 0 || nPage >= (int)m_pages.size())
		return false;

	// One page has at most one queued semantic job. Foreground work always
	// replaces speculative work; a changed render identity replaces its stale
	// queued request instead of allowing zoom/rotate events to accumulate.
	list<Job>::iterator existing = m_pages[nPage];
	if (job.type == PREFETCH_DECODE && existing != m_jobs.end() &&
		existing->priority < AdjacentPrefetch)
		return false;

	bool replacingCurrentRender = false;
	if (m_currentJob.IsActive() && m_currentJob.GetPage() == nPage &&
		m_currentJob.type == job.type)
	{
		if (job.type != RENDER || HasSameRenderIdentity(job, m_currentJob))
		{
			if (job.type == RENDER)
			{
				// The same render became useful again before completion, or already
				// useful work was promoted to the current page. Reuse it only for an
				// exact identity match, without queueing a duplicate.
				m_bRejectCurrentJob = false;
				if (job.priority == CurrentPageRender && m_currentJob.priority != CurrentPageRender)
				{
					m_currentJob.priority = CurrentPageRender;
					m_dwCurrentPageRequest = now;
					m_metrics.currentPageResultElapsedMs = 0;
					m_metrics.jobsExecutedBeforeCurrentPage = 0;
					m_bAwaitingCurrentPageResult = true;
				}
			}
			return false;
		}
		// Rendering is not interrupted. The worker safely drops its result and
		// the replacement request below is the only one that will be notified.
		if (!m_bRejectCurrentJob)
		{
			m_bRejectCurrentJob = true;
			++m_metrics.obsoleteJobsRejected;
		}
		replacingCurrentRender = true;
	}

	// Cleanup is deferred maintenance. If foreground work for the same page is
	// already running, retain one cleanup request behind it so cache ownership
	// is still released after an obsolete render/decode completes.
	const bool queueCleanupAfterCurrent = job.type == CLEANUP;
	if (m_currentJob.IsActive() && m_currentJob.GetPage() == nPage &&
		m_currentJob.priority < job.priority && !replacingCurrentRender &&
		!queueCleanupAfterCurrent)
		return false;

	if (existing != m_jobs.end())
	{
		// A page leaving the render window can remain in the wider decode
		// window. Its stale foreground render must yield to that decode job;
		// otherwise reconciliation would remove the render and leave no work.
		const bool renderToDecode = existing->type == RENDER && job.type == DECODE;
		if (existing->priority < job.priority && job.type != CLEANUP && !renderToDecode)
			return false;
		if (existing->type == job.type && (job.type != RENDER || HasSameRenderIdentity(job, *existing)) &&
			existing->priority == job.priority)
			return false;
		if (existing->type == RENDER || existing->type == PREFETCH_DECODE)
			++m_metrics.obsoleteJobsRemoved;
		RemoveFromQueue(nPage);
	}

	if (job.type == RENDER && job.priority == CurrentPageRender)
	{
		m_dwCurrentPageRequest = now;
		m_metrics.currentPageResultElapsedMs = 0;
		m_metrics.jobsExecutedBeforeCurrentPage = 0;
		m_bAwaitingCurrentPageResult = true;
	}
	if (job.type == RENDER)
		++m_metrics.submittedRenderJobs;
	else if (job.type == DECODE)
		++m_metrics.submittedDecodeJobs;
	else if (job.type == PREFETCH_DECODE)
		++m_metrics.submittedPrefetchJobs;

	// Keep FIFO order within a priority and insert before lower-priority work.
	list<Job>::iterator insertAt = m_jobs.begin();
	while (insertAt != m_jobs.end() && insertAt->priority <= job.priority)
		++insertAt;
	list<Job>::iterator inserted = m_jobs.insert(insertAt, job);
	m_pages[nPage] = inserted;
	if ((int)m_jobs.size() > m_metrics.peakQueueLength)
		m_metrics.peakQueueLength = (int)m_jobs.size();

	return true;
}

bool RenderScheduler::TakeNext(Job& job)
{
	if (m_jobs.empty())
		return false;

	job = m_jobs.front();
	m_bRejectCurrentJob = false;
	m_currentJob = job;
	m_jobs.pop_front();
	m_pages[job.GetPage()] = m_jobs.end();
	if (m_bAwaitingCurrentPageResult && job.priority != CurrentPageRender)
		++m_metrics.jobsExecutedBeforeCurrentPage;
	return true;
}

bool RenderScheduler::CompleteCurrent(DWORD now)
{
	const bool notify = !m_bRejectCurrentJob;
	if (notify && m_currentJob.type == RENDER &&
		m_currentJob.priority == CurrentPageRender && m_bAwaitingCurrentPageResult)
	{
		m_metrics.currentPageResultElapsedMs = now - m_dwCurrentPageRequest;
		m_bAwaitingCurrentPageResult = false;
	}
	m_currentJob = Job();
	m_bRejectCurrentJob = false;
	return notify;
}

void RenderScheduler::RejectCurrent()
{
	if (m_currentJob.IsActive())
		m_bRejectCurrentJob = true;
}

bool RenderScheduler::Reconcile(const JobWindows& windows)
{
	bool cancelPrefetches = false;
	for (list<Job>::iterator it = m_jobs.begin(); it != m_jobs.end(); )
	{
		const set<int>* pages = NULL;
		switch (it->type)
		{
		case RENDER: pages = &windows.renderPages; break;
		case DECODE: pages = &windows.decodePages; break;
		case PREFETCH_DECODE: pages = &windows.prefetchPages; break;
		case READINFO: pages = &windows.readInfoPages; break;
		}
		// cleanupPages contains requests made by this update, not a whitelist.
		// Once cache ownership has been released, cleanup must run unless the
		// page returns to an active/cache window before the worker reaches it.
		bool keep = it->type == CLEANUP ||
			(pages != NULL && pages->find(it->GetPage()) != pages->end());
		if (it->type == CLEANUP &&
			(windows.renderPages.find(it->GetPage()) != windows.renderPages.end() ||
			 windows.decodePages.find(it->GetPage()) != windows.decodePages.end() ||
			 windows.readInfoPages.find(it->GetPage()) != windows.readInfoPages.end()))
			keep = false;
		if (keep)
		{
			++it;
			continue;
		}
		const JobType type = it->type;
		if (type == PREFETCH_DECODE)
			cancelPrefetches = true;
		m_pages[it->GetPage()] = m_jobs.end();
		it = m_jobs.erase(it);
		if (type == RENDER || type == DECODE || type == PREFETCH_DECODE)
			++m_metrics.obsoleteJobsRemoved;
	}
	if (m_currentJob.IsActive() &&
		((m_currentJob.type == RENDER &&
			windows.renderPages.find(m_currentJob.GetPage()) == windows.renderPages.end()) ||
		 (m_currentJob.type == PREFETCH_DECODE &&
			windows.prefetchPages.find(m_currentJob.GetPage()) == windows.prefetchPages.end())))
	{
		if (!m_bRejectCurrentJob)
		{
			m_bRejectCurrentJob = true;
			++m_metrics.obsoleteJobsRejected;
		}
		if (m_currentJob.type == PREFETCH_DECODE)
			cancelPrefetches = true;
	}
	return cancelPrefetches;
}

bool RenderScheduler::Clear()
{
	bool cancelPrefetches = false;
	for (list<Job>::const_iterator it = m_jobs.begin(); it != m_jobs.end(); ++it)
	{
		if (it->type == RENDER || it->type == DECODE || it->type == PREFETCH_DECODE)
			++m_metrics.obsoleteJobsRemoved;
		if (it->type == PREFETCH_DECODE)
			cancelPrefetches = true;
	}
	m_jobs.clear();
	m_pages.assign(m_pages.size(), m_jobs.end());
	return cancelPrefetches;
}

bool RenderScheduler::IsPrefetchQueued(int nPage) const
{
	return nPage >= 0 && nPage < (int)m_pages.size() &&
		m_pages[nPage] != m_jobs.end() && m_pages[nPage]->type == PREFETCH_DECODE;
}

void RenderScheduler::GetQueuedJobCounts(int& render, int& decode, int& prefetchDecode) const
{
	render = decode = prefetchDecode = 0;
	for (list<Job>::const_iterator it = m_jobs.begin(); it != m_jobs.end(); ++it)
	{
		if (it->type == RENDER)
			++render;
		else if (it->type == DECODE)
			++decode;
		else if (it->type == PREFETCH_DECODE)
			++prefetchDecode;
	}
}

void RenderScheduler::GetQueuedJobInfo(vector<JobInfo>& jobs) const
{
	jobs.clear();
	for (list<Job>::const_iterator it = m_jobs.begin(); it != m_jobs.end(); ++it)
	{
		JobInfo info = { it->GetPage(), it->type, it->priority,
			it->type == RENDER ? it->request.size : CSize(0, 0) };
		jobs.push_back(info);
	}
}

bool RenderScheduler::GetCurrentJobInfo(JobInfo& job) const
{
	if (!m_currentJob.IsActive())
		return false;
	JobInfo current = { m_currentJob.GetPage(), m_currentJob.type,
		m_currentJob.priority, m_currentJob.type == RENDER ? m_currentJob.request.size : CSize(0, 0) };
	job = current;
	return true;
}

void RenderScheduler::ResetSubmittedJobCounts()
{
	m_metrics.submittedRenderJobs = m_metrics.submittedDecodeJobs =
		m_metrics.submittedPrefetchJobs = 0;
}

void RenderScheduler::GetSubmittedJobCounts(int& render, int& decode, int& prefetchDecode) const
{
	render = m_metrics.submittedRenderJobs;
	decode = m_metrics.submittedDecodeJobs;
	prefetchDecode = m_metrics.submittedPrefetchJobs;
}

void RenderScheduler::ResetMetrics()
{
	m_metrics.peakQueueLength = m_metrics.obsoleteJobsRemoved = m_metrics.obsoleteJobsRejected = 0;
	m_metrics.jobsExecutedBeforeCurrentPage = 0;
	m_metrics.currentPageResultElapsedMs = 0;
	m_dwCurrentPageRequest = 0;
	m_bAwaitingCurrentPageResult = false;
}

void RenderScheduler::RemoveFromQueue(int nPage)
{
	list<Job>::iterator it = m_pages[nPage];
	if (it != m_jobs.end())
	{
		m_jobs.erase(it);
		m_pages[nPage] = m_jobs.end();
	}
}

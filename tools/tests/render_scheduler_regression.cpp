// Runtime contract for the pure RenderScheduler queue policy.  This test does
// not create a DjVuSource, render bitmap, or worker thread.
#include "stdafx.h"
#include "RenderScheduler.h"

#include <stdio.h>

static int g_failures = 0;

static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		fprintf(stderr, "FAIL: %s\n", message);
		++g_failures;
	}
}

static RenderScheduler::Job RenderJob(int page, int width,
	RenderScheduler::JobPriority priority)
{
	RenderScheduler::Job job;
	job.request = RenderRequest(page, CSize(width, width + 10), 0, 0,
		CDisplaySettings());
	job.type = RenderScheduler::RENDER;
	job.priority = priority;
	return job;
}

static RenderScheduler::Job PageJob(int page, RenderScheduler::JobType type,
	RenderScheduler::JobPriority priority)
{
	RenderScheduler::Job job;
	job.nPage = page;
	job.type = type;
	job.priority = priority;
	return job;
}

static void TestPriorityAndQueuedIdentity()
{
	RenderScheduler scheduler(32);
	RenderScheduler::Job current = RenderJob(1, 100, RenderScheduler::CurrentPageRender);
	RenderScheduler::Job visible = RenderJob(2, 100, RenderScheduler::VisibleRender);
	RenderScheduler::Job decode = PageJob(3, RenderScheduler::DECODE, RenderScheduler::Decode);

	Check(scheduler.Submit(decode, 1), "decode accepted");
	Check(scheduler.Submit(visible, 2), "visible render accepted");
	Check(scheduler.Submit(current, 3), "current-page render accepted");
	Check(!scheduler.Submit(current, 4), "identical queued render deduplicated");

	RenderScheduler::Job job;
	Check(scheduler.TakeNext(job) && job.GetPage() == 1, "current-page render is first");
	Check(scheduler.CompleteCurrent(5), "current-page result accepted");
	Check(scheduler.TakeNext(job) && job.GetPage() == 2, "visible render is second");
	Check(scheduler.CompleteCurrent(6), "visible result accepted");
	Check(scheduler.TakeNext(job) && job.GetPage() == 3, "decode is after renders");
	Check(scheduler.CompleteCurrent(7), "decode completed");

	Check(scheduler.Submit(RenderJob(4, 100, RenderScheduler::Background), 8),
		"background render accepted");
	Check(scheduler.Submit(RenderJob(4, 150, RenderScheduler::VisibleRender), 9),
		"changed queued render identity replaces old request");
	vector<RenderScheduler::JobInfo> jobs;
	scheduler.GetQueuedJobInfo(jobs);
	Check(jobs.size() == 1 && jobs[0].nPage == 4 && jobs[0].priority == RenderScheduler::VisibleRender &&
		jobs[0].size == CSize(150, 160), "replacement retains only the new visible request");
}

static void TestVisibleFifo()
{
	RenderScheduler scheduler(8);
	for (int page = 1; page <= 3; ++page)
		Check(scheduler.Submit(RenderJob(page, 100, RenderScheduler::VisibleRender), page),
			"FIFO visible request accepted");
	RenderScheduler::Job job;
	for (int page = 1; page <= 3; ++page)
	{
		Check(scheduler.TakeNext(job) && job.GetPage() == page &&
			job.priority == RenderScheduler::VisibleRender, "equal-priority visible FIFO order");
		Check(scheduler.CompleteCurrent(10 + page), "FIFO visible result accepted");
	}
}

static void TestQueuedPromotion()
{
	RenderScheduler scheduler(16);
	Check(scheduler.Submit(RenderJob(7, 100, RenderScheduler::Background), 10),
		"background render queued before promotion");
	Check(scheduler.Submit(RenderJob(7, 100, RenderScheduler::VisibleRender), 20),
		"same queued identity promoted to visible");
	vector<RenderScheduler::JobInfo> jobs;
	scheduler.GetQueuedJobInfo(jobs);
	Check(jobs.size() == 1 && jobs[0].nPage == 7 &&
		jobs[0].priority == RenderScheduler::VisibleRender &&
		jobs[0].type == RenderScheduler::RENDER, "promotion leaves one visible render");
}

static void TestRenderToDecodeReplacement()
{
	RenderScheduler scheduler(16);
	Check(scheduler.Submit(RenderJob(7, 100, RenderScheduler::VisibleRender), 10),
		"render queued before decode replacement");
	Check(scheduler.Submit(PageJob(7, RenderScheduler::DECODE, RenderScheduler::Decode), 20),
		"decode replaces queued render even at lower priority");
	vector<RenderScheduler::JobInfo> jobs;
	scheduler.GetQueuedJobInfo(jobs);
	Check(jobs.size() == 1 && jobs[0].nPage == 7 &&
		jobs[0].type == RenderScheduler::DECODE, "render-to-decode leaves one semantic job");
	RenderScheduler::Job job;
	Check(scheduler.TakeNext(job) && job.type == RenderScheduler::DECODE &&
		job.GetPage() == 7, "replacement decode executes");
	Check(scheduler.CompleteCurrent(40), "replacement decode completes");
	Check(scheduler.GetQueuedJobCount() == 0, "render-to-decode queue drained");
}

static void TestRunningCurrentPagePromotion()
{
	RenderScheduler scheduler(16);
	RenderScheduler::Job job;
	RenderScheduler::JobInfo current;
	Check(scheduler.Submit(RenderJob(5, 100, RenderScheduler::VisibleRender), 100),
		"visible render queued before running promotion");
	Check(scheduler.TakeNext(job), "visible render starts before promotion");
	Check(!scheduler.Submit(RenderJob(5, 100, RenderScheduler::CurrentPageRender), 200),
		"same running identity promoted without duplicate");
	Check(scheduler.GetCurrentJobInfo(current) &&
		current.priority == RenderScheduler::CurrentPageRender,
		"running render acquires current-page priority");
	Check(scheduler.GetQueuedJobCount() == 0, "running promotion queues no replacement");
	Check(scheduler.GetMetrics().submittedRenderJobs == 1,
		"running promotion does not submit a duplicate render");
	Check(scheduler.CompleteCurrent(240), "promoted result accepted");
	Check(scheduler.GetMetrics().currentPageResultElapsedMs == 40,
		"promotion starts new current-page timing interval");

	Check(scheduler.Submit(RenderJob(6, 100, RenderScheduler::Background), 300),
		"background render queued before revival");
	Check(scheduler.TakeNext(job), "background render starts before revival");
	scheduler.RejectCurrent();
	Check(scheduler.IsCurrentJobRejected(), "background render initially rejected");
	Check(!scheduler.Submit(RenderJob(6, 100, RenderScheduler::CurrentPageRender), 400),
		"rejected same identity revived without duplicate");
	Check(!scheduler.IsCurrentJobRejected(), "current-page revival clears rejection");
	Check(scheduler.GetCurrentJobInfo(current) &&
		current.priority == RenderScheduler::CurrentPageRender,
		"revived render acquires current-page priority");
	Check(scheduler.GetQueuedJobCount() == 0, "revival queues no replacement");
	Check(scheduler.GetMetrics().submittedRenderJobs == 2,
		"revival does not submit a duplicate render");
	Check(scheduler.CompleteCurrent(425), "revived result accepted");
	Check(scheduler.GetMetrics().currentPageResultElapsedMs == 25,
		"revival restarts current-page timing interval");
}

static void TestExactMetrics()
{
	RenderScheduler scheduler(16);
	Check(scheduler.Submit(RenderJob(1, 100, RenderScheduler::VisibleRender), 10),
		"metrics render submitted");
	Check(scheduler.Submit(PageJob(2, RenderScheduler::DECODE, RenderScheduler::Decode), 20),
		"metrics decode submitted");
	Check(scheduler.Submit(PageJob(3, RenderScheduler::PREFETCH_DECODE,
		RenderScheduler::AdjacentPrefetch), 30), "metrics prefetch submitted");
	Check(scheduler.Submit(RenderJob(1, 150, RenderScheduler::VisibleRender), 40),
		"metrics obsolete queued render replaced");
	RenderScheduler::Metrics metrics = scheduler.GetMetrics();
	Check(metrics.submittedRenderJobs == 2, "submittedRenderJobs counts real submissions");
	Check(metrics.submittedDecodeJobs == 1, "submittedDecodeJobs counts real submissions");
	Check(metrics.submittedPrefetchJobs == 1, "submittedPrefetchJobs counts real submissions");
	Check(metrics.peakQueueLength == 3, "peakQueueLength counts queued jobs");
	Check(metrics.obsoleteJobsRemoved == 1, "obsoleteJobsRemoved counts replaced render");
	RenderScheduler::Job job;
	Check(scheduler.TakeNext(job) && job.GetPage() == 1, "metrics render starts");
	RenderScheduler::JobWindows windows;
	windows.decodePages.insert(2);
	windows.prefetchPages.insert(3);
	Check(!scheduler.Reconcile(windows), "rejecting render does not cancel retained prefetch");
	Check(scheduler.IsCurrentJobRejected(), "out-of-window render rejected");
	Check(!scheduler.Reconcile(windows), "repeated reconciliation retains useful work");
	metrics = scheduler.GetMetrics();
	Check(metrics.obsoleteJobsRejected == 1, "obsoleteJobsRejected counts running render once");
	Check(metrics.obsoleteJobsRemoved == 1, "retained decode and prefetch are not obsolete");
	Check(!scheduler.CompleteCurrent(50), "obsolete render result discarded");

	RenderScheduler timing(16);
	Check(timing.Submit(RenderJob(4, 100, RenderScheduler::CurrentPageRender), 100),
		"timing current-page request queued");
	Check(timing.Submit(PageJob(4, RenderScheduler::DECODE, RenderScheduler::Decode), 110),
		"timing request replaced by decode");
	Check(timing.TakeNext(job) && job.type == RenderScheduler::DECODE,
		"decode executes before an accepted current-page result");
	Check(timing.GetMetrics().jobsExecutedBeforeCurrentPage == 1,
		"jobsExecutedBeforeCurrentPage counts intervening work");
	Check(timing.CompleteCurrent(120), "intervening decode completes");
	Check(timing.Submit(RenderJob(5, 100, RenderScheduler::CurrentPageRender), 200),
		"new current-page timing request queued");
	Check(timing.GetMetrics().jobsExecutedBeforeCurrentPage == 0,
		"new current-page interval resets intervening job count");
	Check(timing.TakeNext(job) && job.GetPage() == 5, "timed current-page render starts");
	Check(timing.CompleteCurrent(245), "timed current-page result accepted");
	Check(timing.GetMetrics().currentPageResultElapsedMs == 45,
		"currentPageResultElapsedMs uses deterministic scheduler timestamps");
}

static void TestRunningRenderReentry()
{
	RenderScheduler scheduler(32);
	RenderScheduler::Job requestA = RenderJob(8, 100, RenderScheduler::CurrentPageRender);
	RenderScheduler::Job job;
	Check(scheduler.Submit(requestA, 10), "running request A accepted");
	Check(scheduler.TakeNext(job), "request A starts");
	scheduler.RejectCurrent();
	Check(scheduler.IsCurrentJobRejected(), "running A rejected");
	Check(!scheduler.Submit(RenderJob(8, 100, RenderScheduler::VisibleRender), 11),
		"same running identity is revived without duplicate");
	Check(!scheduler.IsCurrentJobRejected(), "same identity clears rejection");
	Check(scheduler.GetQueuedJobCount() == 0, "same identity does not queue replacement");
	Check(scheduler.CompleteCurrent(12), "revived A result accepted");

	Check(scheduler.Submit(requestA, 13), "second A request accepted");
	Check(scheduler.TakeNext(job), "second A starts");
	Check(scheduler.Submit(RenderJob(8, 200, RenderScheduler::VisibleRender), 14),
		"changed running identity queues replacement");
	Check(scheduler.IsCurrentJobRejected(), "changed identity rejects running result");
	Check(!scheduler.CompleteCurrent(15), "stale A result discarded");
	Check(scheduler.TakeNext(job) && job.request.size == CSize(200, 210),
		"replacement B runs next");
	Check(scheduler.CompleteCurrent(16), "replacement B result accepted");
}

static void TestMaintenanceAndReconciliation()
{
	RenderScheduler scheduler(32);
	RenderScheduler::Job job;
	Check(scheduler.Submit(RenderJob(12, 100, RenderScheduler::VisibleRender), 20), "render accepted");
	Check(scheduler.TakeNext(job), "render starts");
	Check(scheduler.Submit(PageJob(12, RenderScheduler::CLEANUP, RenderScheduler::Background), 21),
		"cleanup queues behind running render");
	Check(scheduler.CompleteCurrent(22), "render completes before cleanup");
	Check(scheduler.TakeNext(job) && job.type == RenderScheduler::CLEANUP,
		"cleanup executes after running render");
	Check(scheduler.CompleteCurrent(23), "cleanup completes");

	Check(scheduler.Submit(RenderJob(14, 100, RenderScheduler::VisibleRender), 24), "stale render accepted");
	Check(scheduler.Submit(PageJob(15, RenderScheduler::READINFO, RenderScheduler::Background), 25), "read-info accepted");
	Check(scheduler.Submit(PageJob(16, RenderScheduler::PREFETCH_DECODE, RenderScheduler::AdjacentPrefetch), 26), "prefetch accepted");
	RenderScheduler::JobWindows windows;
	windows.readInfoPages.insert(15);
	Check(scheduler.Reconcile(windows), "stale prefetch requests source cancellation");
	vector<RenderScheduler::JobInfo> jobs;
	scheduler.GetQueuedJobInfo(jobs);
	Check(jobs.size() == 1 && jobs[0].nPage == 15 && jobs[0].type == RenderScheduler::READINFO,
		"reconciliation retains only required read-info work");
	RenderScheduler::Metrics metrics = scheduler.GetMetrics();
	Check(metrics.obsoleteJobsRemoved >= 2, "obsolete render and prefetch are counted");

	Check(scheduler.Submit(PageJob(17, RenderScheduler::CLEANUP, RenderScheduler::Background), 27),
		"cleanup accepted for page leaving the cache");
	RenderScheduler::JobWindows reentered;
	reentered.decodePages.insert(17);
	reentered.readInfoPages.insert(15);
	Check(!scheduler.Reconcile(reentered), "re-entering page needs no prefetch cancellation");
	Check(scheduler.GetQueuedJobCount() == 1, "re-entry removes stale cleanup and retains existing read-info");
	scheduler.TakeNext(job);
	Check(job.type == RenderScheduler::READINFO && job.GetPage() == 15,
		"read-info remains the only queued work after cleanup cancellation");
	Check(scheduler.CompleteCurrent(28), "read-info completes");

	Check(scheduler.Submit(PageJob(18, RenderScheduler::PREFETCH_DECODE,
		RenderScheduler::AdjacentPrefetch), 29), "prefetch accepted before clear");
	Check(scheduler.Clear(), "clear reports external prefetch cancellation");
	Check(scheduler.GetQueuedJobCount() == 0, "clear removes every queued job");
}

int _tmain()
{
	TestPriorityAndQueuedIdentity();
	TestVisibleFifo();
	TestQueuedPromotion();
	TestRenderToDecodeReplacement();
	TestRunningCurrentPagePromotion();
	TestExactMetrics();
	TestRunningRenderReentry();
	TestMaintenanceAndReconciliation();
	if (g_failures != 0)
		return 1;
	puts("Render scheduler regression: PASS");
	return 0;
}

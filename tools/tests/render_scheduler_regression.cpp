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
	TestRunningRenderReentry();
	TestMaintenanceAndReconciliation();
	if (g_failures != 0)
		return 1;
	puts("Render scheduler regression: PASS");
	return 0;
}

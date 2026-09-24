#include "../../src/stdafx.h"
#include "../../src/RenderScheduler.h"
#include <stdio.h>

static bool Check(bool condition, const char* message)
{
	if (!condition) fprintf(stderr, "tile scheduler regression failed: %s\n", message);
	return condition;
}

static RenderScheduler::Job Tile(const RenderRequest& request, int column, int row,
	RenderScheduler::JobPriority priority)
{
	TileGrid grid(request.size.cx, request.size.cy);
	RenderScheduler::Job job;
	job.type = RenderScheduler::TILE_RENDER;
	job.tile = TileRequest(TileKey(request, grid.At(column, row), column, row));
	job.priority = priority;
	return job;
}

int main()
{
	bool passed = true;
	const RenderRequest a(10, CSize(1025, 1025), 0, 0, CDisplaySettings());
	RenderRequest b(a);
	b.rotation = 1;
	RenderScheduler queue(20);
	passed &= Check(queue.Submit(Tile(a, 0, 0, RenderScheduler::VisibleRender), 10), "submit tile 0");
	passed &= Check(queue.Submit(Tile(a, 1, 0, RenderScheduler::VisibleRender), 11), "submit tile 1");
	passed &= Check(queue.Submit(Tile(a, 2, 0, RenderScheduler::VisibleRender), 12), "submit tile 2");
	passed &= Check(!queue.Submit(Tile(a, 1, 0, RenderScheduler::VisibleRender), 13) &&
		queue.GetQueuedJobCount() == 3, "duplicate tile must not queue");
	RenderScheduler::Job current;
	for (int column = 0; column < 3; ++column)
	{
		passed &= Check(queue.TakeNext(current) && current.type == RenderScheduler::TILE_RENDER &&
			current.tile.key.column == column, "equal-priority tiles must be FIFO");
		passed &= Check(queue.CompleteCurrent(20 + column, column == 2), "current tile must remain valid");
	}
	passed &= Check(queue.GetQueuedJobCount() == 0, "FIFO queue drained");

	queue.Submit(Tile(a, 0, 0, RenderScheduler::VisibleRender), 30);
	queue.Submit(Tile(a, 1, 0, RenderScheduler::VisibleRender), 31);
	passed &= Check(queue.Submit(Tile(a, 1, 0, RenderScheduler::CurrentPageRender), 32),
		"queued tile priority promotion");
	passed &= Check(queue.TakeNext(current) && current.tile.key.column == 1 &&
		current.priority == RenderScheduler::CurrentPageRender, "current-page tile before visible tile");
	queue.CompleteCurrent(33, false);
	queue.TakeNext(current);
	queue.CompleteCurrent(34, true);
	RenderScheduler::Job prefetch;
	prefetch.type = RenderScheduler::PREFETCH_DECODE;
	prefetch.nPage = 3;
	prefetch.priority = RenderScheduler::AdjacentPrefetch;
	queue.Submit(prefetch, 35);
	queue.Submit(Tile(a, 0, 0, RenderScheduler::CurrentPageRender), 36);
	passed &= Check(queue.TakeNext(current) && current.type == RenderScheduler::TILE_RENDER &&
		current.priority == RenderScheduler::CurrentPageRender, "current-page tile precedes prefetch");
	queue.CompleteCurrent(37, true);
	queue.TakeNext(current);
	queue.CompleteCurrent(38);

	queue.Submit(Tile(a, 0, 0, RenderScheduler::VisibleRender), 40);
	queue.Submit(Tile(a, 1, 0, RenderScheduler::VisibleRender), 41);
	passed &= Check(queue.Submit(Tile(b, 0, 0, RenderScheduler::CurrentPageRender), 42) &&
		queue.GetQueuedJobCount() == 1, "changed request removes old queued tiles");
	queue.TakeNext(current);
	RenderScheduler::JobWindows windows;
	queue.Reconcile(windows);
	passed &= Check(queue.IsCurrentJobRejected() && !queue.CompleteCurrent(43),
		"running tile outside render window must be rejected");
	queue.Submit(Tile(a, 0, 0, RenderScheduler::VisibleRender), 44);
	queue.TakeNext(current);
	queue.Reconcile(windows);
	passed &= Check(queue.IsCurrentJobRejected() &&
		!queue.Submit(Tile(a, 0, 0, RenderScheduler::CurrentPageRender), 45) &&
		!queue.IsCurrentJobRejected() && queue.GetQueuedJobCount() == 0 &&
		queue.CompleteCurrent(46, true),
		"exact tile re-entry revives running job without duplicate");
	queue.Submit(Tile(a, 0, 0, RenderScheduler::VisibleRender), 47);
	queue.TakeNext(current);
	passed &= Check(queue.Submit(Tile(b, 0, 0, RenderScheduler::CurrentPageRender), 48) &&
		queue.IsCurrentJobRejected() && !queue.CompleteCurrent(49) &&
		queue.TakeNext(current) && current.tile.key.render == b &&
		queue.CompleteCurrent(50, true),
		"changed tile identity rejects stale current and runs replacement");

	queue.Submit(Tile(a, 0, 0, RenderScheduler::VisibleRender), 50);
	queue.Submit(Tile(a, 1, 0, RenderScheduler::VisibleRender), 51);
	queue.Reconcile(windows);
	passed &= Check(queue.GetQueuedJobCount() == 0, "reconciliation removes obsolete tiles");
	RenderScheduler::Metrics metrics = queue.GetMetrics();
	passed &= Check(metrics.obsoleteJobsRemoved >= 4 && metrics.obsoleteJobsRejected >= 1 &&
		metrics.submittedRenderJobs >= 7, "tile metrics count real submissions and stale work");

	TileGrid grid(1025, 1025);
	TileCompletion completion(grid);
	passed &= Check(grid.Count() == 9 && !completion.Complete(), "empty tile set incomplete");
	for (int row = 0; row < grid.Rows(); ++row)
		for (int column = 0; column < grid.Columns(); ++column)
			if (row != 2 || column != 2)
				passed &= Check(completion.Mark(column, row), "first tile completion accepted");
	passed &= Check(!completion.Complete() && completion.Remaining() == 1 &&
		!completion.Mark(0, 0) && !completion.Mark(3, 0),
		"missing/duplicate/out-of-grid tiles cannot complete batch");
	passed &= Check(completion.Mark(2, 2) && completion.Complete() &&
		!completion.Mark(2, 2), "complete set accepted exactly once");
	printf("Tile scheduler regression: %s\n", passed ? "PASS" : "FAIL");
	return passed ? 0 : 1;
}

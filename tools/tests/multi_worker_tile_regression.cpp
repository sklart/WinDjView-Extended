#include "../../src/stdafx.h"
#include "../../src/RenderScheduler.h"
#include <stdio.h>

static bool Check(bool ok, const char* message)
{
	if (!ok) fprintf(stderr, "multi-worker tile regression failed: %s\n", message);
	return ok;
}

static RenderScheduler::Job Tile(const RenderRequest& request, int column,
	RenderScheduler::JobPriority priority = RenderScheduler::CurrentPageRender)
{
	TileGrid grid(request.size.cx, request.size.cy);
	RenderScheduler::Job job;
	job.type = RenderScheduler::TILE_RENDER;
	job.tile = TileRequest(TileKey(request, grid.At(column, 0), column, 0));
	job.priority = priority;
	return job;
}

int main()
{
	bool ok = true;
	RenderRequest a(10, CSize(1536, 512), 0, 0, CDisplaySettings());
	RenderRequest b(a);
	b.rotation = 1;
	RenderScheduler scheduler(20);
	for (int x = 0; x < 3; ++x) ok &= Check(scheduler.Submit(Tile(a, x), x), "submit tile");
	RenderScheduler::Job primary, second, third;
	unsigned long long t2 = 0, t3 = 0;
	ok &= Check(scheduler.TakeNext(primary) && primary.tile.key.column == 0,
		"primary takes first tile");
	ok &= Check(scheduler.TakeNextParallelTile(second, t2) && second.tile.key.column == 1,
		"second worker takes shared-queue tile");
	ok &= Check(scheduler.TakeNextParallelTile(third, t3) && third.tile.key.column == 2 &&
		!scheduler.TakeNextParallelTile(third, t3), "bounded unique work distribution");
	ok &= Check(!scheduler.Submit(Tile(a, 1), 5) && scheduler.GetQueuedJobCount() == 0,
		"running tile deduplicated");
	TileCompletion completion(TileGrid(1536, 512));
	ok &= Check(scheduler.CompleteParallelTile(t3, 10, false) && completion.Mark(2, 0) &&
		!completion.Complete(), "out-of-order tile three");
	ok &= Check(scheduler.CompleteCurrent(11, false) && completion.Mark(0, 0) &&
		!completion.Complete(), "out-of-order tile one");
	ok &= Check(scheduler.CompleteParallelTile(t2, 12, true) && completion.Mark(1, 0) &&
		completion.Complete() && !completion.Mark(1, 0) &&
		!scheduler.CompleteParallelTile(t2, 13, true), "exactly-once completion");
	RenderScheduler::Job decode;
	decode.nPage = 11;
	decode.type = RenderScheduler::DECODE;
	decode.priority = RenderScheduler::Decode;
	scheduler.Submit(Tile(a, 0), 14);
	scheduler.TakeNextParallelTile(second, t2);
	scheduler.Submit(decode, 15);
	ok &= Check(!scheduler.CanTakeNext() &&
		!scheduler.TakeNextParallelTile(third, t3) &&
		scheduler.CompleteParallelTile(t2, 16, false) &&
		scheduler.CanTakeNext() && scheduler.TakeNext(primary) &&
		primary.type == RenderScheduler::DECODE &&
		scheduler.CompleteCurrent(17), "decode stays serial behind active tiles");

	scheduler.Submit(Tile(a, 0), 20);
	scheduler.Submit(Tile(a, 1), 21);
	ok &= Check(scheduler.TakeNextParallelTile(second, t2), "start old request");
	ok &= Check(scheduler.Submit(Tile(b, 0), 22) &&
		scheduler.IsParallelTileRejected(t2) && scheduler.GetQueuedJobCount() == 1 &&
		!scheduler.CompleteParallelTile(t2, 23, false), "new identity rejects stale tile");
	ok &= Check(scheduler.TakeNextParallelTile(second, t2) &&
		second.tile.key.render == b, "replacement runs next");
	RenderScheduler::JobWindows windows;
	scheduler.Reconcile(windows);
	ok &= Check(scheduler.IsParallelTileRejected(t2) &&
		!scheduler.CompleteParallelTile(t2, 24, false), "reconciliation rejects running tile");

	RenderScheduler::Job firstA = Tile(a, 0);
	firstA.batchGeneration = 1;
	RenderScheduler::Job middleB = Tile(b, 0);
	middleB.batchGeneration = 2;
	RenderScheduler::Job latestA = Tile(a, 0);
	latestA.batchGeneration = 3;
	scheduler.Submit(firstA, 25);
	scheduler.TakeNextParallelTile(second, t2);
	scheduler.Submit(middleB, 26);
	ok &= Check(scheduler.Submit(latestA, 27) &&
		scheduler.IsParallelTileRejected(t2) &&
		scheduler.GetQueuedJobCount() == 1 &&
		!scheduler.CompleteParallelTile(t2, 28, false) &&
		scheduler.TakeNextParallelTile(second, t2) &&
		second.batchGeneration == 3 &&
		scheduler.CompleteParallelTile(t2, 29, true),
		"A-B-A must not revive a tile from an earlier batch");

	for (int x = 0; x < 3; ++x) scheduler.Submit(Tile(a, x), 30 + x);
	scheduler.TakeNext(primary);
	scheduler.TakeNextParallelTile(second, t2);
	scheduler.CancelTileSiblings(10, 0);
	ok &= Check(scheduler.GetQueuedJobCount() == 0 &&
		scheduler.IsParallelTileRejected(t2) && !scheduler.CompleteParallelTile(t2, 35, false) &&
		scheduler.CompleteCurrent(36, true), "single fallback owner cancels siblings");

	scheduler.Submit(Tile(a, 0), 40);
	scheduler.TakeNextParallelTile(second, t2);
	scheduler.RejectParallelTiles();
	ok &= Check(scheduler.IsParallelTileRejected(t2) &&
		!scheduler.CompleteParallelTile(t2, 41, false), "shutdown rejects active tile");
	RenderScheduler::Metrics metrics = scheduler.GetMetrics();
	ok &= Check(metrics.obsoleteJobsRejected >= 4 && metrics.obsoleteJobsRemoved >= 2,
		"stale work metrics are production scheduler counts");
	printf("Multi-worker tile scheduler regression: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}

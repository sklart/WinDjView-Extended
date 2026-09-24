// Pure page-window policy regression: no view, source, worker, or bitmap.
#include "stdafx.h"
#include "PageWorkingSet.h"

#include <stdio.h>

static int failures = 0;
static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		fprintf(stderr, "page-working-set regression failed: %s\n", message);
		++failures;
	}
}

static bool Contains(const std::vector<int>& pages, int page)
{
	return std::find(pages.begin(), pages.end(), page) != pages.end();
}

static PageWorkingSet::PageFacts Facts(bool decoded = true)
{
	PageWorkingSet::PageFacts facts;
	facts.decoded = decoded;
	facts.updateImages = true;
	facts.bitmapWidth = 800;
	facts.bitmapHeight = 1000;
	return facts;
}

static void TestSingleAndFacing()
{
	std::set<int> observed;
	observed.insert(10);
	std::vector<PageWorkingSet::Visit> visits;
	const std::function<PageWorkingSet::PageRange(int)> range =
		[](int page) { return PageWorkingSet::PageRange(page * 1000, (page + 1) * 1000); };
	PageWorkingSet::BuildVisits(PageWorkingSet::SinglePage, 500, 250, 250, 250, 250,
		0, 900, observed, range, visits);
	Check(!visits.empty() && visits.front().page == 10 && visits[1].page == 0 &&
		visits[2].page == 499 && visits.back().page == 250,
		"single visit order releases old observers before edges and nearby pages");
	Check(visits.size() == 24, "single traversal remains bounded for 500 pages");
	PageWorkingSet::PageFacts facts = Facts();
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 250, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Render, "single current page is rendered");
	facts.bitmapPresent = true;
	facts.reusableBitmap = true;
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 250, 500, 250, 0, 900, facts) ==
		PageWorkingSet::ReuseBitmap, "reusable visible bitmap suppresses a render job");
	facts = Facts(false);
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 250, 500, 250, 0, 900, facts) ==
		PageWorkingSet::ReadInfo, "undecoded current page requests read-info");
	facts = Facts();
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 252, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Render, "small nearby page is in the render window");
	facts.bitmapWidth = 4000;
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 252, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Decode, "large page two away is decode-only");
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 260, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Decode, "single ten-page decode window is unchanged");
	facts.sourceCached = true;
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 10, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Cleanup, "distant observed page gets cleanup");
	PageWorkingSet::WorkingSet result;
	PageWorkingSet::RecordAction(250, PageWorkingSet::Render, result);
	PageWorkingSet::RecordAction(260, PageWorkingSet::Decode, result);
	PageWorkingSet::RecordAction(10, PageWorkingSet::Cleanup, result);
	PageWorkingSet::RecordAction(251, PageWorkingSet::ReadInfo, result);
	Check(result.renderPages.count(250) == 1 && result.decodePages.count(260) == 1 &&
		result.cleanupPages.count(10) == 1 && result.readInfoPages.count(251) == 1 &&
		Contains(result.removeObserved, 10) && !Contains(result.addObserved, 251) &&
		Contains(result.addObserved, 250), "job windows and observer deltas remain distinct");

	facts = Facts();
	Check(PageWorkingSet::Classify(PageWorkingSet::Facing, 255, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Render, "facing small-page render window extends five pages ahead");
	facts.bitmapWidth = 2000;
	Check(PageWorkingSet::Classify(PageWorkingSet::Facing, 255, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Decode, "facing large page outside close render window is decode-only");
	Check(PageWorkingSet::Classify(PageWorkingSet::Facing, 253, 500, 250, 0, 900, facts) ==
		PageWorkingSet::Render, "facing close large page still renders");
}

static void TestContinuous(PageWorkingSet::Layout layout)
{
	std::set<int> observed;
	observed.insert(8);
	std::vector<PageWorkingSet::Visit> visits;
	const std::function<PageWorkingSet::PageRange(int)> range =
		[](int page) { return PageWorkingSet::PageRange(page * 1000, (page + 1) * 1000); };
	PageWorkingSet::BuildVisits(layout, 4096, 3500, 3500, 3501, 3500,
		3500000, 900, observed, range, visits);
	Check(!visits.empty() && visits.front().page == 8 && visits.back().page == 3500 &&
		visits.back().foreground, "continuous distant jump releases old observer and repeats foreground");
	Check(visits.size() < 64, "continuous traversal is bounded for 4096 pages");
	PageWorkingSet::PageFacts facts = Facts();
	facts.displayTop = 3500000;
	facts.displayBottom = 3501000;
	Check(PageWorkingSet::Classify(layout, 3500, 4096, 3500, 3500000, 900, facts) ==
		PageWorkingSet::Render, "continuous visible page renders");
	facts.displayTop = 3509000;
	facts.displayBottom = 3510000;
	Check(PageWorkingSet::Classify(layout, 3509, 4096, 3500, 3500000, 900, facts) ==
		PageWorkingSet::Decode, "continuous page within ten-screen cache window decodes");
	facts.displayTop = 8000;
	facts.displayBottom = 9000;
	facts.sourceCached = true;
	Check(PageWorkingSet::Classify(layout, 8, 4096, 3500, 3500000, 900, facts) ==
		PageWorkingSet::Cleanup, "continuous distant old page is cleaned up");
}

static void TestPrefetchAndEdges()
{
	int next, previous;
	PageWorkingSet::AdjacentPages(PageWorkingSet::SinglePage, 20, 20, 20,
		false, next, previous);
	Check(next == 21 && previous == 19, "single next/previous adjacent order");
	PageWorkingSet::AdjacentPages(PageWorkingSet::Facing, 20, 20, 20,
		true, next, previous);
	Check(next == 22 && previous == 19, "facing pair advances speculative next page");
	PageWorkingSet::AdjacentPages(PageWorkingSet::ContinuousFacing, 20, 18, 23,
		false, next, previous);
	Check(next == 24 && previous == 17, "continuous facing uses visible page range");
	PageWorkingSet::WorkingSet result;
	result.removeObserved.push_back(24);
	Check(PageWorkingSet::RecordPrefetchPage(24, false, result) &&
		result.prefetchPages.count(24) == 1 && Contains(result.addObserved, 24) &&
		!Contains(result.removeObserved, 24), "prefetch retains adjacent observed page");
	Check(!PageWorkingSet::RecordPrefetchPage(17, false, result),
		"legacy facing-parity rule skips an invalid adjacent page");
	Check(PageWorkingSet::RecordPrefetchPage(17, true, result),
		"first-page-alone parity rule accepts the alternate page");
	std::vector<PageWorkingSet::Visit> visits;
	const std::function<PageWorkingSet::PageRange(int)> range =
		[](int page) { return PageWorkingSet::PageRange(page, page + 1); };
	PageWorkingSet::BuildVisits(PageWorkingSet::SinglePage, 1, 0, 0, 0, 0, 0, 900,
		std::set<int>(), range, visits);
	Check(visits.size() == 1 && visits[0].page == 0,
		"one-page document visits only its current page");
	PageWorkingSet::BuildVisits(PageWorkingSet::Facing, 2, 0, 0, 0, 0, 0, 900,
		std::set<int>(), range, visits);
	Check(visits.size() == 2 && visits[0].page == 1 && visits[1].page == 0,
		"two-page facing document keeps outer-to-current order");
	PageWorkingSet::PageFacts edge = Facts();
	Check(PageWorkingSet::Classify(PageWorkingSet::SinglePage, 0, 2, 1, 0, 900, edge) ==
		PageWorkingSet::Render, "first page stays in a two-page render window");
	Check(PageWorkingSet::InDecodeWindow(PageWorkingSet::SinglePage, 1, 2, 0, 0, 900, edge),
		"last document page remains in the decode window");
}

int _tmain()
{
	TestSingleAndFacing();
	TestContinuous(PageWorkingSet::Continuous);
	TestContinuous(PageWorkingSet::ContinuousFacing);
	TestPrefetchAndEdges();
	if (failures != 0) return 1;
	puts("Page working set regression: PASS");
	return 0;
}

// WinDjView Extended - page-window selection without rendering or source access.
#include "stdafx.h"
#include "PageWorkingSet.h"

#include <algorithm>
#include <cstdlib>

void PageWorkingSet::BuildVisits(Layout layout, int pageCount, int currentPage,
	int topPage, int bottomPage, int foregroundPage, int scrollTop, int viewportHeight,
	const std::set<int>& observedPages,
	const std::function<PageRange(int)>& rangeAt, std::vector<Visit>& visits)
{
	visits.clear();
	if (pageCount <= 0)
		return;
	if (layout == SinglePage || layout == Facing)
	{
		std::set<int> desired;
		desired.insert(0);
		desired.insert(pageCount - 1);
		for (int page = max(0, currentPage - 10); page <= min(pageCount - 1, currentPage + 10); ++page)
			desired.insert(page);
		for (std::set<int>::const_iterator it = observedPages.begin(); it != observedPages.end(); ++it)
			if (desired.find(*it) == desired.end()) visits.push_back(Visit(*it));
		if (std::abs(currentPage) > 10) visits.push_back(Visit(0));
		if (pageCount > 1 && std::abs(pageCount - 1 - currentPage) > 10)
			visits.push_back(Visit(pageCount - 1));
		for (int diff = 10; diff >= 0; --diff)
		{
			if (currentPage - diff >= 0) visits.push_back(Visit(currentPage - diff));
			if (currentPage + diff < pageCount && diff != 0)
				visits.push_back(Visit(currentPage + diff));
		}
		return;
	}

	int cacheTop = topPage, cacheBottom = bottomPage;
	const int cacheTopLimit = scrollTop - 10 * viewportHeight;
	const int cacheBottomLimit = scrollTop + 11 * viewportHeight;
	while (cacheTop > 0 && rangeAt(cacheTop - 1).bottom > cacheTopLimit) --cacheTop;
	while (cacheBottom + 1 < pageCount && rangeAt(cacheBottom + 1).top < cacheBottomLimit) ++cacheBottom;
	std::set<int> desired;
	desired.insert(0);
	desired.insert(pageCount - 1);
	for (int page = cacheTop; page <= cacheBottom; ++page) desired.insert(page);
	for (std::set<int>::const_iterator it = observedPages.begin(); it != observedPages.end(); ++it)
		if (desired.find(*it) == desired.end()) visits.push_back(Visit(*it));
	if (cacheTop > 0) visits.push_back(Visit(0));
	if (cacheBottom < pageCount - 1) visits.push_back(Visit(pageCount - 1));
	for (int page = cacheTop; page < topPage; ++page) visits.push_back(Visit(page));
	for (int page = cacheBottom; page > bottomPage; --page) visits.push_back(Visit(page));
	for (int page = bottomPage; page >= topPage; --page) visits.push_back(Visit(page));
	// Preserve the deliberate second visit to the largest visible page.
	visits.push_back(Visit(foregroundPage, true));
}

bool PageWorkingSet::InRenderWindow(Layout layout, int page, int pageCount,
	int currentPage, int scrollTop, int viewportHeight, const PageFacts& facts)
{
	if (page < 0 || page >= pageCount)
		return false;
	if (layout == SinglePage)
	{
		const long area = facts.bitmapWidth * facts.bitmapHeight;
		return (area < 3000000 && std::abs(page - currentPage) <= 2) ||
			std::abs(page - currentPage) <= 1;
	}
	if (layout == Facing)
	{
		const long area = facts.bitmapWidth * facts.bitmapHeight;
		return (area < 1500000 && page >= currentPage - 4 && page <= currentPage + 5) ||
			(page >= currentPage - 2 && page <= currentPage + 3);
	}
	return facts.displayTop < scrollTop + 3 * viewportHeight &&
		facts.displayBottom > scrollTop - 2 * viewportHeight;
}

bool PageWorkingSet::InDecodeWindow(Layout layout, int page, int pageCount,
	int currentPage, int scrollTop, int viewportHeight, const PageFacts& facts)
{
	if (page < 0 || page >= pageCount)
		return false;
	if (layout == SinglePage || layout == Facing)
		return std::abs(page - currentPage) <= 10 || page == 0 || page == pageCount - 1;
	return (facts.displayTop < scrollTop + 11 * viewportHeight &&
		facts.displayBottom > scrollTop - 10 * viewportHeight) ||
		page == 0 || page == pageCount - 1;
}

PageWorkingSet::Action PageWorkingSet::Classify(Layout layout, int page, int pageCount,
	int currentPage, int scrollTop, int viewportHeight, const PageFacts& facts)
{
	if (!facts.decoded)
		return facts.magnify ? NoAction : ReadInfo;
	if (InRenderWindow(layout, page, pageCount, currentPage, scrollTop, viewportHeight, facts))
		return !facts.bitmapPresent || (!facts.reusableBitmap && facts.updateImages) ? Render : ReuseBitmap;
	if (!facts.magnify && InDecodeWindow(layout, page, pageCount, currentPage, scrollTop, viewportHeight, facts))
		return Decode;
	return facts.sourceCached ? Cleanup : Remove;
}

void PageWorkingSet::RecordAction(int page, Action action, WorkingSet& result)
{
	switch (action)
	{
	case ReadInfo: result.readInfoPages.insert(page); break;
	case Render: result.renderPages.insert(page); result.addObserved.push_back(page); break;
	case ReuseBitmap: result.addObserved.push_back(page); break;
	case Decode: result.decodePages.insert(page); result.addObserved.push_back(page); break;
	case Cleanup: result.cleanupPages.insert(page); result.removeObserved.push_back(page); break;
	case Remove: result.removeObserved.push_back(page); break;
	default: break;
	}
}

void PageWorkingSet::AdjacentPages(Layout layout, int currentPage,
	int firstVisible, int lastVisible, bool hasFacingPage, int& next, int& previous)
{
	int first = currentPage, last = currentPage;
	if (layout == Facing && hasFacingPage) last = currentPage + 1;
	else if (layout == Continuous || layout == ContinuousFacing)
		first = firstVisible, last = lastVisible;
	next = last + 1;
	previous = first - 1;
}

bool PageWorkingSet::RecordPrefetchPage(int page, bool firstPageAlone, WorkingSet& result)
{
	// Match the legacy IsValidPage parity rule used by AddPrefetchPage.
	if (firstPageAlone ? (page != 0 && page % 2 != 1) : page % 2 != 0)
		return false;
	result.removeObserved.erase(std::remove(result.removeObserved.begin(),
		result.removeObserved.end(), page), result.removeObserved.end());
	if (std::find(result.addObserved.begin(), result.addObserved.end(), page) == result.addObserved.end())
		result.addObserved.push_back(page);
	result.prefetchPages.insert(page);
	return true;
}

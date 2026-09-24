// WinDjView Extended - page-window selection without rendering or source access.
#pragma once

#include <functional>
#include <set>
#include <vector>

class PageWorkingSet
{
public:
	enum Layout { SinglePage = 0, Continuous = 1, Facing = 2, ContinuousFacing = 3 };
	enum Action { NoAction, ReadInfo, Render, ReuseBitmap, Decode, Cleanup, Remove };

	struct PageRange
	{
		PageRange(int top_ = 0, int bottom_ = 0) : top(top_), bottom(bottom_) {}
		int top, bottom;
	};
	struct Visit
	{
		Visit(int page_, bool foreground_ = false) : page(page_), foreground(foreground_) {}
		int page;
		bool foreground;
	};
	struct PageFacts
	{
		PageFacts() : decoded(false), magnify(false), bitmapPresent(false),
			reusableBitmap(false), updateImages(false), sourceCached(false),
			bitmapWidth(0), bitmapHeight(0), displayTop(0), displayBottom(0) {}
		bool decoded, magnify, bitmapPresent, reusableBitmap, updateImages, sourceCached;
		int bitmapWidth, bitmapHeight, displayTop, displayBottom;
	};
	struct WorkingSet
	{
		std::set<int> renderPages, decodePages, prefetchPages, readInfoPages, cleanupPages;
		std::vector<int> addObserved, removeObserved;
	};

	static void BuildVisits(Layout layout, int pageCount, int currentPage,
		int topPage, int bottomPage, int foregroundPage, int scrollTop, int viewportHeight,
		const std::set<int>& observedPages,
		const std::function<PageRange(int)>& rangeAt, std::vector<Visit>& visits);
	static bool InRenderWindow(Layout layout, int page, int pageCount, int currentPage,
		int scrollTop, int viewportHeight, const PageFacts& facts);
	static bool InDecodeWindow(Layout layout, int page, int pageCount, int currentPage,
		int scrollTop, int viewportHeight, const PageFacts& facts);
	static Action Classify(Layout layout, int page, int pageCount, int currentPage,
		int scrollTop, int viewportHeight, const PageFacts& facts);
	static void RecordAction(int page, Action action, WorkingSet& result);
	static void AdjacentPages(Layout layout, int currentPage,
		int firstVisible, int lastVisible, bool hasFacingPage, int& next, int& previous);
	static bool RecordPrefetchPage(int page, bool firstPageAlone, WorkingSet& result);
};

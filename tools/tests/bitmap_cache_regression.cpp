// Pure BitmapCache metadata/policy regression; no CDIB or CDjVuView instance.
#include "stdafx.h"
#include "BitmapCache.h"

#include <stdio.h>

static int failures = 0;

static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		fprintf(stderr, "bitmap-cache regression failed: %s\n", message);
		++failures;
	}
}

static RenderRequest Request(int page, int width = 800)
{
	return RenderRequest(page, CSize(width, 1000), 0, 0, CDisplaySettings());
}

static void TestIdentityAndCounters()
{
	BitmapCache cache;
	const RenderRequest request = Request(1);
	cache.Register(request, 100);
	Check(cache.Touch(1, request), "exact request is reusable");
	cache.RecordHit();
	Check(!cache.Touch(2, Request(2)), "different page is a miss");
	Check(!cache.Touch(1, Request(1, 801)), "different size is a miss");
	RenderRequest changed = request;
	changed.rotation = 1;
	Check(!cache.Touch(1, changed), "different rotation is a miss");
	changed = request;
	changed.displayMode = 1;
	Check(!cache.Touch(1, changed), "different display mode is a miss");
	changed = request;
	changed.displaySettings.bInvertColors = !changed.displaySettings.bInvertColors;
	Check(!cache.Touch(1, changed), "different display settings are a miss");
	cache.RecordMiss();
	int hits, misses, evictions;
	cache.GetCounters(hits, misses, evictions);
	Check(hits == 1 && misses == 1 && evictions == 0,
		"view-recorded hits and misses retain their legacy meaning");
	cache.ResetCounters();
	cache.GetCounters(hits, misses, evictions);
	Check(hits == 0 && misses == 0 && evictions == 0, "counters reset without removing entries");
	Check(cache.GetCount() == 1, "counter reset retains metadata");
}

static void TestLruAndPinning()
{
	BitmapCache cache;
	const __int64 large = 40LL * 1024 * 1024;
	cache.Register(Request(1), large);
	cache.Register(Request(2), large);
	Check(cache.Touch(1, Request(1)), "page 1 becomes most recently used");
	std::set<int> pinned;
	Check(cache.NeedsPrune() && cache.ChooseVictim(pinned) == 2,
		"least recently used unpinned page is evicted first");
	pinned.insert(2);
	Check(cache.ChooseVictim(pinned) == 1, "pinning excludes the LRU page");
	cache.Remove(2);
	cache.RecordEviction();
	Check(!cache.NeedsPrune() && cache.GetBytes() == large,
		"eviction restores the byte budget");
}

static void TestLimits()
{
	BitmapCache cache;
	std::set<int> pinned;
	for (int page = 0; page <= BitmapCache::MaxEntries; ++page)
		cache.Register(Request(page), 1);
	Check(cache.NeedsPrune() && cache.ChooseVictim(pinned) == 0,
		"17 entries choose oldest entry as victim");
	cache.Remove(0);
	cache.RecordEviction();
	Check(cache.GetCount() == BitmapCache::MaxEntries && !cache.NeedsPrune(),
		"16-entry limit is restored");

	BitmapCache bytes;
	bytes.Register(Request(1), 40LL * 1024 * 1024);
	bytes.Register(Request(2), 40LL * 1024 * 1024);
	Check(bytes.GetBytes() > BitmapCache::MaxBytes() && bytes.ChooseVictim(pinned) == 1,
		"64 MiB limit selects the older bitmap");
}

static void TestOversizedPinnedBitmap()
{
	BitmapCache cache;
	std::set<int> pinned;
	pinned.insert(0);
	cache.Register(Request(0), 70LL * 1024 * 1024);
	Check(cache.NeedsPrune() && cache.ChooseVictim(pinned) == -1,
		"oversized displayed bitmap does not self-evict");
	cache.Register(Request(1), 1024);
	Check(cache.ChooseVictim(pinned) == 1,
		"invisible bitmap is evicted ahead of oversized displayed bitmap");
	cache.Remove(1);
	cache.RecordEviction();
	Check(cache.GetCount() == 1 && cache.GetBytes() > BitmapCache::MaxBytes() &&
		cache.ChooseVictim(pinned) == -1, "oversized pinned bitmap remains reusable alone");
}

static void TestReplacementAndStorage()
{
	BitmapCache cache;
	cache.Register(Request(4), 100);
	cache.Register(Request(4, 801), 200);
	Check(cache.GetCount() == 1 && cache.GetBytes() == 200 &&
		!cache.HasIdentity(4, Request(4)) && cache.HasIdentity(4, Request(4, 801)),
		"replacement updates identity and bytes without a stale entry");
	cache.Remove(4);
	cache.Remove(4);
	Check(cache.GetCount() == 0 && cache.GetBytes() == 0,
		"removal is idempotent and immediately releases accounting");
	Check(BitmapCache::StorageBytes(1, 7, 24) == 28,
		"24-bit rows include DWORD alignment");
	Check(BitmapCache::StorageBytes(1, 7, 32) == 28,
		"32-bit rows use actual aligned stride");
	Check(BitmapCache::StorageBytes(2, 1, 24) == 8,
		"unaligned 24-bit width rounds up to eight bytes");
	Check(BitmapCache::StorageBytes(0, 7, 24) == 0,
		"invalid bitmap dimensions have zero storage");
	const __int64 largest = 0x7fffffffffffffffLL;
	Check(BitmapCache::StorageBytes(0x7fffffff, 0x7fffffff, 32) == largest,
		"DIB multiplication saturates instead of overflowing");
	cache.Register(Request(1), largest);
	cache.Register(Request(2), 100);
	Check(cache.GetBytes() == largest, "aggregate bytes saturate without overflow");
	cache.Remove(1);
	Check(cache.GetBytes() == 100, "removal recalculates a previously saturated total");
}

static void TestClear()
{
	BitmapCache cache;
	cache.Register(Request(1), 100);
	cache.Register(Request(2), 200);
	cache.Register(Request(3), 300);
	cache.RecordHit();
	cache.RecordMiss();
	cache.RecordEviction();
	cache.Clear();
	Check(cache.GetCount() == 0 && cache.GetBytes() == 0,
		"Clear removes every metadata entry and retained byte");
	Check(!cache.HasIdentity(1, Request(1)) && !cache.HasIdentity(2, Request(2)) &&
		!cache.HasIdentity(3, Request(3)) && !cache.Touch(1, Request(1)),
		"Clear forgets old render identities");
	std::vector<int> pages;
	cache.GetPages(pages);
	Check(pages.empty(), "Clear leaves no registered pages");
	int hits, misses, evictions;
	cache.GetCounters(hits, misses, evictions);
	Check(hits == 1 && misses == 1 && evictions == 1,
		"Clear preserves cumulative counters until ResetCounters");
}

int _tmain()
{
	TestIdentityAndCounters();
	TestLruAndPinning();
	TestLimits();
	TestOversizedPinnedBitmap();
	TestReplacementAndStorage();
	TestClear();
	if (failures != 0)
		return 1;
	puts("Bitmap cache regression: PASS");
	return 0;
}

// WinDjView Extended - metadata-only bitmap reuse and eviction policy.
#include "stdafx.h"
#include "BitmapCache.h"

BitmapCache::BitmapCache()
	: m_bytes(0), m_clock(0), m_hits(0), m_misses(0), m_evictions(0)
{
}

__int64 BitmapCache::StorageBytes(int width, int height, int bitsPerPixel)
{
	if (width <= 0 || height <= 0 || bitsPerPixel <= 0)
		return 0;
	const unsigned __int64 bitsPerLine = (unsigned __int64)width * bitsPerPixel;
	const unsigned __int64 stride = ((bitsPerLine + 31) & ~31ULL) / 8;
	const unsigned __int64 rows = (unsigned __int64)height;
	if (stride > 0x7fffffffffffffffULL / rows)
		return 0x7fffffffffffffffLL;
	return (__int64)(stride * rows);
}

void BitmapCache::RecalculateBytes()
{
	m_bytes = 0;
	for (std::map<int, Entry>::const_iterator it = m_entries.begin(); it != m_entries.end(); ++it)
	{
		const __int64 bytes = it->second.bytes;
		if (bytes > 0x7fffffffffffffffLL - m_bytes)
		{
			m_bytes = 0x7fffffffffffffffLL;
			break;
		}
		m_bytes += bytes;
	}
}

void BitmapCache::Register(const RenderRequest& request, __int64 bytes)
{
	if (request.page < 0)
		return;
	m_entries[request.page] = Entry(request, bytes > 0 ? bytes : 0, ++m_clock);
	RecalculateBytes();
}

void BitmapCache::Remove(int page)
{
	if (m_entries.erase(page) != 0)
		RecalculateBytes();
}

void BitmapCache::Clear()
{
	m_entries.clear();
	m_bytes = 0;
}

bool BitmapCache::HasIdentity(int page, const RenderRequest& request) const
{
	std::map<int, Entry>::const_iterator it = m_entries.find(page);
	return it != m_entries.end() && it->second.request == request;
}

bool BitmapCache::Touch(int page, const RenderRequest& request)
{
	std::map<int, Entry>::iterator it = m_entries.find(page);
	if (it == m_entries.end() || it->second.request != request)
		return false;
	it->second.lastUsed = ++m_clock;
	return true;
}

void BitmapCache::GetPages(std::vector<int>& pages) const
{
	pages.clear();
	for (std::map<int, Entry>::const_iterator it = m_entries.begin(); it != m_entries.end(); ++it)
		pages.push_back(it->first);
}

bool BitmapCache::NeedsPrune() const
{
	return GetCount() > MaxEntries || m_bytes > MaxBytes();
}

int BitmapCache::ChooseVictim(const std::set<int>& pinnedPages) const
{
	if (!NeedsPrune())
		return -1;
	int victim = -1;
	unsigned __int64 oldest = ~0ULL;
	for (std::map<int, Entry>::const_iterator it = m_entries.begin(); it != m_entries.end(); ++it)
	{
		if (pinnedPages.find(it->first) == pinnedPages.end() && it->second.lastUsed < oldest)
		{
			victim = it->first;
			oldest = it->second.lastUsed;
		}
	}
	return victim;
}

void BitmapCache::GetCounters(int& hits, int& misses, int& evictions) const
{
	hits = m_hits;
	misses = m_misses;
	evictions = m_evictions;
}

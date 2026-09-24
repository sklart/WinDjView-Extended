// WinDjView Extended - metadata-only bitmap reuse and eviction policy.
#pragma once

#include "RenderRequest.h"

#include <map>
#include <set>
#include <vector>

// BitmapCache never owns a CDIB. The caller registers a bitmap after taking
// ownership and destroys the corresponding CDIB when Remove/ChooseVictim says
// its metadata is no longer retained.
class BitmapCache
{
public:
	enum { MaxEntries = 16 };
	static __int64 MaxBytes() { return 64LL * 1024 * 1024; }
	static __int64 StorageBytes(int width, int height, int bitsPerPixel);

	BitmapCache();
	void Register(const RenderRequest& request, __int64 bytes);
	void Remove(int page);
	bool HasIdentity(int page, const RenderRequest& request) const;
	bool Touch(int page, const RenderRequest& request);
	void GetPages(std::vector<int>& pages) const;
	bool NeedsPrune() const;
	int ChooseVictim(const std::set<int>& pinnedPages) const;
	int GetCount() const { return (int)m_entries.size(); }
	__int64 GetBytes() const { return m_bytes; }

	void RecordHit() { ++m_hits; }
	void RecordMiss() { ++m_misses; }
	void RecordEviction() { ++m_evictions; }
	void ResetCounters() { m_hits = m_misses = m_evictions = 0; }
	void GetCounters(int& hits, int& misses, int& evictions) const;

private:
	struct Entry
	{
		Entry() : bytes(0), lastUsed(0) {}
		Entry(const RenderRequest& request_, __int64 bytes_, unsigned __int64 lastUsed_)
			: request(request_), bytes(bytes_), lastUsed(lastUsed_) {}
		RenderRequest request;
		__int64 bytes;
		unsigned __int64 lastUsed;
	};
	void RecalculateBytes();

	std::map<int, Entry> m_entries;
	__int64 m_bytes;
	unsigned __int64 m_clock;
	int m_hits, m_misses, m_evictions;
};

#pragma once

#include "RenderRequest.h"
#include "TileGeometry.h"

struct TileKey
{
	TileKey(const RenderRequest& render_, const TileRect& rect_, int column_, int row_)
		: render(render_), rect(rect_), column(column_), row(row_) {}
	RenderRequest render;
	TileRect rect;
	int column, row;
};

inline bool operator==(const TileKey& a, const TileKey& b)
{
	return a.render == b.render && a.rect == b.rect &&
		a.column == b.column && a.row == b.row;
}

struct TileRequest
{
	explicit TileRequest(const TileKey& key_) : key(key_) {}
	TileKey key;
};

// A tile producer owns bitmap until it is assembled or discarded. TileResult
// is not a new ownership model: the caller still deletes the CDIB explicitly.
struct TileResult
{
	TileResult(const TileKey& key_, CDIB* bitmap_) : key(key_), bitmap(bitmap_) {}
	TileKey key;
	CDIB* bitmap;
};

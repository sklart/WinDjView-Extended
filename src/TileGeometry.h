#pragma once

#include <vector>

// Pixel coordinates use the same bottom-left origin as DjVuLibre pixmaps and
// the positive-height DIB written by Drawing.cpp.
struct TileRect
{
	TileRect() : x(0), y(0), width(0), height(0) {}
	TileRect(int x_, int y_, int width_, int height_)
		: x(x_), y(y_), width(width_), height(height_) {}
	int x, y, width, height;
};

inline bool operator==(const TileRect& a, const TileRect& b)
{
	return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

struct TileGeometry
{
	static const int kTileSize = 512;
	static const unsigned long long kMinimumTargetPixels = 4ULL * 1024 * 1024;

	// Decide from the requested raster target (after zoom and any crop expansion),
	// not the source DjVu dimensions. Ordinary pages and thumbnails stay on the
	// established full-page path. This phase tiles DIB conversion/assembly after
	// DjVuLibre has produced its source pixmap/bitmap; it does not yet bound that
	// source raster's peak memory.
	static bool ShouldTile(int width, int height, bool thumbnail)
	{
		return !thumbnail && width > 0 && height > 0 &&
			(width >= 2048 || height >= 2048) &&
			static_cast<unsigned long long>(width) * height >= kMinimumTargetPixels;
	}
};

class TileGrid
{
public:
	TileGrid(int width, int height, int tileSize = TileGeometry::kTileSize)
		: m_width(width), m_height(height), m_tileSize(tileSize), m_columns(0), m_rows(0)
	{
		if (width > 0 && height > 0 && tileSize > 0)
		{
			m_columns = 1 + (width - 1) / tileSize;
			m_rows = 1 + (height - 1) / tileSize;
		}
	}

	int Columns() const { return m_columns; }
	int Rows() const { return m_rows; }
	unsigned long long Count() const
	{
		return static_cast<unsigned long long>(m_columns) * m_rows;
	}
	TileRect At(int column, int row) const
	{
		if (column < 0 || row < 0 || column >= m_columns || row >= m_rows)
			return TileRect();
		const int x = column * m_tileSize;
		const int y = row * m_tileSize;
		return TileRect(x, y,
			m_width - x < m_tileSize ? m_width - x : m_tileSize,
			m_height - y < m_tileSize ? m_height - y : m_tileSize);
	}

private:
	int m_width, m_height, m_tileSize, m_columns, m_rows;
};

// Tracks actual completed tiles, not merely submitted jobs. A missing or
// duplicate tile can never make a batch appear complete.
class TileCompletion
{
public:
	explicit TileCompletion(const TileGrid& grid)
		: m_columns(grid.Columns()), m_rows(grid.Rows()),
		  m_done(static_cast<std::size_t>(grid.Count()), false), m_remaining(grid.Count()) {}

	bool Mark(int column, int row)
	{
		if (column < 0 || row < 0 || column >= m_columns || row >= m_rows)
			return false;
		const std::size_t index = static_cast<std::size_t>(row) * m_columns + column;
		if (m_done[index]) return false;
		m_done[index] = true;
		--m_remaining;
		return true;
	}
	bool Has(int column, int row) const
	{
		return column >= 0 && row >= 0 && column < m_columns && row < m_rows &&
			m_done[static_cast<std::size_t>(row) * m_columns + column];
	}
	bool Complete() const { return m_remaining == 0 && !m_done.empty(); }
	unsigned long long Remaining() const { return m_remaining; }

private:
	int m_columns, m_rows;
	std::vector<bool> m_done;
	unsigned long long m_remaining;
};

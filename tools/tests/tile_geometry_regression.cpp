#include "../../src/TileGeometry.h"
#include <stdio.h>
#include <vector>

static bool Check(int width, int height, int expectedColumns, int expectedRows)
{
	TileGrid grid(width, height);
	if (grid.Columns() != expectedColumns || grid.Rows() != expectedRows ||
		grid.Count() != static_cast<unsigned long long>(expectedColumns) * expectedRows)
		return false;
	if (width <= 0 || height <= 0)
		return grid.Count() == 0;
	std::vector<unsigned char> covered(static_cast<size_t>(width) * height, 0);
	for (int row = 0; row < grid.Rows(); ++row)
	{
		for (int column = 0; column < grid.Columns(); ++column)
		{
			TileRect tile = grid.At(column, row);
			if (tile.x != column * TileGeometry::kTileSize ||
				tile.y != row * TileGeometry::kTileSize ||
				tile.width < 1 || tile.height < 1 ||
				tile.width > TileGeometry::kTileSize || tile.height > TileGeometry::kTileSize ||
				tile.x + tile.width > width || tile.y + tile.height > height)
				return false;
			for (int y = tile.y; y < tile.y + tile.height; ++y)
				for (int x = tile.x; x < tile.x + tile.width; ++x)
					if (++covered[static_cast<size_t>(y) * width + x] != 1)
						return false;
		}
	}
	for (size_t i = 0; i < covered.size(); ++i)
		if (covered[i] != 1) return false;
	return grid.At(-1, 0) == TileRect() && grid.At(grid.Columns(), 0) == TileRect();
}

int main()
{
	const bool ok = Check(1, 1, 1, 1) && Check(512, 512, 1, 1) &&
		Check(513, 513, 2, 2) && Check(2048, 3073, 4, 7) &&
		Check(3073, 2048, 7, 4) && // 90-degree target rotation
		Check(0, 100, 0, 0) && Check(-1, 100, 0, 0) &&
		Check(100, 0, 0, 0) && Check(100, -1, 0, 0) &&
		TileGeometry::ShouldTile(2048, 2048, false) &&
		!TileGeometry::ShouldTile(2048, 2048, true) &&
		!TileGeometry::ShouldTile(1024, 1365, false) &&
		!TileGeometry::ShouldTile(0, 4096, false) &&
		!TileGeometry::ShouldTile(2048, 2047, false);
	printf("Tile geometry regression: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}

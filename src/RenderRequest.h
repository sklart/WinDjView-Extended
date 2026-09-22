//	WinDjView
//	Copyright (C) 2004-2012 Andrew Zhezherun
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#pragma once

#include "Global.h"
#include "AppSettings.h"

class CDIB;

// Value description of a raster request. Scheduler priority, queue position
// and worker state intentionally do not participate in this identity.
struct RenderRequest
{
	RenderRequest() : page(-1), rotation(0), displayMode(0) {}
	RenderRequest(int page_, const CSize& size_, int rotation_, int displayMode_,
		const CDisplaySettings& displaySettings_)
		: page(page_), size(size_), rotation(rotation_), displayMode(displayMode_),
		  displaySettings(displaySettings_) {}

	int page;
	CSize size;
	int rotation;
	int displayMode;
	CDisplaySettings displaySettings;
};

inline bool operator==(const RenderRequest& left, const RenderRequest& right)
{
	return left.page == right.page && left.size == right.size &&
		left.rotation == right.rotation && left.displayMode == right.displayMode &&
		left.displaySettings == right.displaySettings;
}

inline bool operator!=(const RenderRequest& left, const RenderRequest& right)
{
	return !(left == right);
}

// A render result transfers bitmap ownership to the consumer when it is
// published. The producer deletes bitmap when the result is not published.
struct RenderResult
{
	RenderResult(const RenderRequest& request_, CDIB* bitmap_)
		: request(request_), bitmap(bitmap_) {}

	RenderRequest request;
	CDIB* bitmap;
};

#pragma once

#include <afx.h>
#include <list>

#include "libdjvu/GRect.h"

namespace PositionParser
{
    const size_t kMaxRectangleInputLength = 8192;
    const size_t kMaxRectangles = 256;

    bool ParseRectangles(const CString& source, std::list<GRect>& rectangles);
}

#include "stdafx.h"
#include "PositionParser.h"

#include <limits.h>

namespace
{
bool ParseIntStrict(const CString& value, int& result)
{
    CString text(value);
    text.Trim();
    if (text.IsEmpty())
        return false;

    TCHAR* end = NULL;
    errno = 0;
    const long parsed = _tcstol(text, &end, 10);
    if (errno == ERANGE || end == (LPCTSTR)text || *end != 0 ||
        parsed < INT_MIN || parsed > INT_MAX)
        return false;

    result = (int)parsed;
    return true;
}
}

bool PositionParser::ParseRectangles(const CString& source, std::list<GRect>& rectangles)
{
    rectangles.clear();
    if (source.IsEmpty() || source.GetLength() > (int)kMaxRectangleInputLength)
        return false;

    CString normalized(source);
    normalized.Replace(_T(','), _T(';'));
    CStringArray values;
    int start = 0;
    for (;;)
    {
        const int end = normalized.Find(_T(';'), start);
        const CString token = end < 0 ? normalized.Mid(start) : normalized.Mid(start, end - start);
        if (token.IsEmpty())
            return false;
        values.Add(token);
        if (end < 0)
            break;
        start = end + 1;
    }

    if (values.GetSize() < 4 || values.GetSize() % 4 != 0 ||
        (size_t)(values.GetSize() / 4) > kMaxRectangles)
        return false;

    for (int index = 0; index < values.GetSize(); index += 4)
    {
        int x, y, width, height;
        if (!ParseIntStrict(values[index], x) || !ParseIntStrict(values[index + 1], y) ||
            !ParseIntStrict(values[index + 2], width) || !ParseIntStrict(values[index + 3], height) ||
            width <= 0 || height <= 0 || x > INT_MAX - width || y > INT_MAX - height)
            return false;
        rectangles.push_back(GRect(x, y, width, height));
    }
    return !rectangles.empty();
}

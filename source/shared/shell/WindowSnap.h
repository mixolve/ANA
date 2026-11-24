#pragma once

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace window_geometry
{
struct SnapBounds
{
    int x, y, width, height;
    int right() const { return x + width; }
    int bottom() const { return y + height; }
};
struct SnapTarget { SnapBounds bounds; bool screen = false; };
struct SnapPosition { int x, y; };

inline SnapPosition snapPosition(const SnapBounds moving,
                                 const std::vector<SnapTarget>& targets,
                                 const int threshold = 8)
{
    int dx = 0, dy = 0, bestX = threshold + 1, bestY = threshold + 1;
    const auto consider = [](int delta, int& best, int& correction)
    {
        const auto distance = std::abs(delta);
        if (distance < best) { best = distance; correction = delta; }
    };
    for (const auto& target : targets)
    {
        const auto& other = target.bounds;
        if (moving.y <= other.bottom() + threshold && moving.bottom() >= other.y - threshold)
        {
            consider(other.x - moving.x, bestX, dx);
            consider(other.right() - moving.right(), bestX, dx);
            if (!target.screen)
            {
                consider(other.x - moving.right(), bestX, dx);
                consider(other.right() - moving.x, bestX, dx);
            }
        }
        if (moving.x <= other.right() + threshold && moving.right() >= other.x - threshold)
        {
            consider(other.y - moving.y, bestY, dy);
            consider(other.bottom() - moving.bottom(), bestY, dy);
            if (!target.screen)
            {
                consider(other.y - moving.bottom(), bestY, dy);
                consider(other.bottom() - moving.y, bestY, dy);
            }
        }
    }
    return { moving.x + dx, moving.y + dy };
}
struct ResizeEdges { bool left = false, top = false, right = false, bottom = false; };
struct ResizeLimits { int minWidth, minHeight, maxWidth, maxHeight; };

inline SnapBounds snapResize(const SnapBounds moving, const std::vector<SnapTarget>& targets,
                             const ResizeEdges edges, const ResizeLimits limits,
                             const int threshold = 8)
{
    const auto snapEdge = [&](const bool horizontal, const bool leading)
    {
        const auto original = horizontal ? (leading ? moving.x : moving.right())
                                         : (leading ? moving.y : moving.bottom());
        auto result = original;
        auto best = threshold + 1;
        const auto consider = [&](const int coordinate)
        {
            const auto size = leading
                ? (horizontal ? moving.right() : moving.bottom()) - coordinate
                : coordinate - (horizontal ? moving.x : moving.y);
            const auto minimum = horizontal ? limits.minWidth : limits.minHeight;
            const auto maximum = horizontal ? limits.maxWidth : limits.maxHeight;
            const auto distance = std::abs(coordinate - original);
            if (size >= minimum && size <= maximum && distance < best)
            { best = distance; result = coordinate; }
        };
        for (const auto& target : targets)
        {
            const auto& other = target.bounds;
            const auto overlaps = horizontal
                ? moving.y <= other.bottom() + threshold && moving.bottom() >= other.y - threshold
                : moving.x <= other.right() + threshold && moving.right() >= other.x - threshold;
            if (!overlaps) continue;
            if (!target.screen || leading) consider(horizontal ? other.x : other.y);
            if (!target.screen || !leading) consider(horizontal ? other.right() : other.bottom());
        }
        return result;
    };
    const auto left = edges.left ? snapEdge(true, true) : moving.x;
    const auto right = edges.right ? snapEdge(true, false) : moving.right();
    const auto top = edges.top ? snapEdge(false, true) : moving.y;
    const auto bottom = edges.bottom ? snapEdge(false, false) : moving.bottom();
    return { left, top, right - left, bottom - top };
}

}

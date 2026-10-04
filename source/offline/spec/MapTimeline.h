#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ana::offline
{
struct SpectrogramTimeSpan
{
    double start = 0.0;
    double end = 1.0;
};

// Interpolate sparse STFT columns inside an item, never across a timeline gap.
class SpectrogramTimeline
{
public:
    struct Bracket
    {
        size_t column0 = 0;
        size_t column1 = 0;
        float mix = 0.0f;
        size_t spanFirst = 0;
        size_t spanLast = 0;
        bool valid = false;
    };

    SpectrogramTimeline(const std::vector<uint32_t>& counts,
                        const std::vector<SpectrogramTimeSpan>& spansIn)
        : spans(spansIn), previous(counts.size(), -1), next(counts.size(), -1)
    {
        auto populated = -1;
        for (size_t column = 0; column < counts.size(); ++column)
        {
            if (counts[column] != 0)
                populated = static_cast<int>(column);
            previous[column] = populated;
        }
        populated = -1;
        for (size_t column = counts.size(); column-- > 0;)
        {
            if (counts[column] != 0)
                populated = static_cast<int>(column);
            next[column] = populated;
        }
    }

    float framePosition(const double time) const noexcept
    {
        if (previous.empty())
            return 0.0f;
        return static_cast<float>(std::clamp(
            time * static_cast<double>(previous.size()) - 0.5,
            0.0, static_cast<double>(previous.size() - 1)));
    }

    Bracket bracket(const double time) const noexcept
    {
        Bracket result;
        if (previous.empty() || ! std::isfinite(time))
            return result;
        const auto columns = static_cast<double>(previous.size());
        const auto position = framePosition(time);
        for (const auto& span : spans)
        {
            if (time < span.start || time >= span.end)
                continue;
            const auto first = static_cast<int>(std::clamp(
                std::floor(span.start * columns), 0.0, columns - 1.0));
            const auto last = static_cast<int>(std::clamp(
                std::ceil(span.end * columns) - 1.0, 0.0, columns - 1.0));
            const auto floor = std::clamp(static_cast<int>(std::floor(position)), first, last);
            const auto ceil = std::min(last, floor + 1);
            auto left = previous[static_cast<size_t>(floor)];
            auto right = next[static_cast<size_t>(ceil)];
            if (left < first)
                left = -1;
            if (right > last)
                right = -1;
            if (left < 0)
                left = right;
            if (right < 0)
                right = left;
            if (left < 0 || right < 0)
                continue;
            result.column0 = static_cast<size_t>(left);
            result.column1 = static_cast<size_t>(right);
            result.mix = left == right ? 0.0f : std::clamp(
                (position - static_cast<float>(left)) / static_cast<float>(right - left),
                0.0f, 1.0f);
            result.spanFirst = static_cast<size_t>(first);
            result.spanLast = static_cast<size_t>(last);
            result.valid = true;
            return result;
        }
        return result;
    }

private:
    const std::vector<SpectrogramTimeSpan>& spans;
    std::vector<int> previous;
    std::vector<int> next;
};
}

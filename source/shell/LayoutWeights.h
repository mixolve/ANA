#pragma once

#include <array>
#include <cstddef>

namespace ana::processor_detail
{
template <size_t Count>
void normalizeLayoutWeights(std::array<float, Count>& weights) noexcept
{
    auto total = 0.0;
    for (const auto weight : weights)
        total += static_cast<double>(weight);
    for (auto& weight : weights)
        weight = static_cast<float>(static_cast<double>(weight) / total);
}
}

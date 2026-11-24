#pragma once
#include <array>
#include <atomic>
#include <cstdint>

inline constexpr std::array<std::uint32_t, 10> animationColours {
    0xFF9999, 0xFFCC99, 0xFFFF99, 0x99FF99, 0x99FFFF,
    0x9999FF, 0xCC99FF, 0xFF99CC, 0xFFFFFF, 0xBBBBBB
};

struct BurningFilmSettings
{
    std::atomic<int> colour { static_cast<int>(animationColours.size()) - 1 };
    std::atomic<float> speed { 2.0f };
    std::atomic<float> glow { 1.0f };
    std::atomic<float> scaleFactor { 0.1f };
    std::atomic<float> cropY { 0.0f };
};

struct AnimationSettings
{
    std::atomic<bool> playOnPause { false };
    std::atomic<int> pauseDelayMs { 1000 };
    BurningFilmSettings burningFilm;
};

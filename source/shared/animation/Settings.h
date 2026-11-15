#pragma once
#include <array>
#include <atomic>
#include <cstdint>

inline constexpr std::array<std::uint32_t, 10> animationColours {
    0xFF9999, 0xFFCC99, 0xFFFF99, 0x99FF99, 0x99FFFF,
    0x9999FF, 0xCC99FF, 0xFF99CC, 0xFFFFFF, 0xBBBBBB
};

struct AnimationModeSettings
{
    std::atomic<int> colour { static_cast<int>(animationColours.size()) - 1 };
    std::atomic<float> speed { 1.0f };
    std::atomic<float> glow { 1.0f };
    std::atomic<float> scaleFactor { 0.1f };
    std::atomic<float> thickness { 1.0f };
    std::atomic<float> density { 14.0f };
    std::atomic<float> cropY { 0.0f };
};

inline const char* animationModeName(int mode) noexcept
{
    return mode == 2 ? "topographic" : "burning-film";
}

struct AnimationSettings
{
    std::atomic<bool> playOnPause { false };
    std::atomic<int> pauseDelayMs { 1000 };
    // Burning Film is the only active mode; old mode storage is retained for host-state compatibility.
    std::atomic<int> mode { 1 };
    std::array<AnimationModeSettings, 2> modes;
    AnimationSettings() { modes[0].speed.store(2.0f); }
    AnimationModeSettings& current() noexcept { return modes[0]; }
    const AnimationModeSettings& current() const noexcept { return modes[0]; }
};

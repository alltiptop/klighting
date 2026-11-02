// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>

namespace Ambient
{
inline constexpr auto ConfigGroup = "Effect-ambientled";
inline constexpr int DefaultFramesPerSecond = 3;
inline constexpr int MaxFramesPerSecond = 20;
inline constexpr int DefaultSmoothingMs = 400;
inline constexpr int MaxSmoothingMs = 3000;
inline constexpr int DefaultLedFramesPerSecond = 60;
inline constexpr int MaxLedFramesPerSecond = 60;
inline constexpr int DefaultMinimumBrightness = 25;
inline constexpr int DefaultMaximumBrightness = 70;
inline constexpr int DefaultColorTemperature = 0;
inline int ledIntervalMs(int framesPerSecond)
{
    const int fps = std::clamp(framesPerSecond, 1, MaxLedFramesPerSecond);
    return (1000 + fps - 1) / fps;
}
}

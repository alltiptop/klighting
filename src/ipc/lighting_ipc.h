// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Ambient::Ipc
{
inline constexpr auto Service = "org.klighting.Service";
inline constexpr auto Path = "/org/klighting/Service";
inline constexpr auto Interface = "org.klighting.Service1";
inline constexpr auto CapturePath = "/org/klighting/Capture";
inline constexpr auto CaptureInterface = "org.klighting.Capture1";
inline constexpr auto DesktopPath = "/org/klighting/Desktop";
inline constexpr auto DesktopInterface = "org.klighting.Desktop1";
inline constexpr auto IntegrationPlugin = "klighting_integration";
inline constexpr int Version = 1;
inline constexpr int SampleBytes = 32 * 18 * 4;
}

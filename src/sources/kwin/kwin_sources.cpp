// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_sources.h"
#include "kwin_capture_source.h"
#include "kwin_desktop_source.h"

namespace Ambient
{
IntegrationSources kwinSources()
{
    auto guard = std::make_shared<HardwareControlGuard>();
    return { std::make_unique<KWinDesktopSource>(), std::make_unique<KWinCaptureSource>(guard), guard };
}
}

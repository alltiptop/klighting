// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "color.h"
#include <cmath>
#include <core/rect.h>

namespace Ambient
{
inline QRect captureRegion(
    const KWin::RectF& screen, const KWin::RectF& viewport, double scale, bool bottomOnly, int bottomPercent)
{
    if (screen.isEmpty() || viewport.isEmpty() || !std::isfinite(scale) || scale <= 0)
        return { };
    // KWin snaps the viewport to physical pixels and then multiplies by 1/scale.
    // LogicalOutput divides by scale instead. At 150% the resulting rectangles
    // differ by floating-point roundoff, so strict logical containment fails.
    if (!viewport.scaled(scale).rounded().contains(screen.scaled(scale).rounded()))
        return { };
    // The framebuffer blit API takes integer logical coordinates. Round inward
    // so scaling the source rectangle cannot read beyond the render target.
    return sampleRegion(screen.intersected(viewport).roundedIn(), bottomOnly, bottomPercent);
}
}

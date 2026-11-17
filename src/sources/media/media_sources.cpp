// SPDX-License-Identifier: GPL-3.0-or-later
#include "combined_media_activity_source.h"
#include "sources/media_activity_source.h"
#if defined(KLIGHTING_HAVE_PIPEWIRE)
#include "pipewire_activity_source.h"
#endif
#if defined(KLIGHTING_HAVE_V4L2)
#include "v4l2_activity_source.h"
#endif

namespace Ambient
{
std::unique_ptr<MediaActivitySource> mediaActivitySource()
{
    std::unique_ptr<MediaActivitySource> pipewire, v4l2;
#if defined(KLIGHTING_HAVE_PIPEWIRE)
    pipewire = std::make_unique<PipeWireActivitySource>();
#endif
#if defined(KLIGHTING_HAVE_V4L2)
    v4l2 = std::make_unique<V4l2ActivitySource>();
#endif
    if (!pipewire && !v4l2)
        return { };
    return std::make_unique<CombinedMediaActivitySource>(std::move(pipewire), std::move(v4l2));
}
}

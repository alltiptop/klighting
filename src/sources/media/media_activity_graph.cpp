// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_activity_graph.h"

namespace Ambient
{
namespace
{
    QString sourceKind(const QVariantMap& p)
    {
        const auto mediaClass = p.value("media.class").toString();
        if (mediaClass == "Audio/Source" || mediaClass == "Audio/Source/Virtual") {
            if (p.value("device.class") != "monitor")
                return QStringLiteral("microphone-active");
        }
        if (mediaClass != "Video/Source" && mediaClass != "Stream/Output/Video")
            return { };
        const auto role = p.value("media.role").toString();
        // KWin names its streams but does not set media.role.
        if (role == "Screen" || p.value("node.name").toString().startsWith("kwin-screencast-")
            || p.value("media.name").toString().startsWith("kwin-screencast-"))
            return QStringLiteral("screen-capture");
        const auto api = p.value("device.api").toString();
        if (role == "Camera" || api == "v4l2" || api == "libcamera" || p.contains("api.v4l2.path")
            || p.contains("api.libcamera.path"))
            return QStringLiteral("camera-active");
        return { }; // An unidentified video source is not necessarily a camera or screen.
    }
}
QVariantMap MediaActivityGraph::values() const
{
    for (const auto& node : nodes)
        if (!node.known)
            return { };
    for (const auto& link : links)
        if (!link.known || !nodes.contains(link.output) || !nodes.contains(link.input))
            return { };
    QVariantMap result { { "microphone-active", false }, { "camera-active", false },
        { "screen-capture", false } };
    for (const auto& link : links) {
        if (!link.active)
            continue;
        const auto& output = nodes[link.output];
        const auto& input = nodes[link.input];
        if (!output.running || !input.running)
            continue;
        const auto kind = sourceKind(output.properties);
        if (!kind.isEmpty())
            result[kind] = true;
    }
    return result;
}
}

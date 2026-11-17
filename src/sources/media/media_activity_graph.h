// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMap>
#include <QVariantMap>

namespace Ambient
{
struct MediaActivityNode {
    QVariantMap properties;
    bool running = false, known = false;
};
struct MediaActivityLink {
    quint32 output = 0, input = 0;
    bool active = false, known = false;
};
// A metadata-only snapshot of the visible PipeWire graph.
struct MediaActivityGraph {
    QMap<quint32, MediaActivityNode> nodes;
    QMap<quint32, MediaActivityLink> links;
    QVariantMap values() const;
};
}

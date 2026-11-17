// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QSet>
#include <QVariantMap>
#include <memory>

namespace Ambient
{
// Observes capture activity without receiving audio or video buffers.
class MediaActivitySource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setEnabled(bool enabled) = 0;
    virtual void setRequiredKinds(const QSet<QString>& kinds) { setEnabled(!kinds.isEmpty()); }
Q_SIGNALS:
    void changed(const QVariantMap& values);
};
std::unique_ptr<MediaActivitySource> mediaActivitySource();
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>

namespace Ambient
{
class AudioStateSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setEnabled(bool enabled) = 0;
Q_SIGNALS:
    void changed(const QVariantMap& values);
};
}

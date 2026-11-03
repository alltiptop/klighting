// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "presets.h"
#include <QObject>

namespace Ambient
{
// Desktop-specific observation only; preset selection and layer evaluation stay in the runtime.
// Future providers can supply the same state without a compositor dependency in the core.
class DesktopSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setTrackedKeys(const QSet<quint32>& keys) = 0;
    virtual QString status() const = 0;
Q_SIGNALS:
    void stateChanged(const QString& windowClass, const Ambient::ModifierState& modifiers, bool locked);
    void unavailable();
};
}

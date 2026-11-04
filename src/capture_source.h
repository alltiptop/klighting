// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>

namespace Ambient
{
struct CaptureState {
    bool providerAvailable = false;
    bool screenAvailable = false;
    bool enabled = false;
    bool ready = false; // A usable screen, independent of whether sampling was requested.
    QString status { };
    QVariantMap info { };
    bool operator==(const CaptureState&) const = default;
};

// Image observation only. Hardware ownership is a separate integration concern.
class CaptureSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void requestCapture(int fps, const QVariantMap& options) = 0;
    virtual CaptureState state() const = 0;
Q_SIGNALS:
    void changed();
    void sample(const QByteArray& rgba);
};
}

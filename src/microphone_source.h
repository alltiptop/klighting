// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>

namespace Ambient
{
struct MicrophoneState {
    bool available = false;
    bool muteKnown = false;
    bool muted = false;
    QString backend, sourceName, description, reason;
    bool operator==(const MicrophoneState&) const = default;
};

// Observes the default input's mute switch without capturing or changing audio.
class MicrophoneSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setEnabled(bool enabled) = 0;
    virtual MicrophoneState state() const = 0;
Q_SIGNALS:
    void changed();
};
}

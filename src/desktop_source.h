// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "presets.h"
#include <QObject>
#include <optional>

namespace Ambient
{
enum class ProviderState { Checking, Available, Unavailable };
struct DesktopState {
    ProviderState provider = ProviderState::Checking;
    std::optional<QString> windowClass = std::nullopt;
    std::optional<ModifierState> modifiers = std::nullopt;
    std::optional<bool> locked = std::nullopt;
    bool operator==(const DesktopState&) const = default;
};
// Desktop-specific observation only; preset selection and layer evaluation stay in the runtime.
// Future providers can supply the same state without a compositor dependency in the core.
class DesktopSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setTrackedKeys(const QSet<quint32>& keys) = 0;
    virtual QString status() const = 0;
    virtual QVariantMap info() const { return { }; }
    virtual bool canRetry() const { return false; }
    virtual QString retry() { return QStringLiteral("Desktop source cannot be retried"); }
    DesktopState state() const { return m_state; }
Q_SIGNALS:
    void changed();

protected:
    void publish(const DesktopState& state)
    {
        if (m_state == state)
            return;
        m_state = state;
        Q_EMIT changed();
    }

private:
    DesktopState m_state;
};
}

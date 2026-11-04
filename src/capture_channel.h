// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "daemon_peer.h"
#include <QDBusContext>

namespace Ambient
{
class CaptureChannel : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.klighting.Capture1")
public:
    explicit CaptureChannel(QObject* parent = nullptr);
    ~CaptureChannel() override;
    void sendSample(const QByteArray& rgba);
    void setError(const QString& error) { m_error = error; }
    void setState(bool screenAvailable, bool enabled, bool locked)
    {
        m_screenAvailable = screenAvailable;
        m_enabled = enabled;
        m_locked = locked;
    }
public Q_SLOTS:
    Q_SCRIPTABLE QVariantMap Snapshot() const;
    Q_SCRIPTABLE void Configure(int fps, const QVariantMap& options);
Q_SIGNALS:
    void configured(int fps, const QVariantMap& options);
    Q_SCRIPTABLE void Sample(const QByteArray& rgba);

private:
    DaemonPeer m_peer;
    QString m_error;
    const QString m_instance;
    bool m_registered = false;
    bool m_screenAvailable = false, m_enabled = false, m_locked = false;
};
}

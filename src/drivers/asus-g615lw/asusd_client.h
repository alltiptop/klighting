// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "aura_protocol.h"
#include "led_transport.h"
#include "settings.h"
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <memory>

namespace Ambient
{
class AsusdClient : public QObject
{
    Q_OBJECT
public:
    explicit AsusdClient(QObject* parent = nullptr, const QDBusConnection& bus = QDBusConnection::systemBus(),
        QString service = QString::fromLatin1(AsusdService), std::unique_ptr<LedTransport> transport = { });
    ~AsusdClient() override;
    void start(const QString& preferredPath = { });
    void setEnabled(bool enabled);
    void setLedFramesPerSecond(int framesPerSecond);
    bool ready() const;
    QString status() const { return m_status; }
    quint64 completedFrames() const { return m_completedFrames; }
    void submit(
        const QColor& color, const QList<QColor>& keyColors = { }, const QList<QColor>& stripColors = { });
Q_SIGNALS:
    void readinessChanged();
private Q_SLOTS:
    void propertiesChanged(
        const QString& interface, const QVariantMap& changed, const QStringList& invalidated);

private:
    void discover();
    void discoverObjects(const QString& owner, quint64 generation);
    void forget(const QString& reason);
    void pump();
    void restore();
    void sendRestore(bool setting = false);
    void transferFinished(quint64 operation, const QString& error);
    bool readPower(const QVariant& value);
    std::unique_ptr<LedTransport> m_transport;
    QDBusConnection m_bus;
    QString m_service, m_owner, m_path, m_preferredPath, m_status;
    QTimer m_retry, m_sendTimer;
    QElapsedTimer m_lastWrite;
    QByteArray m_restorePacket;
    std::optional<LedFrame> m_lastFrame, m_pending;
    quint64 m_generation = 0;
    quint64 m_completedFrames = 0;
    quint64 m_operation = 0;
    uint m_brightness = 0;
    int m_ledIntervalMs = ledIntervalMs(DefaultLedFramesPerSecond);
    bool m_enabled = true, m_busy = false, m_discovering = false;
    bool m_touched = false, m_restorePending = false;
    bool m_usbTouched = false, m_releasePending = false, m_releasing = false;
    bool m_keyboard = false, m_lightbar = false;
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QTimer>

namespace Ambient
{
// Optional screen source and guard against legacy effects that still own hardware.
class KWinCaptureSource : public QObject
{
    Q_OBJECT
public:
    explicit KWinCaptureSource(
        const QDBusConnection& bus = QDBusConnection::sessionBus(), QObject* parent = nullptr);
    ~KWinCaptureSource() override;
    void requestCapture(int fps, const QVariantMap& options);
    QString status() const { return m_status; }
    bool safeToControl() const { return m_safe; }
    bool captureAvailable() const { return m_capture; }
Q_SIGNALS:
    void changed();
    void sample(const QByteArray& rgba);
private Q_SLOTS:
    void screenSample(const QByteArray& rgba);

private:
    void ownerChanged(const QString& owner);
    void refresh();
    void captureState(bool safe, bool available, const QString& status);
    void configureCapture();
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_poll;
    QString m_owner, m_status;
    int m_generation = 0, m_fps = 0;
    QVariantMap m_options;
    bool m_safe = false, m_capture = false, m_reading = false;
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QTimer>

namespace Ambient
{
class ServiceControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit ServiceControl(QObject* parent = nullptr);
    bool busy() const { return m_busy; }
    bool running() const { return m_running; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    void reload();
    void refresh();
Q_SIGNALS:
    void changed();
    void finished();

private:
    QTimer m_timer;
    QString m_status, m_error;
    bool m_busy = false, m_running = false, m_reading = false;
    int m_generation = 0;
};
}

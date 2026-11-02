// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <KSharedConfig>
#include <QObject>
#include <QTimer>

namespace Ambient
{
class EffectControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit EffectControl(QObject* parent = nullptr);
    bool enabled() const { return m_enabled; }
    bool available() const { return m_available; }
    bool loaded() const { return m_loaded; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    bool dirty() const { return m_pending; }
    void setEnabled(bool enabled);
    Q_INVOKABLE void refresh();
    void reset();
    void apply();
Q_SIGNALS:
    void changed();
    void finished();

private:
    KSharedConfig::Ptr m_config;
    QTimer m_timer;
    QString m_name, m_loadedName, m_status, m_error;
    bool m_enabled = false, m_saved = false, m_loaded = false, m_available = false;
    bool m_busy = false, m_reading = false, m_pending = false;
    void complete(const QString& error);
    void changeRunningState(const QString& name, bool requested, bool enableAfterUnload = false);
    void verify(const QString& name, bool requested, bool enableAfterUnload);
};
}

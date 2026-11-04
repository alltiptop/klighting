// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_manager.h"
#include "effect_palette.h"
#include "notification_effect.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace Ambient
{
// Owns lighting independently of any compositor, capture provider or settings window.
class LightingRuntime : public QObject
{
    Q_OBJECT
public:
    explicit LightingRuntime(const DeviceEnvironment& environment = deviceEnvironment(),
        const QStringList& directories = driverDirectories(), QObject* parent = nullptr);
    ~LightingRuntime() override;
    void configure(const PresetCollection& collection);
    void setEnabled(bool enabled);
    void setLocked(bool locked);
    void setDesktop(const QString& windowClass, const ModifierState& modifiers);
    void setPowerState(const PowerState& state);
    void setCaptureAvailable(bool available);
    void sample(QByteArrayView rgba, double brightness = 1.0, double saturation = 1.0);
    void notify(const NotificationEvent& event);
    int captureFps() const;
    bool notificationsEnabled() const;
    bool running() const { return m_enabled && !m_locked; }
    QVariantMap status() const;
    const LoadedDriver& drivers() const { return m_devices.drivers(); }
Q_SIGNALS:
    void requirementsChanged();

private:
    void refresh();
    void updateEffects();
    void updateTimer();
    int inputDelayMs() const;
    void acceptSample(QByteArrayView rgba, double brightness, double saturation);
    void advance();
    void tick(bool force = false);
    DeviceManager m_devices;
    PresetCollection m_collection;
    EffectPalette m_palette;
    NotificationEffects m_notifications;
    DominantColorPicker m_picker;
    PowerState m_power;
    QString m_windowClass;
    ModifierState m_modifiers;
    QTimer m_timer, m_discovery;
    QElapsedTimer m_clock, m_lastSample;
    double m_time = 0, m_nextNotificationOutput = 0;
    QMap<QString, QColor> m_lastPalette, m_lastPulses;
    QMap<QString, int> m_lastBrightness;
    struct PendingSample {
        QByteArray rgba;
        double brightness, saturation;
    };
    std::optional<PendingSample> m_pendingSample;
    QColor m_screenColor;
    bool m_readinessPending = false;
    bool m_enabled = false, m_locked = false, m_captureAvailable = false;
    quint64 m_samples = 0, m_animationWakeups = 0, m_ticks = 0, m_effectUpdates = 0;
    int m_ledFps = 0;
};
}

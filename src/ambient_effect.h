// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_manager.h"
#include "effect_palette.h"
#include "frame_sampler.h"
#include "notification_source.h"
#include "power_source.h"
#include "presets.h"
#include "settings.h"
#include <QPointer>
#include <QTimer>
#include <effect/effect.h>

namespace KWin
{
class Window;
}

namespace Ambient
{
class AmbientEffect : public KWin::Effect
{
    Q_OBJECT
public:
    AmbientEffect();
    ~AmbientEffect() override;
    static bool supported();
    void reconfigure(ReconfigureFlags flags) override;
    bool isActive() const override;
    bool blocksDirectScanout() const override { return isActive(); }
    int requestedEffectChainPosition() const override { return 0; }
    void paintScreen(const KWin::RenderTarget& target, const KWin::RenderViewport& viewport, int mask,
        const KWin::Region& region, KWin::LogicalOutput* screen) override;
    QString debug(const QString& parameter) const override;

private:
    void tick();
    void updateTimer();
    bool lightingActive() const;
    void selectScreen();
    void updateEnabled();
    void updateKeyLayer();
    ModifierState modifiers() const;
    void watchActiveWindow();
    void selectPreset(bool force = false);
    void resetCapture();
    void discardSampler();
    DeviceManager m_devices;
    DominantColorPicker m_colorPicker;
    EffectPalette m_palette;
    NotificationEffects m_notifications;
    NotificationSource m_notificationSource;
    PowerSource m_powerSource;
    std::unique_ptr<FrameSampler> m_sampler;
    QPointer<KWin::LogicalOutput> m_screen;
    QTimer m_timer, m_deviceTimer;
    QElapsedTimer m_captureTime, m_tickTime;
    QString m_screenName;
    QVariantMap m_deviceOptions;
    int m_framesPerSecond = DefaultFramesPerSecond, m_captureInterval = 334;
    int m_bottomPercent = 25;
    int m_ledFramesPerSecond = DefaultLedFramesPerSecond;
    double m_brightness = 1, m_saturation = 1;
    bool m_bottomOnly = false, m_captureRequested = false, m_failed = false, m_painting = false;
    bool m_locked = false, m_conflict = false, m_shiftHeld = false;
    quint64 m_screenPaints = 0, m_captures = 0, m_samples = 0;
    quint64 m_skippedBounds = 0, m_missingFramebuffer = 0;
    QColor m_sampleColor;
    PresetCollection m_presets;
    ModifierState m_modifiers;
    QPointer<KWin::Window> m_activeWindow;
    QString m_activeClass, m_presetError;
};
}

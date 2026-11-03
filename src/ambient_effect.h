// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "capture_channel.h"
#include "frame_sampler.h"
#include <QPointer>
#include <QTimer>
#include <effect/effect.h>

namespace Ambient
{
// Screen sampling only. Hardware, presets and animations belong to klightingd.
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
    void configure(int fps, const QVariantMap& options);
    void selectScreen();
    void resetCapture();
    void discardSampler();
    void tick();
    CaptureChannel m_channel;
    std::unique_ptr<FrameSampler> m_sampler;
    QPointer<KWin::LogicalOutput> m_screen;
    QTimer m_timer;
    QElapsedTimer m_captureTime;
    QString m_screenName;
    int m_framesPerSecond = 0, m_captureInterval = 1000, m_bottomPercent = 25;
    bool m_bottomOnly = false, m_locked = false, m_failed = false, m_painting = false;
    quint64 m_screenPaints = 0, m_captures = 0, m_samples = 0;
    quint64 m_skippedBounds = 0, m_missingFramebuffer = 0;
};
}

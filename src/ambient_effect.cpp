// SPDX-License-Identifier: GPL-3.0-or-later
#include "ambient_effect.h"
#include "capture_geometry.h"
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <algorithm>
#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <opengl/eglcontext.h>

Q_LOGGING_CATEGORY(ambientEffectLog, "kwin.effect.ambientled")
namespace Ambient
{
using namespace KWin;
AmbientEffect::AmbientEffect()
{
    m_locked = effects->isScreenLocked();
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AmbientEffect::tick);
    connect(&m_channel, &CaptureChannel::configured, this, &AmbientEffect::configure);
    connect(effects, &EffectsHandler::screenAdded, this, &AmbientEffect::selectScreen);
    connect(effects, &EffectsHandler::screenRemoved, this, &AmbientEffect::selectScreen);
    connect(effects, &EffectsHandler::screenAboutToLock, this, [this] {
        m_locked = true;
        resetCapture();
    });
    connect(effects, &EffectsHandler::screenLockingChanged, this, [this](bool locked) {
        m_locked = locked;
        resetCapture();
    });
    selectScreen();
}
AmbientEffect::~AmbientEffect() { discardSampler(); }
bool AmbientEffect::supported()
{
    if (!effects->isOpenGLCompositing())
        return false;
    const auto* context = effects->openglContext();
    return context && context->hasMapBufferRange() && context->haveSyncFences()
        && context->hasVersion(Version(3, 0));
}
void AmbientEffect::reconfigure(ReconfigureFlags)
{
    m_failed = false;
    m_channel.setError({ });
    resetCapture();
}
void AmbientEffect::configure(int fps, const QVariantMap& options)
{
    const auto screen = options.value("screen").toString();
    const bool bottom = options.value("bottomOnly").toBool();
    const int percent = std::clamp(options.value("bottomPercent", 25).toInt(), 1, 100);
    if (m_framesPerSecond == fps && m_screenName == screen && m_bottomOnly == bottom
        && m_bottomPercent == percent)
        return;
    m_framesPerSecond = fps;
    m_captureInterval = fps > 0 ? (1000 + fps - 1) / fps : 1000;
    m_screenName = screen;
    m_bottomOnly = bottom;
    m_bottomPercent = percent;
    selectScreen();
}
void AmbientEffect::selectScreen()
{
    if (m_screen)
        disconnect(m_screen, nullptr, this, nullptr);
    m_screen = nullptr;
    const auto screens = effects->screens();
    for (auto* screen : screens) {
        if ((!m_screenName.isEmpty() && screen->name() == m_screenName)
            || (m_screenName.isEmpty() && screen->isInternal())) {
            m_screen = screen;
            break;
        }
    }
    if (!m_screen && m_screenName.isEmpty() && !screens.isEmpty())
        m_screen = screens.first();
    if (m_screen)
        connect(m_screen, &LogicalOutput::geometryChanged, this, &AmbientEffect::resetCapture);
    resetCapture();
}
void AmbientEffect::resetCapture()
{
    m_timer.stop();
    discardSampler();
    m_captureTime.invalidate();
    if (isActive()) {
        // Poll the asynchronous readback promptly; capture cadence is independent.
        m_timer.start(16);
        effects->addRepaint(m_screen->geometry());
    }
}
bool AmbientEffect::isActive() const { return m_screen && m_framesPerSecond > 0 && !m_locked && !m_failed; }
void AmbientEffect::discardSampler()
{
    if (!m_sampler)
        return;
    if (effects->makeOpenGLContextCurrent()) {
        m_sampler.reset();
    } else {
        // A lost compositor context cannot safely destroy GL wrappers. These
        // tiny resources are reclaimed with the failed context/process.
        qCWarning(ambientEffectLog) << "Cannot release sampler after OpenGL context loss";
        (void)m_sampler.release();
    }
}
void AmbientEffect::paintScreen(const RenderTarget& target, const RenderViewport& viewport, int mask,
    const Region& region, LogicalOutput* screen)
{
    const bool outermost = !m_painting;
    QScopedValueRollback<bool> painting(m_painting, true);
    effects->paintScreen(target, viewport, mask, region, screen);
    if (!outermost || !isActive() || screen != m_screen || m_framesPerSecond == 0)
        return;
    ++m_screenPaints;
    const QRect area = captureRegion(
        screen->geometryF(), viewport.renderRect(), viewport.scale(), m_bottomOnly, m_bottomPercent);
    if (area.isEmpty()) {
        ++m_skippedBounds;
        return;
    }
    if (!target.framebuffer()) {
        ++m_missingFramebuffer;
        return;
    }
    if (!m_sampler)
        m_sampler = std::make_unique<FrameSampler>();
    if (m_sampler->pending() || (m_captureTime.isValid() && m_captureTime.elapsed() < m_captureInterval)) {
        return;
    }
    if (m_sampler->capture(target, viewport, area)) {
        ++m_captures;
        m_captureTime.start();
    }
}
void AmbientEffect::tick()
{
    if (!isActive())
        return;
    if (m_sampler && m_sampler->pending() && effects->makeOpenGLContextCurrent()) {
        if (const auto pixels = m_sampler->poll()) {
            ++m_samples;
            m_channel.sendSample(*pixels);
        }
    }
    if (m_sampler && m_sampler->failed()) {
        m_failed = true;
        m_channel.setError(QStringLiteral("GPU readback failed; reload Ambient capture to retry"));
        m_timer.stop();
        discardSampler();
        return;
    }
    if ((!m_sampler || !m_sampler->pending())
        && (!m_captureTime.isValid() || m_captureTime.elapsed() >= m_captureInterval))
        effects->addRepaint(m_screen->geometry());
}
QString AmbientEffect::debug(const QString&) const
{
    return QStringLiteral("captureProtocol=1 screen=%1 active=%2 fps<=%3 paints=%4 captures=%5 samples=%6 "
                          "locked=%7 gpuFailed=%8 boundsSkipped=%9 noFramebuffer=%10")
        .arg(m_screen ? m_screen->name() : QStringLiteral("none"))
        .arg(isActive())
        .arg(m_framesPerSecond)
        .arg(m_screenPaints)
        .arg(m_captures)
        .arg(m_samples)
        .arg(m_locked)
        .arg(m_failed)
        .arg(m_skippedBounds)
        .arg(m_missingFramebuffer);
}
}

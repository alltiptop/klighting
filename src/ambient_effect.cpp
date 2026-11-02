// SPDX-License-Identifier: GPL-3.0-or-later
#include "ambient_effect.h"
#include "capture_geometry.h"
#include "effect_names.h"
#include <KConfigGroup>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <input.h>
#include <keyboard_input.h>
#include <opengl/eglcontext.h>
#include <window.h>
#include <xkb.h>

Q_LOGGING_CATEGORY(ambientEffectLog, "kwin.effect.ambientled")
namespace Ambient
{
using namespace KWin;
AmbientEffect::AmbientEffect()
{
    m_locked = effects->isScreenLocked();
    connect(&m_timer, &QTimer::timeout, this, &AmbientEffect::tick);
    connect(effects, &EffectsHandler::windowActivated, this, &AmbientEffect::watchActiveWindow);
    connect(&m_deviceTimer, &QTimer::timeout, this, [this] {
        if (m_devices.refresh())
            updateKeyLayer();
        updateTimer();
        tick();
    });
    m_deviceTimer.start(2000);
    if (input()) {
        connect(
            input(), &InputRedirection::keyStateChanged, this,
            [this] {
                if (modifiers() != m_modifiers)
                    updateKeyLayer();
            },
            Qt::QueuedConnection);
        connect(
            input(), &InputRedirection::keyboardModifiersChanged, this,
            [this] {
                if (modifiers() != m_modifiers)
                    updateKeyLayer();
            },
            Qt::QueuedConnection);
    }
    connect(effects, &EffectsHandler::screenAdded, this, &AmbientEffect::selectScreen);
    connect(effects, &EffectsHandler::screenRemoved, this, &AmbientEffect::selectScreen);
    connect(effects, &EffectsHandler::screenAboutToLock, this, [this] {
        m_locked = true;
        updateEnabled();
    });
    connect(effects, &EffectsHandler::screenLockingChanged, this, [this](bool locked) {
        m_locked = locked;
        updateEnabled();
    });
    connect(
        &m_notificationSource, &NotificationSource::received, this, [this](const NotificationEvent& event) {
            if (lightingActive()) {
                m_notifications.trigger(event);
                updateTimer();
            }
        });
    connect(&m_powerSource, &PowerSource::stateChanged, this, [this] {
        m_devices.setPowerState(m_powerSource.state());
        updateKeyLayer();
        resetCapture();
    });
    connect(&m_powerSource, &PowerSource::powerConnected, this, [this] {
        // Apply power policy first so a paused device can show the notification
        // as soon as its backend resumes.
        if (!m_locked && !m_conflict && !m_failed)
            m_notifications.trigger({ { }, { }, { }, { }, 1, QStringLiteral("power-connected") });
        updateTimer();
    });
    reconfigure(ReconfigureAll);
}
AmbientEffect::~AmbientEffect()
{
    m_timer.stop();
    // Restore is sent by the client destructor after queued colour writes.
    discardSampler();
    m_devices.setEnabled(false);
}
bool AmbientEffect::supported()
{
    if (!LoadedDriver().hasDrivers())
        return false;
    if (!effects->isOpenGLCompositing())
        return false;
    const auto* context = effects->openglContext();
    return context && context->hasMapBufferRange() && context->haveSyncFences()
        && context->hasVersion(Version(3, 0));
}
void AmbientEffect::reconfigure(ReconfigureFlags)
{
    m_powerSource.reloadThreshold();
    m_devices.setPowerState(m_powerSource.state());
    const KConfigGroup cfg(effects->config(), ConfigGroup);
    m_deviceOptions = { { "AuraObjectPath", cfg.readEntry("AuraObjectPath", QString()) } };
    m_screenName = cfg.readEntry("ScreenName", QString());
    m_bottomOnly = cfg.readEntry("Region", QStringLiteral("full")) == QLatin1String("bottom");
    m_bottomPercent = std::clamp(cfg.readEntry("BottomPercent", 25), 1, 100);
    m_brightness = std::clamp(cfg.readEntry("Brightness", 1.0), 0.0, 1.0);
    m_saturation = std::clamp(cfg.readEntry("Saturation", 1.0), 0.0, 2.0);
    const auto fallback = defaultCollection(m_devices.drivers().description().id);
    const auto collection = loadPresets(fallback, &m_presetError);
    if (collection)
        m_presets = *collection;
    else if (m_presets.presets.isEmpty())
        m_presets = fallback;
    // A malformed edit keeps the last valid running collection.
    m_failed = false;
    m_devices.configure(m_presets, m_deviceOptions);
    watchActiveWindow();
    selectPreset(true);
}
void AmbientEffect::watchActiveWindow()
{
    if (m_activeWindow)
        disconnect(m_activeWindow, nullptr, this, nullptr);
    const auto* effectWindow = effects->activeWindow();
    m_activeWindow = effectWindow ? effectWindow->window() : nullptr;
    if (m_activeWindow)
        connect(m_activeWindow, &Window::windowClassChanged, this, [this] { selectPreset(); });
    selectPreset();
}
void AmbientEffect::selectPreset(bool force)
{
    m_activeClass
        = m_activeWindow && !m_activeWindow->isDesktop() ? m_activeWindow->resourceClass() : QString();
    updateKeyLayer();
    if (force || !m_screen)
        selectScreen();
    if (m_framesPerSecond == 0)
        discardSampler();
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
    updateEnabled();
}
ModifierState AmbientEffect::modifiers() const
{
    ModifierState state;
    if (input() && input()->keyboard()) {
        const auto* keyboard = input()->keyboard();
        const auto keys = keyboard->pressedKeys();
        state.pressed = QSet<quint32>(keys.begin(), keys.end());
        state.effective = keyboard->modifiers();
        if (const auto* xkb = keyboard->xkb()) {
            state.capsLock = xkb->leds().testFlag(LED::CapsLock);
            if ((xkb->latchedModifiers() | xkb->lockedModifiers()).testFlag(Xkb::Mod5))
                state.effective |= Qt::GroupSwitchModifier;
        }
    }
    return state;
}
void AmbientEffect::updateKeyLayer()
{
    m_modifiers = modifiers();
    m_shiftHeld = conditionActive(QStringLiteral("shift"), m_modifiers);
    m_devices.selectPresets(m_activeClass, m_modifiers);
    const auto effects = runtimeEffects(m_presets);
    m_palette.configure(effects, m_devices.ambientEffects(), m_powerSource.state());
    m_framesPerSecond = m_palette.samplingFps();
    m_captureInterval = m_framesPerSecond > 0 ? (1000 + m_framesPerSecond - 1) / m_framesPerSecond : 1000;
    m_notifications.configure(effects, m_devices.notificationEffects(), m_powerSource.state());
    m_notificationSource.setEnabled(!m_notifications.empty() && !m_locked && !m_conflict && !m_failed);
    updateTimer();
    if (m_framesPerSecond == 0)
        discardSampler();
    QTimer::singleShot(0, this, &AmbientEffect::tick);
    m_captureRequested = m_framesPerSecond > 0;
    // Key signals keep static lighting responsive without an animation timer.
    // No input is grabbed, logged or consumed.
}
void AmbientEffect::updateTimer()
{
    const int previousFps = m_ledFramesPerSecond;
    m_ledFramesPerSecond = m_notifications.active() ? std::max(m_palette.ledFps(), 60) : m_palette.ledFps();
    if (previousFps != m_ledFramesPerSecond)
        m_devices.setLedFramesPerSecond(m_ledFramesPerSecond);
    if (!lightingActive() || m_ledFramesPerSecond == 0) {
        m_timer.stop();
        return;
    }
    const int interval = ledIntervalMs(m_ledFramesPerSecond);
    m_timer.setTimerType(Qt::PreciseTimer);
    if (!m_timer.isActive())
        m_tickTime.start();
    if (!m_timer.isActive() || m_timer.interval() != interval)
        m_timer.start(interval);
}
void AmbientEffect::updateEnabled()
{
    // Count native plugin aliases too, including old copies without this guard.
    // Do not choose a winner: let the user disable the unwanted effect in KDE.
    m_conflict = hasConflictingAmbientEffects(effects->loadedEffects());
    m_devices.setEnabled(!m_locked && m_screen && !m_conflict && !m_failed);
    m_notificationSource.setEnabled(!m_notifications.empty() && !m_locked && !m_conflict && !m_failed);
    resetCapture();
    updateTimer();
}
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
void AmbientEffect::resetCapture()
{
    discardSampler();
    m_colorPicker.reset();
    m_palette.reset();
    m_notifications.reset();
    m_captureTime.invalidate();
    m_tickTime.start();
    m_captureRequested = m_framesPerSecond > 0;
    if (isActive() && m_captureRequested)
        effects->addRepaint(m_screen->geometry());
}
bool AmbientEffect::isActive() const { return m_framesPerSecond > 0 && lightingActive(); }
bool AmbientEffect::lightingActive() const
{
    return m_screen && !m_locked && !m_failed && !m_conflict && m_devices.ready();
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
        m_captureRequested = true;
        return;
    }
    if (m_sampler->capture(target, viewport, area)) {
        ++m_captures;
        m_captureTime.start();
        m_captureRequested = false;
    }
}
void AmbientEffect::tick()
{
    // KWin has no public loaded-effects change signal. Check on the existing
    // timer, including while paused, so unloading an extra copy resumes us.
    if (hasConflictingAmbientEffects(effects->loadedEffects()) != m_conflict)
        updateEnabled();
    const double elapsed = m_tickTime.isValid() ? m_tickTime.restart() : m_timer.interval();
    if (!lightingActive())
        return;
    if (modifiers() != m_modifiers)
        updateKeyLayer();
    if (m_sampler && m_sampler->pending() && effects->makeOpenGLContextCurrent()) {
        if (const auto pixels = m_sampler->poll()) {
            ++m_samples;
            m_sampleColor = m_colorPicker.select(*pixels, m_brightness, m_saturation);
            m_palette.sample(m_sampleColor);
        }
    }
    if (m_sampler && m_sampler->failed()) {
        m_failed = true;
        m_devices.setEnabled(false);
        m_notifications.reset();
        m_notificationSource.setEnabled(false);
        qCWarning(ambientEffectLog) << "GPU readback failed; effect paused until reconfiguration";
        return;
    }
    if (m_captureRequested && (!m_sampler || !m_sampler->pending())
        && (!m_captureTime.isValid() || m_captureTime.elapsed() >= m_captureInterval))
        effects->addRepaint(m_screen->geometry());
    m_palette.advance(elapsed);
    m_notifications.advance(elapsed);
    m_devices.submit(m_palette.colors(), m_notifications.colors());
    updateTimer();
}
QString AmbientEffect::debug(const QString&) const
{
    return QStringLiteral("screen=%1 active=%2 fps<=%3 ledFps<=%4 devices=%5 driverStatus=%6 "
                          "paints=%7 captures=%8 samples=%9 sent=%10 color=%11 locked=%12 gpuFailed=%13 "
                          "boundsSkipped=%14 noFramebuffer=%15 conflict=%16 effects=%17 windowClass=%18 "
                          "presetError=%19 notifications=%20 power=%21")
        .arg(m_screen ? m_screen->name() : QStringLiteral("none"))
        .arg(lightingActive())
        .arg(m_framesPerSecond)
        .arg(m_ledFramesPerSecond)
        .arg(m_devices.count())
        .arg(m_devices.status())
        .arg(m_screenPaints)
        .arg(m_captures)
        .arg(m_samples)
        .arg(m_devices.completedFrames())
        .arg(m_sampleColor.isValid() ? m_sampleColor.name() : QStringLiteral("none"))
        .arg(m_locked)
        .arg(m_failed)
        .arg(m_skippedBounds)
        .arg(m_missingFramebuffer)
        .arg(m_conflict)
        .arg(m_palette.colors().size())
        .arg(m_activeClass, m_presetError, m_notificationSource.status(), m_powerSource.status());
}
}

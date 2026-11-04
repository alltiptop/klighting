// SPDX-License-Identifier: GPL-3.0-or-later
#include "component_info.h"
#include "daemon_peer.h"
#include "integration_startup_guard.h"
#include "lighting_ipc.h"
#include <QDBusContext>
#include <QDBusMessage>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <input.h>
#include <keyboard_input.h>
#include <linux/input-event-codes.h>
#include <plugin.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>
#include <xkb.h>

namespace Ambient
{
class DesktopIntegration : public KWin::Plugin, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.klighting.Desktop1")
public:
    DesktopIntegration()
    {
        m_registered = QDBusConnection::sessionBus().registerObject(Ipc::DesktopPath, this,
            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
        m_publish.setSingleShot(true);
        connect(&m_publish, &QTimer::timeout, this, &DesktopIntegration::publish);
        connect(&m_peer, &DaemonPeer::changed, this, [this] {
            m_state.clear();
            m_publish.start(0);
        });
        connect(KWin::workspace(), &KWin::Workspace::windowActivated, this, [this] { watchWindow(); });
        if (KWin::input()) {
            connect(KWin::input(), &KWin::InputRedirection::keyStateChanged, this,
                [this] { m_publish.start(0); });
            connect(KWin::input(), &KWin::InputRedirection::keyboardModifiersChanged, this,
                [this] { m_publish.start(0); });
        }
        if (KWin::waylandServer())
            connect(
                KWin::waylandServer(), &KWin::WaylandServer::lockStateChanged, this, [this] { publish(); });
        watchWindow();
    }
    ~DesktopIntegration() override
    {
        if (m_registered)
            QDBusConnection::sessionBus().unregisterObject(Ipc::DesktopPath);
    }
public Q_SLOTS:
    Q_SCRIPTABLE QVariantMap Snapshot()
    {
        if (!calledFromDBus() || !m_peer.isOwner(message().service()))
            return { };
        publish();
        return m_state;
    }
    Q_SCRIPTABLE void Configure(const QByteArray& trackedKeys)
    {
        if (!calledFromDBus())
            return;
        if (!m_peer.isOwner(message().service())) {
            sendErrorReply(QDBusError::AccessDenied,
                QStringLiteral("Only the lighting service may configure desktop input"));
            return;
        }
        if (trackedKeys.size() != m_tracked.size()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid tracked-key mask"));
            return;
        }
        if (m_tracked != trackedKeys) {
            m_tracked = trackedKeys;
            publish();
        }
    }
Q_SIGNALS:
    Q_SCRIPTABLE void StateChanged(const QVariantMap& state);

private:
    void watchWindow()
    {
        if (m_window)
            disconnect(m_window, nullptr, this, nullptr);
        m_window = KWin::workspace() ? KWin::workspace()->activeWindow() : nullptr;
        if (m_window)
            connect(m_window, &KWin::Window::windowClassChanged, this, [this] { m_publish.start(0); });
        publish();
    }
    void publish()
    {
        if (!m_registered || !m_peer.connected())
            return;
        const bool lockAvailable = KWin::waylandServer();
        const bool locked = lockAvailable && KWin::waylandServer()->isScreenLocked();
        const bool keysAvailable = KWin::input() && KWin::input()->keyboard();
        QByteArray keys(m_tracked.size(), '\0');
        Qt::KeyboardModifiers modifiers;
        bool capsLock = false;
        if (!locked && keysAvailable) {
            const auto* keyboard = KWin::input()->keyboard();
            for (const auto code : keyboard->pressedKeys())
                if (code > 0 && code <= KEY_MAX && (quint8(m_tracked[code / 8]) & (1u << (code % 8))))
                    keys[code / 8] = char(quint8(keys[code / 8]) | (1u << (code % 8)));
            modifiers = keyboard->modifiers();
            if (const auto* xkb = keyboard->xkb()) {
                capsLock = xkb->leds().testFlag(KWin::LED::CapsLock);
                if ((xkb->latchedModifiers() | xkb->lockedModifiers()).testFlag(KWin::Xkb::Mod5))
                    modifiers |= Qt::GroupSwitchModifier;
            }
        }
        QVariantMap next { { "locked", locked }, { "windowAvailable", KWin::workspace() != nullptr },
            { "keysAvailable", keysAvailable }, { "lockAvailable", lockAvailable },
            { "windowClass",
                !locked && m_window && !m_window->isDesktop() ? m_window->resourceClass().left(256)
                                                              : QString() },
            { "pressed", keys }, { "modifiers", uint(modifiers) }, { "capsLock", capsLock } };
        next.insert(componentInfo(Component::Desktop));
        next.insert("instanceId", m_instance);
        if (m_state == next)
            return;
        m_state = next;
        m_peer.send(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", { m_state });
    }
    DaemonPeer m_peer;
    QPointer<KWin::Window> m_window;
    QTimer m_publish;
    QVariantMap m_state;
    const QString m_instance = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray m_tracked = QByteArray((KEY_MAX + 8) / 8, '\0');
    bool m_registered = false;
};
class DesktopIntegrationFactory : public KWin::PluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "kwin_integration_build.json")
    Q_INTERFACES(KWin::PluginFactory)
public:
    std::unique_ptr<KWin::Plugin> create() const override
    {
        const auto error = IntegrationStartupGuard().consume(QDBusConnection::sessionBus().baseService());
        if (!error.isEmpty()) {
            qWarning().noquote() << error;
            return nullptr;
        }
        return std::make_unique<DesktopIntegration>();
    }
};
}
#include "kwin_integration.moc"

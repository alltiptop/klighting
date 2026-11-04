// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lighting_ipc.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QTimer>
using namespace Ambient;
class FakeKWin : public QDBusVirtualObject
{
    Q_OBJECT
public:
    QDBusConnection bus = QDBusConnection::sessionBus();
    QStringList reconfigured;
    QStringList installed { "ambientled" }, loaded { "ambientled" };
    QStringList loads, unloads;
    bool refuseLoad = false, refuseUnload = false, overlappingLoad = false;
    bool deferLoadReply = false;
    QList<bool> savedAtLoad, savedAtUnload;
    int unexpected = 0;
    int windowQueries = 0;
    QVariantMap windowInfo { { "resourceClass", "org.kde.kate" }, { "resourceName", "kate" } };
    QString windowError;
    bool deferWindowReply = false;
    QDBusMessage windowRequest;
    FakeKWin()
    {
        if (!bus.registerVirtualObject("/Effects", this) || !bus.registerVirtualObject("/KWin", this)
            || !bus.registerService("org.kde.KWin"))
            qFatal("Cannot register isolated KWin test fixture");
    }
    ~FakeKWin() override
    {
        bus.unregisterService("org.kde.KWin");
        bus.unregisterObject("/Effects");
        bus.unregisterObject("/KWin");
    }
    void replyToWindowQuery()
    {
        if (windowError.isEmpty())
            bus.send(windowRequest.createReply(QVariant::fromValue(windowInfo)));
        else
            bus.send(windowRequest.createErrorReply(windowError, "Window selection failed"));
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        if (message.path() == "/KWin" && message.interface() == "org.kde.KWin"
            && message.member() == "queryWindowInfo" && message.arguments().isEmpty()) {
            ++windowQueries;
            windowRequest = message;
            if (!deferWindowReply)
                replyToWindowQuery();
        } else if (message.interface() == "org.freedesktop.DBus.Properties" && message.member() == "GetAll") {
            QVariantMap properties { { "listOfEffects", installed }, { "loadedEffects", loaded } };
            connection.send(message.createReply(QVariant::fromValue(properties)));
        } else if (message.member() == "loadEffect") {
            loads << message.arguments().first().toString();
            savedAtLoad << KConfigGroup(KSharedConfig::openConfig("kwinrc"), "Plugins")
                               .readEntry("ambientledEnabled", false);
            if (deferLoadReply)
                return true;
            overlappingLoad = overlappingLoad || !loaded.isEmpty();
            if (!refuseLoad)
                loaded = loads.mid(loads.size() - 1);
            connection.send(message.createReply(!refuseLoad));
        } else if (message.member() == "unloadEffect") {
            unloads << message.arguments().first().toString();
            savedAtUnload << KConfigGroup(KSharedConfig::openConfig("kwinrc"), "Plugins")
                                 .readEntry("ambientledEnabled", false);
            if (!refuseUnload)
                loaded.removeAll(unloads.last());
            connection.send(message.createReply());
        } else if (message.member() == "isEffectLoaded") {
            connection.send(message.createReply(loaded.contains(message.arguments().first().toString())));
        } else if (message.interface() == "org.freedesktop.DBus.Properties" && message.member() == "Get"
            && message.arguments() == QVariantList { "org.kde.kwin.Effects", "loadedEffects" }) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(loaded))));
        } else if (message.interface() == "org.kde.kwin.Effects" && message.member() == "reconfigureEffect") {
            reconfigured.append(message.arguments().first().toString());
            connection.send(message.createReply());
        } else {
            ++unexpected;
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "unexpected call"));
        }
        return true;
    }
};

class FakeLighting : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.klighting.Service1")
public:
    int reloads = 0;
    int retries = 0;
    bool canRetry = false;
    QString retryError;
    QVariantMap info { { "protocol", Ipc::Version } };
    FakeLighting()
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(Ipc::Service)
            || !bus.registerObject(Ipc::Path, this, QDBusConnection::ExportAllSlots))
            qFatal("Cannot register isolated lighting service");
    }
    ~FakeLighting() override
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(Ipc::Path);
        bus.unregisterService(Ipc::Service);
    }
public Q_SLOTS:
    QString Reload()
    {
        ++reloads;
        return { };
    }
    QString RetryDesktopIntegration()
    {
        ++retries;
        if (retryError.isEmpty())
            canRetry = false;
        return retryError;
    }
    QVariantMap Status() const
    {
        auto status = info;
        status.insert("safeToControl", true);
        status.insert("desktopCanRetry", canRetry);
        status.insert("desktop", "Automatic loading paused");
        return status;
    }
};

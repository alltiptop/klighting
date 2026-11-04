// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_sources.h"
#include "lighting_ipc.h"
#include "lighting_service.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTextStream>
#include <csignal>
#include <sys/signalfd.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    // Block before creating worker threads; graceful termination releases USB ownership.
    sigset_t terminationSignals;
    sigemptyset(&terminationSignals);
    sigaddset(&terminationSignals, SIGTERM);
    sigaddset(&terminationSignals, SIGINT);
    sigprocmask(SIG_BLOCK, &terminationSignals, nullptr);
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("klightingd"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Device lighting service"));
    parser.addHelpOption();
    parser.addOption({ "activate", "Start the installed session service and exit." });
    parser.addOption({ "status", "Print the running service status without starting it." });
    parser.process(app);
    auto bus = QDBusConnection::sessionBus();
    if (parser.isSet("activate")) {
        auto call = QDBusMessage::createMethodCall(
            "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "StartServiceByName");
        call << QString::fromLatin1(Ambient::Ipc::Service) << 0u;
        const QDBusReply<uint> reply = bus.call(call, QDBus::Block, 10000);
        if (reply.isValid())
            return 0;
        QTextStream(stderr) << reply.error().message() << Qt::endl;
        return 1;
    }
    if (parser.isSet("status")) {
        auto call = QDBusMessage::createMethodCall(
            Ambient::Ipc::Service, Ambient::Ipc::Path, Ambient::Ipc::Interface, "Status");
        call.setAutoStartService(false);
        const QDBusReply<QVariantMap> reply = bus.call(call, QDBus::Block, 1500);
        if (!reply.isValid()) {
            QTextStream(stderr) << reply.error().message() << Qt::endl;
            return 1;
        }
        QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(reply.value())).toJson();
        return 0;
    }
    if (!bus.registerService(Ambient::Ipc::Service)) {
        QTextStream(stderr) << "Cannot own the lighting service: " << bus.lastError().message() << Qt::endl;
        return 1;
    }
    const int signalFd = signalfd(-1, &terminationSignals, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signalFd < 0)
        return 1;
    QSocketNotifier stop(signalFd, QSocketNotifier::Read);
    QObject::connect(&stop, &QSocketNotifier::activated, &app, [&] {
        signalfd_siginfo info;
        if (read(signalFd, &info, sizeof(info)) == sizeof(info))
            app.quit();
    });
    int result = 1;
    {
        Ambient::LightingService service(
            Ambient::deviceEnvironment(), Ambient::driverDirectories(), Ambient::kwinSources());
        if (bus.registerObject(Ambient::Ipc::Path, &service, QDBusConnection::ExportScriptableSlots))
            result = app.exec();
        bus.unregisterObject(Ambient::Ipc::Path);
    }
    bus.unregisterService(Ambient::Ipc::Service);
    close(signalFd);
    return result;
}

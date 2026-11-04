// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver_loader.h"
#include "effect_names.h"
#include "lighting_ipc.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>
#include <cmath>
#include <csignal>

static volatile std::sig_atomic_t interrupted = 0;
static void stopTest(int) { interrupted = 1; }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Read-only device discovery, or an explicit bounded lighting test."));
    parser.addHelpOption();
    parser.addOption({ QStringLiteral("test-colors"),
        QStringLiteral(
            "Run a 12-second uniform color test. Stop klightingd and unload the ambient effect first.") });
    parser.addOption({ QStringLiteral("test-keys"),
        QStringLiteral("Run a 12-second per-element color test. Stop klightingd and unload the ambient "
                       "effect first.") });
    parser.addOption({ QStringLiteral("device"), QStringLiteral("Select a driver:instance identity."),
        QStringLiteral("identity") });
    parser.process(app);
    const bool testKeys = parser.isSet(QStringLiteral("test-keys"));
    if (testKeys && parser.isSet(QStringLiteral("test-colors"))) {
        QTextStream(stderr) << "Choose only one test mode.\n";
        return 1;
    }
    const bool animate = parser.isSet(QStringLiteral("test-colors")) || testKeys;
    if (animate) {
        auto* busInterface = QDBusConnection::sessionBus().interface();
        if (!busInterface) {
            QTextStream(stderr) << "Cannot check lighting ownership; color test cancelled.\n";
            return 1;
        }
        const auto owner = busInterface->isServiceRegistered(Ambient::Ipc::Service);
        if (!owner.isValid() || owner.value()) {
            QTextStream(stderr) << "Stop klightingd before the color test.\n";
            return 1;
        }
        std::signal(SIGINT, stopTest);
        std::signal(SIGTERM, stopTest);
        const auto kwin = busInterface->isServiceRegistered("org.kde.KWin");
        if (!kwin.isValid()) {
            QTextStream(stderr) << "Cannot check KWin ownership; color test cancelled.\n";
            return 1;
        }
        if (kwin.value()) {
            auto call = QDBusMessage::createMethodCall(
                "org.kde.KWin", "/Effects", "org.freedesktop.DBus.Properties", "Get");
            call << "org.kde.kwin.Effects" << "loadedEffects";
            QDBusReply<QDBusVariant> reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 1000);
            if (!reply.isValid()) {
                QTextStream(stderr) << "Cannot check loaded KWin effects; color test cancelled.\n";
                return 1;
            }
            for (const auto& name : qdbus_cast<QStringList>(reply.value().variant())) {
                if (Ambient::isAmbientEffectName(name)) {
                    QTextStream(stderr) << "Unload " << name << " before the color test.\n";
                    return 1;
                }
            }
        }
    }
    Ambient::LoadedDriver driver;
    // Cached discovery runs off-thread. Allow it to finish before a one-shot probe exits.
    QElapsedTimer discoveryDeadline;
    discoveryDeadline.start();
    while (driver.discoveryPending() && discoveryDeadline.elapsed() < 2000) {
        QEventLoop wait;
        QTimer::singleShot(20, &wait, &QEventLoop::quit);
        wait.exec();
        driver.refresh();
    }
    if (driver.discoveryPending()) {
        QTextStream(stderr) << "Device discovery timed out.\n";
        return 1;
    }
    if (!animate) {
        for (const auto& device : driver.devices())
            QTextStream(stdout) << device.identity() << ": " << device.instance.description.name << Qt::endl;
        if (!driver.error().isEmpty())
            QTextStream(stderr) << driver.error() << Qt::endl;
        return driver.devices().isEmpty() ? 1 : 0;
    }
    if (parser.isSet("device"))
        driver.select(parser.value("device"));
    else if (driver.devices().size() != 1) {
        QTextStream(stderr) << "Select exactly one device with --device driver:instance.\n";
        return 1;
    } else
        driver.select(driver.devices().first().identity());
    if (!driver.driver()) {
        QTextStream(stderr) << driver.error() << Qt::endl;
        return 1;
    }
    if (testKeys && !driver.description().capabilities.testFlag(Ambient::DeviceCapability::PerKeyColor)
        && !driver.description().capabilities.testFlag(Ambient::DeviceCapability::PerStripColor)) {
        QTextStream(stderr) << "The selected device does not support per-element colors.\n";
        return 1;
    }
    auto backend = driver.driver()->createBackend(driver.instance());
    if (!backend)
        return 1;
    auto& client = *backend;
    QTextStream(stdout) << "Driver: " << driver.description().id << Qt::endl;
    client.setLedFramesPerSecond(20); // Keep the explicit diagnostic at its verified rate.
    QTimer colorTimer;
    colorTimer.setTimerType(Qt::PreciseTimer);
    bool started = false, finishing = false;
    int frame = 0;
    client.readinessChanged = [&] {
        QTextStream(stdout) << client.status() << Qt::endl;
        if (client.ready() && !started) {
            started = true;
            QTextStream(stdout) << (testKeys
                    ? "ELEMENT TEST: repeating red/green/blue in driver order; intensity changes smoothly."
                    : "COLOR TEST: 12 seconds, red -> green -> blue.")
                                << Qt::endl;
            colorTimer.start(50);
        } else if (animate && started && !finishing && !client.ready()) {
            finishing = true;
            colorTimer.stop();
            client.setEnabled(false);
            QTimer::singleShot(3000, &app, [&] { app.exit(1); });
        }
    };
    QObject::connect(&colorTimer, &QTimer::timeout, &app, [&] {
        if (testKeys) {
            const int value = qRound(96 + 24 * std::sin(frame / 20.0));
            Ambient::DeviceFrame colors { QColor(value, value, value), { }, { } };
            const auto color = [value](int index) { return QColor::fromHsv((index % 3) * 120, 255, value); };
            if (driver.description().capabilities.testFlag(Ambient::DeviceCapability::PerKeyColor))
                for (int i = 0; i < driver.description().keys.size(); ++i)
                    colors.keys.append(color(i));
            if (driver.description().capabilities.testFlag(Ambient::DeviceCapability::PerStripColor))
                for (const auto& strip : driver.description().strips)
                    for (int i = 0; i < strip.elements.size(); ++i)
                        colors.strips[strip.id].append(color(i));
            client.submit(colors);
        } else {
            client.submit({ QColor::fromHsv((frame * 3) % 360, 255, 128), { }, { } });
        }
        if (++frame == 240 || interrupted) {
            finishing = true;
            colorTimer.stop();
            client.setEnabled(false);
            QTimer::singleShot(3000, &app, [&] {
                QTextStream(stdout) << "Completed frames: " << client.completedFrames() << "; "
                                    << client.status() << Qt::endl;
                app.exit(!interrupted && client.completedFrames() > 0 ? 0 : 1);
            });
        }
    });
    QTimer::singleShot(3000, &app, [&] {
        if (!started)
            app.exit(1);
    });
    QTimer::singleShot(0, &app, [&] {
        client.setEnabled(true);
        client.start(driver.instance().options);
        client.readinessChanged();
    });
    return app.exec();
}

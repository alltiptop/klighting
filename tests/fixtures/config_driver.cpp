// SPDX-License-Identifier: GPL-3.0-or-later
#include "keyboard.h"
#include <QCoreApplication>
#include <QTimer>

class TestControls final : public Ambient::DeviceControls
{
public:
    void readLightbar(const QVariantMap&, std::function<void(Ambient::LightbarState)> done) override
    {
        QTimer::singleShot(0, qApp, [done = std::move(done)] {
            done({ true, qApp->property("test.controls.enabled").toBool(), { } });
        });
    }
    void writeLightbar(bool enabled, std::function<void(QString)> done) override
    {
        QTimer::singleShot(0, qApp, [enabled, done = std::move(done)] {
            qApp->setProperty("test.controls.enabled", enabled);
            qApp->setProperty("test.controls.writes", qApp->property("test.controls.writes").toInt() + 1);
            done({ });
        });
    }
};
class ConfigDriver final : public QObject, public Ambient::DeviceDriver
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "config_driver.json")
    Q_INTERFACES(Ambient::DeviceDriver)
public:
    QList<Ambient::DeviceInstance> discover(const Ambient::DeviceEnvironment&) const override
    {
        return { { "internal", TestFixture::keyboardDescription(),
            { { "InputKeyCodes", TestFixture::inputKeyCodes() } } } };
    }
    std::unique_ptr<Ambient::DeviceBackend> createBackend(const Ambient::DeviceInstance&) override
    {
        return { };
    }
    std::unique_ptr<Ambient::DeviceControls> createControls(const Ambient::DeviceInstance&) override
    {
        return std::make_unique<TestControls>();
    }
};
#include "config_driver.moc"

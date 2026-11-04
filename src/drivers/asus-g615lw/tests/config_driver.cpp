// SPDX-License-Identifier: GPL-3.0-or-later
#include "asus_controls.h"
#include "g615lw_layout.h"

// Tests use the real ASUS settings implementation against the isolated fake
// daemon, independent of the machine's DMI board name. No USB backend exists.
class ConfigDriver final : public QObject, public Ambient::DeviceDriver
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "../asus_driver.json")
    Q_INTERFACES(Ambient::DeviceDriver)
public:
    QList<Ambient::DeviceInstance> discover(const Ambient::DeviceEnvironment&) const override
    {
        return { { QStringLiteral("internal"), Ambient::g615lwDescription(),
            { { "InputKeyCodes", Ambient::g615lwInputKeyCodes() } } } };
    }
    std::unique_ptr<Ambient::DeviceBackend> createBackend(const Ambient::DeviceInstance&) override
    {
        return { };
    }
    std::unique_ptr<Ambient::DeviceControls> createControls(const Ambient::DeviceInstance&) override
    {
        return std::make_unique<Ambient::AsusControls>();
    }
};
#include "config_driver.moc"

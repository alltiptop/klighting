// SPDX-License-Identifier: GPL-3.0-or-later
#include "linux_leds.h"

class LinuxLedsDriver final : public QObject, public Ambient::DeviceDriver
{
    Q_OBJECT
    Q_PROPERTY(bool discoveryPending READ discoveryPending)
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "linux_leds.json")
    Q_INTERFACES(Ambient::DeviceDriver)
public:
    bool discoveryPending() const { return m_discovery.pending(); }
    QList<Ambient::DeviceInstance> discover(const Ambient::DeviceEnvironment& environment) const override
    {
        return m_discovery.discover({ environment.boardName });
    }
    std::unique_ptr<Ambient::DeviceBackend> createBackend(const Ambient::DeviceInstance& device) override
    {
        const auto path = device.options.value("SysfsPath").toString();
        return std::make_unique<Ambient::Generic::Backend>(
            [path] { return std::make_unique<Ambient::LinuxLeds::Session>(path); });
    }
    std::unique_ptr<Ambient::DeviceControls> createControls(const Ambient::DeviceInstance&) override
    {
        return { };
    }

private:
    mutable Ambient::Generic::DiscoveryCache m_discovery { [](const QStringList& context) {
        return Ambient::LinuxLeds::discover(QStringLiteral("/sys/class/leds"), context.value(0));
    } };
};
#include "linux_leds_driver.moc"

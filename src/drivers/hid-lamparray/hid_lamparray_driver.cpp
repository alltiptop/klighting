// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_lamps.h"

class HidLampArrayDriver final : public QObject, public Ambient::DeviceDriver
{
    Q_OBJECT
    Q_PROPERTY(bool discoveryPending READ discoveryPending)
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "hid_lamparray.json")
    Q_INTERFACES(Ambient::DeviceDriver)
public:
    bool discoveryPending() const { return m_discovery.pending(); }
    QList<Ambient::DeviceInstance> discover(const Ambient::DeviceEnvironment&) const override
    {
        return m_discovery.discover({ });
    }
    std::unique_ptr<Ambient::DeviceBackend> createBackend(const Ambient::DeviceInstance& device) override
    {
        return std::make_unique<Ambient::Generic::Backend>([device] {
            return Ambient::HidLampArray::createSession(device, Ambient::HidLampArray::cacheDirectory());
        });
    }
    std::unique_ptr<Ambient::DeviceControls> createControls(const Ambient::DeviceInstance&) override
    {
        return { };
    }

private:
    mutable Ambient::Generic::DiscoveryCache m_discovery { [](const QStringList&) {
        return Ambient::HidLampArray::discover(
            QStringLiteral("/sys/bus/usb/devices"), Ambient::HidLampArray::cacheDirectory());
    } };
};
#include "hid_lamparray_driver.moc"

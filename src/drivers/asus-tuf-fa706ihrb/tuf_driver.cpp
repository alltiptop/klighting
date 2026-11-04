// SPDX-License-Identifier: GPL-3.0-or-later
#include "fa706_layout.h"
#include "tuf_backend.h"

namespace Ambient
{
class TufDriver final : public QObject, public DeviceDriver
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "tuf_driver.json")
    Q_INTERFACES(Ambient::DeviceDriver)

public:
    QList<DeviceInstance> discover(const DeviceEnvironment& environment) const override
    {
        if (environment.boardName != QLatin1String("FA706IHRB"))
            return { };
        return { { QStringLiteral("internal"), fa706Description(),
            { { "InputKeyCodes", fa706InputKeyCodes() },
                { "DeviceClaims", QStringList { QStringLiteral("internal-keyboard") } } } } };
    }
    std::unique_ptr<DeviceBackend> createBackend(const DeviceInstance&) override
    {
        return std::make_unique<TufBackend>();
    }
    std::unique_ptr<DeviceControls> createControls(const DeviceInstance&) override { return { }; }
};
}
#include "tuf_driver.moc"

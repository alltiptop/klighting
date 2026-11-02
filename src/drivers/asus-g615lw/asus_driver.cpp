// SPDX-License-Identifier: GPL-3.0-or-later
#include "asus_controls.h"
#include "asus_translations.h"
#include "asusd_client.h"
#include "g615lw_layout.h"

namespace Ambient
{
class AsusBackend final : public DeviceBackend
{
public:
    AsusBackend()
    {
        QObject::connect(&m_client, &AsusdClient::readinessChanged, &m_client, [this] {
            if (readinessChanged)
                readinessChanged();
        });
    }
    void start(const QVariantMap& options) override
    {
        m_client.start(options.value("AuraObjectPath").toString());
    }
    void setEnabled(bool enabled) override { m_client.setEnabled(enabled); }
    void setLedFramesPerSecond(int fps) override { m_client.setLedFramesPerSecond(fps); }
    bool ready() const override { return m_client.ready(); }
    QString status() const override { return m_client.status(); }
    quint64 completedFrames() const override { return m_client.completedFrames(); }
    void submit(const DeviceFrame& frame) override
    {
        m_client.submit(frame.uniform, frame.keys, frame.strips.value(FirstStripId));
    }

private:
    AsusdClient m_client;
};
class AsusDriver final : public QObject, public DeviceDriver
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "asus_driver.json")
    Q_INTERFACES(Ambient::DeviceDriver)
    AsusTranslations m_translations;

public:
    QList<DeviceInstance> discover(const DeviceEnvironment& environment) const override
    {
        if (environment.boardName != QLatin1String("G615LW"))
            return { };
        return { { QStringLiteral("internal"), g615lwDescription(),
            { { "InputKeyCodes", g615lwInputKeyCodes() } } } };
    }
    std::unique_ptr<DeviceBackend> createBackend(const DeviceInstance&) override
    {
        return std::make_unique<AsusBackend>();
    }
    std::unique_ptr<DeviceControls> createControls(const DeviceInstance&) override
    {
        return std::make_unique<AsusControls>();
    }
};
}
#include "asus_driver.moc"

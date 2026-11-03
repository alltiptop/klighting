// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_driver.h"
#include <QCoreApplication>

namespace
{
void count(const QString& id, const char* action)
{
    const auto key = ("test." + id + '.' + action).toUtf8();
    qApp->setProperty(key, qApp->property(key).toInt() + 1);
}
class Backend final : public Ambient::DeviceBackend
{
    QString m_id;
    bool m_enabled = false;
    quint64 m_frames = 0;

public:
    explicit Backend(const QString& id)
        : m_id(id)
    {
        count(m_id, "created");
    }
    ~Backend() override { count(m_id, "destroyed"); }
    void start(const QVariantMap&) override { count(m_id, "started"); }
    void setEnabled(bool enabled) override
    {
        m_enabled = enabled;
        qApp->setProperty(("test." + m_id + ".enabled").toUtf8(), enabled);
    }
    void setLedFramesPerSecond(int) override { }
    bool ready() const override { return m_enabled; }
    QString status() const override { return m_enabled ? "Ready" : "Paused"; }
    quint64 completedFrames() const override { return m_frames; }
    void submit(const Ambient::DeviceFrame& frame) override
    {
        ++m_frames;
        count(m_id, "frames");
        QVariantList colors;
        for (const auto& c : frame.strips.value(Ambient::FirstStripId))
            colors.append(c.name());
        qApp->setProperty(("test." + m_id + ".colors").toUtf8(), colors);
        qApp->setProperty(("test." + m_id + ".uniform").toUtf8(), frame.uniform.name());
    }
};
}
class MultiDriver final : public QObject, public Ambient::DeviceDriver
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "multi_driver.json")
    Q_INTERFACES(Ambient::DeviceDriver)
public:
    QList<Ambient::DeviceInstance> discover(const Ambient::DeviceEnvironment&) const override
    {
        const auto setting = qApp->property("test.devices");
        const auto ids = setting.isValid() ? setting.toStringList() : QStringList { "one", "two" };
        QList<Ambient::DeviceInstance> devices;
        for (const auto& id : ids) {
            Ambient::DeviceDescription d {
                "test-strip", "Test LED strip",
                Ambient::DeviceCapability::Color | Ambient::DeviceCapability::PerStripColor, { }, { },
                { { Ambient::FirstStripId, "Strip", { { 7, "1", { } }, { 42, "2", { } }, { 1, "3", { } } } } }
            };
            const auto steps = qApp->property(("test." + id + ".brightnessSteps").toUtf8());
            if (steps.isValid()) {
                d.capabilities = Ambient::DeviceCapability::Brightness;
                d.keys = { { 7, "Keyboard", { 0, 0, 1, 1 } } };
                d.strips.clear();
            }
            devices.append({ id, d,
                { { "DeviceClaims", qApp->property(("test." + id + ".claims").toUtf8()) },
                    { "BrightnessSteps", steps } } });
        }
        return devices;
    }
    std::unique_ptr<Ambient::DeviceBackend> createBackend(const Ambient::DeviceInstance& d) override
    {
        return std::make_unique<Backend>(d.id);
    }
    std::unique_ptr<Ambient::DeviceControls> createControls(const Ambient::DeviceInstance&) override
    {
        return { };
    }
};
#include "multi_driver.moc"

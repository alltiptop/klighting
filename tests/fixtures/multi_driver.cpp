// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_driver.h"
#include <QCoreApplication>
#include <QDynamicPropertyChangeEvent>

namespace
{
void count(const QString& id, const char* action)
{
    const auto key = ("test." + id + '.' + action).toUtf8();
    qApp->setProperty(key, qApp->property(key).toInt() + 1);
}
class Backend final : public QObject, public Ambient::DeviceBackend
{
    QString m_id;
    bool m_enabled = false;
    quint64 m_frames = 0;

public:
    explicit Backend(const QString& id)
        : m_id(id)
    {
        count(m_id, "created");
        // Readiness injection is opt-in; ordinary tests and the benchmark keep
        // their original frame-delivery cost without observing every property.
        if (qApp->property(("test." + m_id + ".notifyReady").toUtf8()).toBool())
            qApp->installEventFilter(this);
    }
    ~Backend() override
    {
        qApp->removeEventFilter(this);
        count(m_id, "destroyed");
    }
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (object == qApp && event->type() == QEvent::DynamicPropertyChange
            && static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()
                == ("test." + m_id + ".ready").toUtf8()) {
            const auto notify = qApp->property(("test." + m_id + ".notifyReady").toUtf8());
            if (readinessChanged && (!notify.isValid() || notify.toBool()))
                readinessChanged();
        }
        return false;
    }
    void start(const QVariantMap&) override { count(m_id, "started"); }
    void setEnabled(bool enabled) override
    {
        const bool wasReady = ready();
        m_enabled = enabled;
        qApp->setProperty(("test." + m_id + ".enabled").toUtf8(), enabled);
        if (readinessChanged && wasReady != ready())
            readinessChanged();
    }
    void setLedFramesPerSecond(int) override { }
    bool ready() const override
    {
        const auto available = qApp->property(("test." + m_id + ".ready").toUtf8());
        return m_enabled && (!available.isValid() || available.toBool());
    }
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
            if (qApp->property(("test." + id + ".globalColor").toUtf8()).toBool())
                d.capabilities = Ambient::DeviceCapability::Color;
            if (qApp->property(("test." + id + ".shortStrip").toUtf8()).toBool())
                d.strips[0].elements.removeLast();
            const auto steps = qApp->property(("test." + id + ".brightnessSteps").toUtf8());
            if (steps.isValid()) {
                d.capabilities = Ambient::DeviceCapability::Brightness;
                d.keys = { { 7, "A", { 0, 0, 2, 1 } }, { 42, "B", { 2, 0, 2, 1 } } };
                d.strips.clear();
            }
            devices.append({ id, d,
                { { "DeviceClaims", qApp->property(("test." + id + ".claims").toUtf8()) },
                    { "ColorLevels", qApp->property(("test." + id + ".colorLevels").toUtf8()) },
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

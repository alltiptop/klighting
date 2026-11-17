// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/dbus_trigger_source.h"
#include "triggers/effect_triggers.h"
#include <QSignalSpy>
#include <QTest>

using namespace Ambient;
class TriggerFixture : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.klighting.TriggerTest")
    Q_PROPERTY(bool Enabled MEMBER enabled)
public:
    bool enabled = true;
Q_SIGNALS:
    void Event(bool value);
};
class DBusTriggersTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void propertyChangesPreservePulsePhase()
    {
        auto bus = QDBusConnection::sessionBus();
        TriggerFixture fixture;
        const QString service = "org.klighting.TriggerChangesTest";
        QVERIFY(bus.registerService(service));
        QVERIFY(bus.registerObject(
            "/Changes", &fixture, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals));
        EffectPreset effect;
        effect.id = "pulse";
        effect.type = "pulse";
        effect.notification.pattern = "blink";
        effect.notification.period = 1000;
        effect.trigger.type = "dbus";
        effect.trigger.service = service;
        effect.trigger.path = "/Changes";
        effect.trigger.interface = "org.klighting.TriggerTest";
        effect.trigger.member = "Enabled";
        EffectTriggers triggers;
        triggers.configure({ effect }, { effect.id }, { });
        DBusTriggerSource source(effect.trigger, bus);
        connect(&source, &DBusTriggerSource::valueChanged, this,
            [&](const QVariant& value) { triggers.setValue(effect.id, value); });
        QSignalSpy values(&source, &DBusTriggerSource::valueChanged);
        auto eventOptions = effect.trigger;
        eventOptions.signal = true;
        eventOptions.member = "Event";
        DBusTriggerSource events(eventOptions, bus);
        QSignalSpy ready(&events, &DBusTriggerSource::valueChanged);
        QSignalSpy barriers(&events, &DBusTriggerSource::triggered);
        QTRY_VERIFY(!values.isEmpty() && values.last().first() == QVariant(true));
        QTRY_VERIFY(!ready.isEmpty());
        const auto change = [&](const QVariantMap& changed, const QStringList& invalidated) {
            auto message = QDBusMessage::createSignal(
                "/Changes", "org.freedesktop.DBus.Properties", "PropertiesChanged");
            message << effect.trigger.interface << changed << invalidated;
            return bus.send(message);
        };
        triggers.advance(750);
        QCOMPARE(triggers.colors().value(effect.id).alpha(), 0);
        values.clear();
        QVERIFY(change({ { "Other", false } }, { "Another" }));
        // A later signal from the same sender establishes that PropertiesChanged was delivered.
        Q_EMIT fixture.Event(true);
        QTRY_COMPARE(barriers.size(), 1);
        QVERIFY(values.isEmpty());
        QCOMPARE(triggers.colors().value(effect.id).alpha(), 0);

        QVERIFY(change({ { "Enabled", true } }, { }));
        Q_EMIT fixture.Event(true);
        QTRY_COMPARE(barriers.size(), 2);
        QCOMPARE(values.size(), 1);
        QCOMPARE(values.first().first(), QVariant(true));
        QCOMPARE(triggers.colors().value(effect.id).alpha(), 0); // No false/true edge or phase reset.

        values.clear();
        fixture.enabled = false;
        QVERIFY(change({ { "Enabled", false } }, { }));
        QTRY_VERIFY(!values.isEmpty());
        QCOMPARE(values.size(), 1);
        QCOMPARE(values.first().first(), QVariant(false));
        QVERIFY(triggers.colors().isEmpty());

        values.clear();
        fixture.enabled = true;
        QVERIFY(change({ }, { "Enabled" }));
        QTRY_VERIFY(!values.isEmpty() && values.last().first() == QVariant(true));
        QVERIFY(!values.first().first().isValid()); // Real invalidation still clears and re-reads state.
        QCOMPARE(triggers.colors().value(effect.id).alpha(), 255);
        bus.unregisterObject("/Changes");
        bus.unregisterService(service);
    }
    void propertyOwnerLossAndSignal()
    {
        auto bus = QDBusConnection::sessionBus();
        TriggerFixture fixture;
        QVERIFY(bus.registerService("org.klighting.TriggerTest"));
        QVERIFY(bus.registerObject(
            "/Trigger", &fixture, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals));
        TriggerOptions t;
        t.type = "dbus";
        t.service = "org.klighting.TriggerTest";
        t.path = "/Trigger";
        t.interface = t.service;
        t.member = "Enabled";
        DBusTriggerSource source(t, bus);
        QSignalSpy values(&source, &DBusTriggerSource::valueChanged);
        QTRY_VERIFY(!values.isEmpty() && values.last().first() == QVariant(true));
        fixture.enabled = false;
        auto changed
            = QDBusMessage::createSignal("/Trigger", "org.freedesktop.DBus.Properties", "PropertiesChanged");
        changed << t.interface << QVariantMap { { "Enabled", false } } << QStringList();
        QVERIFY(bus.send(changed));
        QTRY_VERIFY(values.last().first() == QVariant(false));
        t.signal = true;
        t.member = "Event";
        DBusTriggerSource events(t, bus);
        QSignalSpy ready(&events, &DBusTriggerSource::valueChanged);
        QSignalSpy eventsReceived(&events, &DBusTriggerSource::triggered);
        auto incomplete = t;
        incomplete.member.clear();
        DBusTriggerSource inactive(incomplete, bus);
        QSignalSpy ignored(&inactive, &DBusTriggerSource::triggered);
        QTRY_VERIFY(!ready.isEmpty());
        Q_EMIT fixture.Event(true);
        QTRY_COMPARE(eventsReceived.size(), 1);
        QCOMPARE(eventsReceived.first().first(), QVariant(true));
        QVERIFY(bus.unregisterService(t.service));
        QTRY_VERIFY(!values.last().first().isValid());
        QVERIFY(bus.registerService(t.service));
        QTRY_VERIFY(values.last().first() == QVariant(false));
        Q_EMIT fixture.Event(true);
        QTRY_COMPARE(eventsReceived.size(), 2);
        QCOMPARE(ignored.size(), 0);
        bus.unregisterObject("/Trigger");
        bus.unregisterService(t.service);
    }
};
QTEST_GUILESS_MAIN(DBusTriggersTest)
#include "test_dbus_triggers.moc"

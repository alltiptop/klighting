// SPDX-License-Identifier: GPL-3.0-or-later
#include "triggers/effect_triggers.h"
#include <QJsonDocument>
#include <QTest>

using namespace Ambient;
class TriggersTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void unknownIsNeverActive()
    {
        TriggerOptions trigger;
        trigger.type = "microphone";
        trigger.value = false;
        QVERIFY(!triggerMatches(trigger, { }));
        QVERIFY(triggerMatches(trigger, false));
        QVERIFY(!triggerMatches(trigger, QString("false")));
        trigger.comparison = "ne";
        QVERIFY(!triggerMatches(trigger, { }));
        QVERIFY(triggerMatches(trigger, true));
        trigger.value = 50;
        trigger.comparison = "ge";
        QVERIFY(triggerMatches(trigger, 50.0));
        QVERIFY(!triggerMatches(trigger, 49.9));
        QVERIFY(!triggerMatches(trigger, "100"));
    }
    void legacyMigrationKeepsMeaning()
    {
        QString error;
        const auto mic = parseEffectPreset(
            R"({"format":"ambient-led-effect","version":1,"id":"mic","name":"Microphone","type":"microphone","options":{"color":"#102030"}})",
            &error);
        QVERIFY2(mic, qPrintable(error));
        QCOMPARE(mic->type, "solid");
        QCOMPARE(mic->solidColor, QColor("#102030"));
        QCOMPARE(mic->trigger.type, "microphone");
        QCOMPARE(mic->trigger.value, QVariant(false));
        const auto notification = parseEffectPreset(
            R"({"format":"ambient-led-effect","version":1,"id":"n","name":"Notice","type":"notifications","options":{"color":"#010203","period":500,"cycles":4,"pattern":"blink","application":"org.kde.kate","urgency":2}})",
            &error);
        QVERIFY2(notification, qPrintable(error));
        QCOMPARE(notification->type, "pulse");
        QCOMPARE(notification->trigger.type, "notification");
        QCOMPARE(notification->trigger.duration, 2000);
        QCOMPARE(notification->trigger.application, "org.kde.kate");
        QCOMPARE(notification->trigger.urgency, 2);
        QCOMPARE(notification->notification.pattern, "blink");
        for (const auto& effect : { *mic, *notification }) {
            const auto json = effectPresetJson(effect);
            QCOMPARE(json["version"].toInt(), 2);
            const auto parsed = parseEffectPreset(QJsonDocument(json).toJson(), &error);
            QVERIFY2(parsed, qPrintable(error));
            QCOMPARE(*parsed, effect);
        }
    }
    void formatVersionGuardsNewSemantics()
    {
        QString error;
        EffectPreset pulse;
        pulse.type = "pulse";
        auto json = effectPresetJson(pulse);
        json["version"] = 1;
        QVERIFY(!parseEffectPreset(QJsonDocument(json).toJson(), &error));
        json["version"] = 2;
        json["type"] = "microphone";
        QVERIFY(!parseEffectPreset(QJsonDocument(json).toJson(), &error));
        EffectTriggers triggers;
        triggers.configure({ pulse }, { pulse.id }, { });
        QVERIFY(triggers.animating());
        QVERIFY(triggers.colors().contains(pulse.id));
    }
    void conditionsApplyToAllVisualTypes()
    {
        for (const auto* type : { "solid", "brightness", "pulse" }) {
            EffectPreset p;
            p.id = "effect";
            p.type = type;
            p.trigger.type = "volume";
            p.trigger.value = 50;
            p.trigger.comparison = "ge";
            p.brightness = 30;
            EffectTriggers triggers;
            triggers.configure({ p }, { p.id }, { });
            QVERIFY(triggers.colors().isEmpty());
            QVERIFY(triggers.brightness().isEmpty());
            QVERIFY(triggers.setValue(p.id, 80));
            if (p.type == "brightness")
                QCOMPARE(triggers.brightness().value(p.id), 30);
            else
                QVERIFY(triggers.colors().contains(p.id));
            QCOMPARE(triggers.animating(), p.type == "pulse");
            if (p.type != "pulse")
                QCOMPARE(triggers.nextUpdateMs(), -1);
            QVERIFY(triggers.setValue(p.id, { }));
            QVERIFY(triggers.colors().isEmpty());
            QVERIFY(triggers.brightness().isEmpty());
            QCOMPARE(triggers.nextUpdateMs(), -1);
        }
    }
    void eventEndsAndDoesNotExtendOnBursts()
    {
        EffectPreset p;
        p.id = "event";
        p.type = "solid";
        p.trigger.type = "notification";
        p.trigger.duration = 1000;
        p.trigger.application = "org.kde.kate";
        EffectTriggers triggers;
        triggers.configure({ p }, { p.id }, { });
        QVERIFY(!triggers.notify({ "different" }));
        QVERIFY(triggers.notify({ "org.kde.kate" }));
        QCOMPARE(triggers.nextUpdateMs(), 1000);
        triggers.advance(900);
        QVERIFY(!triggers.notify({ "org.kde.kate" }));
        QCOMPARE(triggers.nextUpdateMs(), 100);
        triggers.advance(100);
        QVERIFY(triggers.colors().isEmpty());
        QCOMPARE(triggers.nextUpdateMs(), -1);
        QVERIFY(triggers.notify({ "org.kde.kate" }));
        triggers.configure({ p }, { }, { });
        QVERIFY(triggers.colors().isEmpty());
    }
    void rejectsMalformedTriggerAndAmbientCondition()
    {
        QString error;
        auto json = effectPresetJson(EffectPreset());
        TriggerOptions t;
        t.type = "microphone";
        json["trigger"] = triggerJson(t);
        QVERIFY(!parseEffectPreset(QJsonDocument(json).toJson(), &error));
        auto options = triggerJson(t);
        options["duration"] = 0;
        QVERIFY(!parseTrigger(options, &error));
        options = triggerJson(t);
        options["type"] = "unknown";
        QVERIFY(!parseTrigger(options, &error));
        t.type = "dbus";
        t.service = "org.example.Service";
        t.path = "not/a/path";
        QVERIFY(!parseTrigger(triggerJson(t), &error));
    }
};
QTEST_GUILESS_MAIN(TriggersTest)
#include "test_triggers.moc"

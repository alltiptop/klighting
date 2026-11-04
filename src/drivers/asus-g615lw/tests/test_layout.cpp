// SPDX-License-Identifier: GPL-3.0-or-later
#include "application_settings.h"
#include "device_manager.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#include <linux/input-event-codes.h>

using namespace Ambient;
class DriversTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void realPluginDescribesHardwareWithoutStartingIt()
    {
        LoadedDriver loaded({ "G615LW" }, { ASUS_DRIVER_DIR });
        QVERIFY2(loaded.driver(), qPrintable(loaded.error()));
        QCOMPARE(loaded.description().id, QStringLiteral("asus-g615lw"));
        QCOMPARE(loaded.description().keys.size(), 88);
        QCOMPARE(loaded.description().bodyColor, QColor("#333333"));
        QCOMPARE(loaded.description().keyColor, QColor("#111111"));
        QVERIFY(loaded.description().capabilities.testFlag(DeviceCapability::PerKeyColor));
        QVERIFY(loaded.description().capabilities.testFlag(DeviceCapability::LightbarPower));
        QCOMPARE(loaded.description().keys[36].id, 36);
        QCOMPARE(loaded.description().keys[36].label, QStringLiteral("W"));
        QCOMPARE(loaded.description().groups.value("WASD"), (QList<int> { 36, 50, 51, 52 }));
        const auto codes = loaded.instance().options.value("InputKeyCodes").toMap();
        QCOMPARE(codes.value("36").toUInt(), quint32(KEY_W));
        QCOMPARE(codes.value("63").toUInt(), quint32(KEY_LEFTSHIFT));
        QCOMPARE(codes.value("74").toUInt(), quint32(KEY_RIGHTSHIFT));
        QCOMPARE(codes.value("77").toUInt(), quint32(KEY_LEFTCTRL));
        QCOMPARE(codes.value("84").toUInt(), quint32(KEY_F23));
        QVERIFY(!codes.contains("78")); // Fn is not reported by this firmware.
        QVERIFY(!codes.contains("0")); // Unverified macro bindings must not be guessed.
        LoadedDriver unsupported({ "another-board" }, { ASUS_DRIVER_DIR });
        QVERIFY(!unsupported.driver());
        QVERIFY(!unsupported.error().isEmpty());
    }
    void asusStripCornersAndDiffusion()
    {
        LoadedDriver loaded({ "G615LW" }, { ASUS_DRIVER_DIR });
        QVERIFY(loaded.driver());
        const auto& strip = loaded.description().strips.first();
        QCOMPARE(strip.elements.size(), 12);
        QCOMPARE(strip.elements[1].label, QString("2"));
        QCOMPARE(strip.elements[4].label, QString("5"));
        QCOMPARE(strip.elements[7].label, QString("8"));
        QCOMPARE(strip.elements[10].label, QString("11"));
        const QList<int> cornerIds { 91, 97, 96, 90 };
        const QList<QPointF> corners { { 16, 25 }, { 16, 174 }, { 338, 174 }, { 338, 25 } };
        for (qsizetype i = 0; i < cornerIds.size(); ++i) {
            QVERIFY(std::any_of(strip.path.begin(), strip.path.end(),
                [&](const auto& p) { return p.elementId == cornerIds[i] && p.position == corners[i]; }));
            // A corner's own color never extends down an entire side.
            for (const auto& p : strip.path)
                if (p.elementId == cornerIds[i])
                    QVERIFY((p.position - corners[i]).manhattanLength() <= 1);
        }
        QCOMPARE(strip.path.first().elementId, strip.path.last().elementId);
        QCOMPARE(strip.path.first().position, strip.path.last().position);
        // Two non-corner elements cover each long side, without extra hardware IDs.
        int longTransitions = 0;
        for (qsizetype i = 1; i < strip.path.size(); ++i) {
            const auto& a = strip.path[i - 1];
            const auto& b = strip.path[i];
            if ((a.position - b.position).manhattanLength() > 100) {
                QVERIFY(!cornerIds.contains(a.elementId));
                QVERIFY(!cornerIds.contains(b.elementId));
                ++longTransitions;
            }
        }
        QCOMPARE(longTransitions, 4);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", directory.path().toUtf8());
    QCoreApplication app(argc, argv);
    DriversTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_layout.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_manager.h"
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <linux/input-event-codes.h>

using namespace Ambient;
class TufLayoutTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void describesOnlyTheVerifiedSingleZoneModel()
    {
        LoadedDriver loaded({ "FA706IHRB" }, { TUF_DRIVER_DIR });
        QVERIFY2(loaded.driver(), qPrintable(loaded.error()));
        const auto& description = loaded.description();
        QCOMPARE(description.id, QString("asus-tuf-fa706ihrb"));
        // Declared explicitly rather than through the loader's legacy "internal" fallback.
        QCOMPARE(loaded.instance().options.value("DeviceClaims").toStringList(),
            QStringList { "internal-keyboard" });
        QVERIFY(deviceClaims(loaded.devices().first()).contains("internal-keyboard"));
        QCOMPARE(description.capabilities, DeviceCapability::Color | DeviceCapability::Brightness);
        QVERIFY(description.strips.isEmpty());
        QVERIFY(validateDescription(description).isEmpty());
        QVERIFY(!loaded.driver()->createControls(loaded.instance()));
        const auto codes = loaded.instance().options.value("InputKeyCodes").toMap();
        QCOMPARE(codes.value(QString::number(KEY_W)).toInt(), KEY_W);
        QVERIFY(!codes.contains(QString::number(KEY_FN)));
        const auto key = [&](int id) {
            return std::find_if(description.keys.cbegin(), description.keys.cend(),
                [id](const auto& item) { return item.id == id; });
        };
        QVERIFY(key(KEY_ENTER) != description.keys.cend());
        QVERIFY(key(KEY_KPENTER) != description.keys.cend());
        QVERIFY(key(KEY_KPENTER)->rect.x() > key(KEY_ENTER)->rect.x());
        for (const auto& board : { "FA706IH", "FA506IHRB", "G615LW", "" }) {
            LoadedDriver other({ board }, { TUF_DRIVER_DIR });
            QVERIFY(!other.driver());
        }
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
    TufLayoutTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_layout.moc"

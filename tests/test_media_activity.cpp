// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/media/media_activity_graph.h"
#include <QTest>

using namespace Ambient;
class MediaActivityTest : public QObject
{
    Q_OBJECT
private:
    static MediaActivityGraph graph(const QVariantMap& source)
    {
        MediaActivityGraph g;
        g.nodes[1] = { source, true, true };
        g.nodes[2] = { { { "media.class", "Stream/Input/Video" } }, true, true };
        g.links[3] = { 1, 2, true, true };
        return g;
    }
private Q_SLOTS:
    void distinguishesSources_data()
    {
        QTest::addColumn<QVariantMap>("properties");
        QTest::addColumn<QString>("active");
        QTest::newRow("microphone") << QVariantMap { { "media.class", "Audio/Source" } }
                                    << "microphone-active";
        QTest::newRow("virtual-input")
            << QVariantMap { { "media.class", "Audio/Source/Virtual" } } << "microphone-active";
        QTest::newRow("speaker-monitor") << QVariantMap { { "media.class", "Audio/Sink" } } << "";
        QTest::newRow("monitor-source")
            << QVariantMap { { "media.class", "Audio/Source" }, { "device.class", "monitor" } } << "";
        QTest::newRow("v4l2-camera") << QVariantMap { { "media.class", "Video/Source" },
            { "api.v4l2.path", "/dev/video0" } }
                                     << "camera-active";
        QTest::newRow("libcamera") << QVariantMap { { "media.class", "Video/Source" },
            { "device.api", "libcamera" } }
                                   << "camera-active";
        QTest::newRow("camera-role") << QVariantMap { { "media.class", "Video/Source" },
            { "media.role", "Camera" } }
                                     << "camera-active";
        QTest::newRow("screen-role") << QVariantMap { { "media.class", "Video/Source" },
            { "media.role", "Screen" } }
                                     << "screen-capture";
        QTest::newRow("kwin-screen") << QVariantMap { { "media.class", "Video/Source" },
            { "node.name", "kwin-screencast-DP-1" } }
                                     << "screen-capture";
        QTest::newRow("kwin-window") << QVariantMap { { "media.class", "Stream/Output/Video" },
            { "media.name", "kwin-screencast-window" } }
                                     << "screen-capture";
        QTest::newRow("unidentified-video") << QVariantMap { { "media.class", "Video/Source" } } << "";
        QTest::newRow("screen-consumer")
            << QVariantMap { { "media.class", "Stream/Input/Video" }, { "media.role", "Screen" } } << "";
    }
    void distinguishesSources()
    {
        QFETCH(QVariantMap, properties);
        QFETCH(QString, active);
        const auto values = graph(properties).values();
        QCOMPARE(values.size(), 3);
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            QCOMPARE(it.value().toBool(), it.key() == active);
    }
    void requiresActiveConsumerAndSource()
    {
        auto g = graph({ { "media.class", "Audio/Source" } });
        QVERIFY(g.values()["microphone-active"].toBool());
        g.links[3].active = false;
        QVERIFY(!g.values()["microphone-active"].toBool());
        g.links[3].active = true;
        g.nodes[2].running = false;
        QVERIFY(!g.values()["microphone-active"].toBool());
        g.nodes[2].running = true;
        g.nodes[1].running = false;
        QVERIFY(!g.values()["microphone-active"].toBool());
        g.nodes[1].running = true;
        g.links.clear();
        QVERIFY(!g.values()["microphone-active"].toBool());
    }
    void remainsActiveUntilLastConsumerStops()
    {
        auto g = graph({ { "media.class", "Video/Source" }, { "media.role", "Camera" } });
        g.nodes[4] = g.nodes[2];
        g.links[5] = { 1, 4, true, true };
        g.links.remove(3);
        g.nodes.remove(2);
        QVERIFY(g.values()["camera-active"].toBool());
        g.links[5].active = false;
        QVERIFY(!g.values()["camera-active"].toBool());
    }
    void incompleteGraphIsUnknown()
    {
        auto g = graph({ { "media.class", "Audio/Source" } });
        g.nodes[1].known = false;
        QVERIFY(g.values().isEmpty());
        g.nodes[1].known = true;
        g.links[3].known = false;
        QVERIFY(g.values().isEmpty());
        g.links[3].known = true;
        g.nodes.remove(1);
        QVERIFY(g.values().isEmpty());
        g.links.clear();
        QCOMPARE(g.values()["microphone-active"], QVariant(false));
    }
};
QTEST_GUILESS_MAIN(MediaActivityTest)
#include "test_media_activity.moc"

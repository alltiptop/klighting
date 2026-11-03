// SPDX-License-Identifier: GPL-3.0-or-later
#include <KCMultiDialog>
#include <KPluginMetaData>
#include <KQuickConfigModule>
#include <QApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <linux/input-event-codes.h>
class QuickTest : public QObject
{
    Q_OBJECT
    static QQuickItem* itemNamed(QQuickItem* root, const QString& name)
    {
        if (root->isVisible() && root->objectName() == name)
            return root;
        for (auto* child : root->childItems())
            if (auto* found = itemNamed(child, name))
                return found;
        return nullptr;
    }
    static bool inView(QQuickItem* item, QQuickWidget* host)
    {
        if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0)
            return false;
        const auto rect = item->mapRectToScene(item->boundingRect());
        return rect.left() >= -1 && rect.right() <= host->width() + 1 && rect.top() >= -1
            && rect.top() < host->height();
    }
    static bool beside(QQuickItem* a, QQuickItem* b, QQuickWidget* host)
    {
        return inView(a, host) && inView(b, host)
            && a->mapRectToScene(a->boundingRect()).right()
            <= b->mapRectToScene(b->boundingRect()).left() + 1;
    }
    static QQuickItem* buttonWithText(QQuickItem* item, const QString& text)
    {
        if (item->isVisible() && item->property("text").toString() == text)
            return item;
        for (auto* child : item->childItems())
            if (auto* button = buttonWithText(child, text))
                return button;
        return nullptr;
    }
    static void recordChord(QWindow* window, const QList<QPair<quint32, int>>& keys)
    {
        for (const auto& [code, key] : keys) {
            QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, code + 8, 0, 0);
            QApplication::sendEvent(window, &event);
        }
        for (const auto& [code, key] : keys) {
            QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier, code + 8, 0, 0);
            QApplication::sendEvent(window, &event);
        }
    }
    static bool snapshot(KCMultiDialog& dialog, const QString& name)
    {
        QTest::qWait(100);
        const auto dir = qEnvironmentVariable("AMBIENT_QUICK_PREVIEW");
        return dir.isEmpty() || dialog.grab().save(dir + "/" + name + ".png");
    }
private Q_SLOTS:
    void brightnessEditorTracksSelectedPreset()
    {
        KCMultiDialog dialog;
        QVERIFY(
            dialog.addModule(KPluginMetaData(QString(CONFIG_SECTION_DIR) + "/kcm_ambientled_effects.so")));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1100, 850);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host && host->rootObject());
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 2);
        auto* settings = module->property("controller").value<QObject*>();
        auto* add = module->mainUi()->findChild<QObject*>("AddBrightnessEffect");
        QVERIFY(settings && add);
        QVERIFY(QMetaObject::invokeMethod(add, "triggered"));
        auto* editor = module->subPage(0);
        QObject *slider = nullptr, *field = nullptr;
        QTRY_VERIFY((slider = editor->findChild<QObject*>("BrightnessSlider")));
        QTRY_VERIFY((field = editor->findChild<QObject*>("BrightnessValue")));
        const int first = settings->property("selectedEffect").toInt();
        QCOMPARE(slider->property("value").toInt(), 100);
        slider->setProperty("value", 35);
        QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
        QTRY_COMPARE(field->property("value").toInt(), 35);
        QCOMPARE(settings->property("effectPreset").toMap()["brightness"].toInt(), 35);
        QVERIFY(QMetaObject::invokeMethod(add, "triggered"));
        QTRY_COMPARE(slider->property("value").toInt(), 100);
        field->setProperty("value", 0);
        QVERIFY(QMetaObject::invokeMethod(field, "valueModified"));
        QTRY_COMPARE(slider->property("value").toInt(), 0);
        settings->setProperty("selectedEffect", first);
        QTRY_COMPARE(field->property("value").toInt(), 35);
        QTRY_COMPARE(slider->property("value").toInt(), 35);
    }
    void solidColorSwitching()
    {
        KCMultiDialog dialog;
        QVERIFY(
            dialog.addModule(KPluginMetaData(QString(CONFIG_SECTION_DIR) + "/kcm_ambientled_effects.so")));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1100, 850);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host && host->rootObject());
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 2);
        auto* settings = module->property("controller").value<QObject*>();
        auto* add = module->mainUi()->findChild<QObject*>("AddSolidEffect");
        QVERIFY(settings && add);
        QVERIFY(QMetaObject::invokeMethod(add, "triggered"));
        auto* editor = module->subPage(0);
        QObject* field = nullptr;
        QTRY_VERIFY((field = editor->findChild<QObject*>("HexColorInput")));
        const int first = settings->property("selectedEffect").toInt();
        auto* picker = editor->findChild<QObject*>("ColorPicker");
        QVERIFY(picker);
        QVERIFY(QMetaObject::invokeMethod(editor->findChild<QObject*>("ColorSwatch"), "clicked"));
        picker->setProperty("color", QColor("#c05020"));
        QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
        QCOMPARE(field->property("text").toString(), QString("#c05020"));
        QVERIFY(QMetaObject::invokeMethod(add, "triggered"));
        const int second = settings->property("selectedEffect").toInt();
        QVERIFY(first != second);
        // The same SolidPage stays loaded, but must show the newly selected definition.
        QTRY_COMPARE(field->property("text").toString(), QString("#ffffff"));
        field->setProperty("text", "#2080c0");
        QVERIFY(QMetaObject::invokeMethod(field, "textEdited"));
        settings->setProperty("selectedEffect", first);
        QTRY_COMPARE(field->property("text").toString(), QString("#c05020"));
        QCOMPARE(settings->property("effectPreset").toMap()["solidColor"].toString(), QString("#c05020"));
        QVERIFY(QMetaObject::invokeMethod(editor->findChild<QObject*>("ColorSwatch"), "clicked"));
        QCOMPARE(picker->property("currentColor").value<QColor>(), QColor("#c05020"));
        QVERIFY(QMetaObject::invokeMethod(picker, "reject"));
        settings->setProperty("selectedEffect", second);
        QTRY_COMPARE(field->property("text").toString(), QString("#2080c0"));
        auto* selected = itemNamed(module->mainUi(), "Effect" + QString::number(second));
        QVERIFY(selected);
        auto* circle = itemNamed(selected, "SolidColorCircle");
        QVERIFY(circle);
        QCOMPARE(circle->property("color").value<QColor>(), QColor("#2080c0"));
        QVERIFY(snapshot(dialog, "solid-color-sidebar"));
        QCOMPARE(settings->property("effectPreset").toMap()["solidColor"].toString(), QString("#2080c0"));
        module->load();
    }
    void sharedEffectPreviewInPreset()
    {
        KCMultiDialog dialog;
        QVERIFY(
            dialog.addModule(KPluginMetaData(QString(CONFIG_SECTION_DIR) + "/kcm_ambientled_presets.so")));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1600, 1100);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host && host->rootObject());
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 2);
        auto* settings = module->property("controller").value<QObject*>();
        auto* editor = module->subPage(0);
        QStringList errors;
        connect(module->engine().get(), &QQmlEngine::warnings, this, [&](const QList<QQmlError>& warnings) {
            for (const auto& error : warnings)
                errors << error.toString();
        });
        QQuickItem* ambientRow = nullptr;
        QTRY_VERIFY((ambientRow = itemNamed(editor, "AssignedEffect:ambient-default")));
        QVERIFY(!itemNamed(ambientRow, "FramesPerSecond"));
        QVERIFY(!module->needsSave());
        QVERIFY(
            QMetaObject::invokeMethod(ambientRow->findChild<QObject*>("ExpandAssignedEffect"), "clicked"));
        QTRY_VERIFY(itemNamed(ambientRow, "FramesPerSecond"));
        QVERIFY(itemNamed(ambientRow, "SharedEffectWarning"));
        QVERIFY(!module->needsSave()); // Expanding a shared preset never copies or edits it.
        auto* fps = itemNamed(ambientRow, "FramesPerSecond");
        fps->setProperty("value", 5);
        QVERIFY(QMetaObject::invokeMethod(fps, "valueModified"));
        QCOMPARE(settings->property("effectPreset").toMap()["fps"].toInt(), 5);
        QVERIFY(
            QMetaObject::invokeMethod(settings, "addEffect", Q_ARG(QString, "solid"), Q_ARG(bool, false)));
        const auto id = settings->property("effectPreset").toMap()["id"].toString();
        QVERIFY(QMetaObject::invokeMethod(
            settings, "setSolidColor", Q_ARG(QString, "#2040c0"), Q_ARG(QString, id)));
        auto* add = editor->findChild<QObject*>("AddAssignedEffect:" + id);
        QVERIFY(add && QMetaObject::invokeMethod(add, "triggered"));
        settings->setProperty("selectedEffect", 0);
        QQuickItem* row = nullptr;
        QTRY_VERIFY((row = itemNamed(editor, "AssignedEffect:" + id)));
        QVERIFY(!itemNamed(row, "HexColorInput"));
        QVERIFY(QMetaObject::invokeMethod(row->findChild<QObject*>("ExpandAssignedEffect"), "clicked"));
        QTRY_VERIFY(itemNamed(row, "HexColorInput"));
        QVERIFY(!itemNamed(ambientRow, "FramesPerSecond"));
        auto* color = itemNamed(row, "HexColorInput");
        QCOMPARE(color->property("text").toString(), QString("#2040c0"));
        QVERIFY(itemNamed(row, "SharedEffectWarning"));
        auto* picker = row->findChild<QObject*>("ColorPicker");
        QVERIFY(picker);
        QVERIFY(QMetaObject::invokeMethod(row->findChild<QObject*>("ColorSwatch"), "clicked"));
        picker->setProperty("color", QColor("#e08020"));
        QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
        QCOMPARE(color->property("text").toString(), QString("#e08020"));
        QVariantMap options;
        QVERIFY(QMetaObject::invokeMethod(
            settings, "assignedEffectOptions", Q_RETURN_ARG(QVariantMap, options), Q_ARG(QString, id)));
        QCOMPARE(options["solidColor"].toString(), QString("#e08020"));
        QCOMPARE(settings->property("selectedEffect").toInt(), 0);
        auto* icon = itemNamed(row, "AssignedEffectIcon");
        QVERIFY(icon);
        auto* circle = itemNamed(icon, "SolidColorCircle");
        QVERIFY(circle);
        QCOMPARE(circle->property("color").value<QColor>(), QColor("#e08020"));
        QCOMPARE(circle->width(), circle->height());
        QCOMPARE(circle->property("radius").toReal(), circle->width() / 2);
        auto* keyboard = itemNamed(editor, "KeyboardView");
        auto* assigned = itemNamed(editor, "AssignedEffectsEditor");
        auto* keyboardPage = editor->findChild<QObject*>("KeyboardPage");
        QVERIFY(keyboard && assigned && keyboardPage);
        const auto maximum = keyboardPage->property("formMaximumWidth").toReal();
        QVERIFY(assigned->width() <= maximum + 1);
        QVERIFY(keyboard->width() > assigned->width());
        auto* flick = keyboardPage->property("flickable").value<QObject*>();
        QVERIFY(flick);
        flick->setProperty(
            "contentY", flick->property("contentHeight").toReal() - flick->property("height").toReal());
        QTest::qWait(250); // Let the standard InlineMessage finish its reveal animation.
        QVERIFY(snapshot(dialog, "shared-effect-expanded-wide"));
        dialog.resize(500, 1100);
        QTRY_VERIFY(inView(editor, host));
        flick->setProperty(
            "contentY", flick->property("contentHeight").toReal() - flick->property("height").toReal());
        QTest::qWait(250);
        flick->setProperty(
            "contentY", flick->property("contentHeight").toReal() - flick->property("height").toReal());
        QVERIFY(snapshot(dialog, "shared-effect-expanded-narrow"));
        QVERIFY(QMetaObject::invokeMethod(row->findChild<QObject*>("ExpandAssignedEffect"), "clicked"));
        QTRY_VERIFY(!itemNamed(row, "HexColorInput"));
        QVERIFY(itemNamed(row, "AssignedEffectIcon"));
        module->load();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void nativeSectionNavigation_data()
    {
        QTest::addColumn<QString>("section");
        QTest::addColumn<QString>("controllerSection");
        QTest::addColumn<QString>("sidebar");
        QTest::newRow("Devices") << "devices" << "base" << "DeviceSidebar";
        QTest::newRow("Presets") << "presets" << "keyboard" << "PresetSidebar";
        QTest::newRow("Effects") << "effects" << "effects" << "EffectSidebar";
        QTest::newRow("Applications") << "applications" << "applications" << "ApplicationSidebar";
    }
    void nativeSectionNavigation()
    {
        QFETCH(QString, section);
        QFETCH(QString, controllerSection);
        QFETCH(QString, sidebar);
        const KPluginMetaData metadata(QString(CONFIG_SECTION_DIR) + "/kcm_ambientled_" + section + ".so");
        QVERIFY(metadata.isValid());
        QVERIFY(!metadata.isHidden());
        QCOMPARE(metadata.value("X-KDE-System-Settings-Parent-Category"), QString("ambientled"));
        QVERIFY(KPluginMetaData(CONFIG_PLUGIN_PATH).isHidden());
        QStringList errors;
        KCMultiDialog dialog;
        QVERIFY(dialog.addModule(metadata));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1100, 850);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host && host->rootObject());
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module && module->property("nativeSection").toBool());
        connect(module->engine().get(), &QQmlEngine::warnings, this, [&](const QList<QQmlError>& warnings) {
            for (const auto& error : warnings)
                errors << error.toString();
        });
        QTRY_COMPARE(module->depth(), 2);
        auto* list = module->mainUi();
        auto* editor = module->subPage(0);
        QCOMPARE(list->objectName(), sidebar);
        // The section menu belongs to System Settings, outside the module and its footer.
        QVERIFY(!itemNamed(host->rootObject(), "SectionSidebar"));
        QTRY_VERIFY(beside(list, editor, host));
        auto* separator = itemNamed(list, "SidebarSeparator");
        QVERIFY(separator && separator->isVisible());
        QVERIFY(separator->width() <= 2);
        QCOMPARE(separator->height(), list->height());
        auto* settings = module->property("controller").value<QObject*>();
        QCOMPARE(settings->property("section").toString(), controllerSection);
        QVERIFY(
            QMetaObject::invokeMethod(settings, "setName", Q_ARG(QString, QString("Resize retains edits"))));
        QVERIFY(module->needsSave());
        QVERIFY(snapshot(dialog, "native-" + section + "-wide"));
        dialog.resize(500, 850);
        QTRY_VERIFY(inView(editor, host));
        QTRY_VERIFY(!inView(list, host));
        QVERIFY(editor->width() >= host->width() - 30);
        QVERIFY(snapshot(dialog, "native-" + section + "-narrow"));
        auto* back = buttonWithText(host->rootObject(), "Navigate Back");
        QVERIFY(back && back->isEnabled());
        QTest::mouseClick(
            host, Qt::LeftButton, Qt::NoModifier, back->mapToScene(back->boundingRect().center()).toPoint());
        QTRY_VERIFY(inView(list, host));
        QVERIFY(QMetaObject::invokeMethod(list, "showEditor"));
        QTRY_VERIFY(inView(editor, host));
        dialog.resize(1100, 850);
        QTRY_VERIFY(beside(list, editor, host));
        QCOMPARE(settings->property("preset").toMap()["name"].toString(), QString("Resize retains edits"));
        QVERIFY(module->needsSave());
        module->load();
        QVERIFY(!module->needsSave());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void sidebarFiltersKeepOriginalSelection_data()
    {
        QTest::addColumn<QString>("section");
        QTest::addColumn<QString>("prefix");
        QTest::newRow("Applications") << "applications" << "Application";
        QTest::newRow("Presets") << "presets" << "Preset";
        QTest::newRow("Effects") << "effects" << "Effect";
    }
    void sidebarFiltersKeepOriginalSelection()
    {
        QFETCH(QString, section);
        QFETCH(QString, prefix);
        KCMultiDialog dialog;
        QVERIFY(dialog.addModule(
            KPluginMetaData(QString(CONFIG_SECTION_DIR) + "/kcm_ambientled_" + section + ".so")));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1100, 850);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host);
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 2);
        auto* settings = module->property("controller").value<QObject*>();
        QVERIFY(settings);
        if (section == "applications") {
            for (int i = 0; i < 2; ++i) {
                int index = -1;
                QVERIFY(QMetaObject::invokeMethod(settings, "addRule", Q_RETURN_ARG(int, index)));
                QCOMPARE(index, i);
                QVERIFY(QMetaObject::invokeMethod(settings, "setRule", Q_ARG(int, index),
                    Q_ARG(QString, QString("name")), Q_ARG(QVariant, QVariant(i == 0 ? "First" : "Second"))));
            }
            QVERIFY(QMetaObject::invokeMethod(settings, "setRule", Q_ARG(int, 1),
                Q_ARG(QString, QString("windowClass")), Q_ARG(QVariant, QVariant("org.example.Needle"))));
            module->mainUi()->setProperty("selectedRule", 0);
        } else if (section == "presets") {
            QVERIFY(QMetaObject::invokeMethod(settings, "setName", Q_ARG(QString, QString("First"))));
            bool added = false;
            QVERIFY(QMetaObject::invokeMethod(
                settings, "addPreset", Q_RETURN_ARG(bool, added), Q_ARG(bool, false)));
            QVERIFY(added);
            QVERIFY(QMetaObject::invokeMethod(settings, "setName", Q_ARG(QString, QString("Needle"))));
            settings->setProperty("selected", 0);
        } else {
            QVERIFY(QMetaObject::invokeMethod(settings, "setEffectName", Q_ARG(QString, QString("First"))));
            bool added = false;
            QVERIFY(QMetaObject::invokeMethod(settings, "addEffect", Q_RETURN_ARG(bool, added),
                Q_ARG(QString, QString("ambient")), Q_ARG(bool, false)));
            QVERIFY(added);
            QVERIFY(QMetaObject::invokeMethod(settings, "setEffectName", Q_ARG(QString, QString("Needle"))));
            settings->setProperty("selectedEffect", 0);
        }
        auto* search = itemNamed(host->rootObject(), prefix + "Search");
        auto* list = itemNamed(host->rootObject(), prefix + "List");
        QVERIFY(search && list);
        const bool dirty = settings->property("dirty").toBool();
        search->setProperty("text", "  nEEdLe  ");
        QTRY_COMPARE(list->property("count").toInt(), 1);
        auto* result = itemNamed(list, prefix + "1");
        QTRY_VERIFY((result = itemNamed(list, prefix + "1")));
        QVERIFY(inView(result, host));
        QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
            result->mapToScene(result->boundingRect().center()).toPoint());
        if (section == "applications")
            QCOMPARE(module->mainUi()->property("selectedRule").toInt(), 1);
        else if (section == "presets")
            QCOMPARE(settings->property("selected").toInt(), 1);
        else
            QCOMPARE(settings->property("selectedEffect").toInt(), 1);
        search->setProperty("text", "no such result");
        QTRY_COMPARE(list->property("count").toInt(), 0);
        search->setProperty("text", "");
        QTRY_COMPARE(list->property("count").toInt(), 2);
        QCOMPARE(settings->property("dirty").toBool(), dirty);
    }
    void layerListRecordsReplacesAndRejectsDuplicates()
    {
        KCMultiDialog dialog;
        QVERIFY(
            dialog.addModule(KPluginMetaData(QString(CONFIG_SECTION_DIR) + "/kcm_ambientled_presets.so")));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1240, 950);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host);
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 2);
        auto* editor = module->subPage(0);
        auto* settings = module->property("controller").value<QObject*>();
        auto* recorder = module->property("recorder").value<QObject*>();
        auto* keyboard = editor->findChild<QQuickItem*>("KeyboardView");
        QVERIFY(settings && recorder && keyboard);
        const auto layers = [&] { return settings->property("preset").toMap()["layers"].toList(); };
        const auto click = [&](const QString& name) {
            auto* button = itemNamed(editor, name);
            return button && QMetaObject::invokeMethod(button, "clicked");
        };
        QCOMPARE(layers().size(), 1);
        auto* livePreview = itemNamed(editor, "LivePreview");
        auto* addLayer = itemNamed(editor, "AddLayer");
        auto* base = itemNamed(editor, "LayerChoiceBase");
        QVERIFY(livePreview && addLayer && base);
        QVERIFY(!livePreview->property("checked").toBool());
        QTRY_VERIFY(beside(livePreview, addLayer, host));
        QCOMPARE(base->height(), itemNamed(editor, "LayerRow0")->height());
        QVERIFY(itemNamed(editor, "LayerSeparator0"));
        QVERIFY(!editor->findChild<QObject*>("LayerSelector"));
        QVERIFY(click("AddLayer"));
        QVERIFY(recorder->property("recording").toBool());
        QCOMPARE(layers().size(), 1); // A pending capture is not a layer.
        recordChord(dialog.windowHandle(), { { KEY_ESC, Qt::Key_Escape } });
        QVERIFY(!recorder->property("recording").toBool());
        QCOMPARE(layers().size(), 1);
        QVERIFY(!module->needsSave());
        QVERIFY(click("AddLayer"));
        recordChord(
            dialog.windowHandle(), { { KEY_LEFTMETA, Qt::Key_Meta }, { KEY_RIGHTSHIFT, Qt::Key_Shift } });
        QTRY_COMPARE(layers().size(), 2);
        QCOMPARE(keyboard->property("layerIndex").toInt(), 1);
        QTRY_VERIFY(itemNamed(editor, "LayerChoice1"));
        QTRY_COMPARE(itemNamed(editor, "LayerRow1")->height(), base->height());
        QVERIFY(itemNamed(editor, "LayerSeparator1"));
        QCOMPARE(itemNamed(editor, "LayerChoice1")->property("text").toString(),
            QString("Meta (Left) + Shift (Right)"));
        QVERIFY(QMetaObject::invokeMethod(settings, "assignKeys", Q_ARG(int, 1),
            Q_ARG(QVariantList, QVariantList { 36 }), Q_ARG(int, 1), Q_ARG(QString, "#00ff00")));
        QVERIFY(click("RecordLayerKeys1"));
        recordChord(
            dialog.windowHandle(), { { KEY_LEFTCTRL, Qt::Key_Control }, { KEY_RIGHTSHIFT, Qt::Key_Shift } });
        QCOMPARE(layers().size(), 2);
        QCOMPARE(
            layers()[1].toMap()["conditions"].toStringList(), (QStringList { "ctrl-left", "shift-right" }));
        QCOMPARE(keyboard->property("preview").toList()[36].toMap()["color"].toString(), QString("#00ff00"));
        const auto beforeDuplicate = layers();
        for (const auto& button : { QString("AddLayer"), QString("RecordLayerKeys0") }) {
            QVERIFY(click(button));
            recordChord(dialog.windowHandle(),
                { { KEY_RIGHTSHIFT, Qt::Key_Shift }, { KEY_LEFTCTRL, Qt::Key_Control } });
            QCOMPARE(layers(), beforeDuplicate);
            QVERIFY(!settings->property("error").toString().isEmpty());
        }
        // Let the duplicate-error banner finish opening before locating the radio text.
        QTest::qWait(250);
        QVERIFY(snapshot(dialog, "layers-duplicate"));
        // Clicking the text of the radio button selects that row without editing its combination.
        auto* first = itemNamed(editor, "LayerChoice0");
        QVERIFY(first && inView(first, host));
        QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
            first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint());
        QCOMPARE(keyboard->property("layerIndex").toInt(), 0);
        QVERIFY(click("LayerChoice1"));
        QVERIFY(click("RemoveLayer0"));
        QCOMPARE(layers().size(), 1);
        QCOMPARE(keyboard->property("layerIndex").toInt(), 0);
        QCOMPARE(
            layers()[0].toMap()["conditions"].toStringList(), (QStringList { "ctrl-left", "shift-right" }));
        QVERIFY(snapshot(dialog, "layers-list"));
        dialog.resize(500, 950);
        QTest::qWait(150);
        auto* radio = itemNamed(editor, "LayerChoice0");
        auto* replace = itemNamed(editor, "RecordLayerKeys0");
        auto* remove = itemNamed(editor, "RemoveLayer0");
        QVERIFY(inView(radio, host) && inView(replace, host) && inView(remove, host));
        QVERIFY(beside(radio, replace, host) && beside(replace, remove, host));
        QCOMPARE(base->height(), itemNamed(editor, "LayerRow0")->height());
        QVERIFY(beside(livePreview, addLayer, host));
        QVERIFY(snapshot(dialog, "layers-list-narrow"));
        QVERIFY(click("RemoveLayer0"));
        QCOMPARE(layers().size(), 0);
        QCOMPARE(keyboard->property("layerIndex").toInt(), -1);
        QVERIFY(itemNamed(editor, "LayerChoiceBase")->property("checked").toBool());
        module->load();
    }
    void keyboardDragSurvivesHoldAndScrolling()
    {
        KCMultiDialog dialog;
        QVERIFY(dialog.addModule(KPluginMetaData(CONFIG_PLUGIN_PATH)));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1440, 700);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host);
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 3);
        QVERIFY(QMetaObject::invokeMethod(module->mainUi(), "selectSection", Q_ARG(QVariant, 1)));
        QTRY_COMPARE(module->depth(), 3);
        QTRY_VERIFY(beside(module->mainUi(), module->subPage(0), host)
            && beside(module->subPage(0), module->subPage(1), host));
        auto* keyboard = module->subPage(1)->findChild<QQuickItem*>("KeyboardView");
        QVERIFY(keyboard);
        const auto key = [&](int id) -> QQuickItem* {
            for (auto* child : keyboard->childItems())
                if (child->property("modelData").toMap().value("id", -1).toInt() == id)
                    return child;
            return nullptr;
        };
        QVERIFY(!module->subPage(1)->findChild<QObject*>("LayerPreviewStatus"));
        auto* first = key(36);
        auto* last = key(52);
        QVERIFY(first && last);
        QTRY_VERIFY(inView(first, host) && inView(last, host));
        const auto from = first->mapToScene(first->boundingRect().center()).toPoint();
        const auto to = last->mapToScene(last->boundingRect().center()).toPoint();
        QTest::mousePress(host, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 8; ++step)
            QTest::mouseMove(host, from + (to - from) * step / 8, 20);
        QTest::qWait(700);
        QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, to);
        const auto selected = keyboard->property("selection").toList();
        QVERIFY(selected.contains(36));
        QVERIFY(selected.contains(52));
        QTest::qWait(700);
        QCOMPARE(keyboard->property("selection").toList(), selected);
        QCOMPARE(first->mapToScene(first->boundingRect().center()).toPoint(), from);
        auto* settings = module->property("controller").value<QObject*>();
        QVERIFY(settings);
        for (const int layer : { -1, 0 })
            QVERIFY(QMetaObject::invokeMethod(settings, "assignKeys", Q_ARG(int, layer),
                Q_ARG(QVariantList, QVariantList { 36 }), Q_ARG(int, 1),
                Q_ARG(QString, layer < 0 ? "#0000ff" : "#ff0000")));
        const auto color
            = [&] { return keyboard->property("preview").toList()[36].toMap()["color"].toString(); };
        QTRY_COMPARE(color(), QString("#0000ff"));
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier, KEY_RIGHTSHIFT + 8, 0, 0);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Shift, Qt::ShiftModifier, KEY_RIGHTSHIFT + 8, 0, 0);
        auto* livePreview = itemNamed(module->subPage(1), "LivePreview");
        QVERIFY(livePreview && !livePreview->property("checked").toBool());
        QApplication::sendEvent(dialog.windowHandle(), &press);
        QVERIFY(!keyboard->property("livePreview").toBool());
        QCOMPARE(color(), QString("#0000ff"));
        QCOMPARE(keyboard->property("selection").toList(), selected);
        QApplication::sendEvent(dialog.windowHandle(), &release);
        const bool dirtyBeforePreview = settings->property("dirty").toBool();
        livePreview->setProperty("checked", true);
        QCOMPARE(settings->property("dirty").toBool(), dirtyBeforePreview);
        QApplication::sendEvent(dialog.windowHandle(), &press);
        QTRY_VERIFY(keyboard->property("livePreview").toBool());
        QCOMPARE(color(), QString("#ff0000"));
        QVERIFY(key(74)->property("previewPressed").toBool());
        QVERIFY(!key(63)->property("previewPressed").toBool());
        QVERIFY(itemNamed(key(74), "KeySelectionBorder"));
        QCOMPARE(itemNamed(key(74), "KeyColorBorder")->property("color").value<QColor>(), QColor("#111111"));
        QVERIFY(snapshot(dialog, "held-key-outline"));
        QCOMPARE(keyboard->property("layerIndex").toInt(), -1);
        QCOMPARE(keyboard->property("selection").toList(), selected);
        QApplication::sendEvent(dialog.windowHandle(), &release);
        QTRY_VERIFY(!keyboard->property("livePreview").toBool());
        QVERIFY(!key(74)->property("previewPressed").toBool());
        QVERIFY(!itemNamed(key(74), "KeySelectionBorder"));
        QCOMPARE(color(), QString("#0000ff"));
        QCOMPARE(keyboard->property("selection").toList(), selected);
        // Restore a manually selected layer, not always Base.
        int upper = -1;
        QVERIFY(QMetaObject::invokeMethod(settings, "addLayer", Q_RETURN_ARG(int, upper),
            Q_ARG(QStringList, QStringList { "shift-right" })));
        QVERIFY(QMetaObject::invokeMethod(settings, "assignKeys", Q_ARG(int, upper),
            Q_ARG(QVariantList, QVariantList { 36 }), Q_ARG(int, 1), Q_ARG(QString, "#00ff00")));
        auto* layers = itemNamed(module->subPage(1), "LayerChoice0");
        QVERIFY(layers);
        QVERIFY(QMetaObject::invokeMethod(layers, "clicked"));
        keyboard->setProperty("selection", selected);
        QTRY_COMPARE(color(), QString("#ff0000"));
        QApplication::sendEvent(dialog.windowHandle(), &press);
        QTRY_COMPARE(color(), QString("#00ff00"));
        QCOMPARE(keyboard->property("layerIndex").toInt(), 0);
        QCOMPARE(keyboard->property("selection").toList(), selected);
        QApplication::sendEvent(dialog.windowHandle(), &release);
        QTRY_COMPARE(color(), QString("#ff0000"));
        QCOMPARE(keyboard->property("selection").toList(), selected);
        // Explicit combination recording temporarily suspends passive preview.
        auto* recorder = module->property("recorder").value<QObject*>();
        QVERIFY(recorder);
        QVERIFY(QMetaObject::invokeMethod(recorder, "start", Q_ARG(QWindow*, host->quickWindow())));
        QVERIFY(recorder->property("recording").toBool());
        QApplication::sendEvent(dialog.windowHandle(), &press);
        QVERIFY(!keyboard->property("livePreview").toBool());
        QApplication::sendEvent(dialog.windowHandle(), &release);
        QVERIFY(!recorder->property("recording").toBool());
        QApplication::sendEvent(dialog.windowHandle(), &press);
        QTRY_VERIFY(keyboard->property("livePreview").toBool());
        QApplication::sendEvent(dialog.windowHandle(), &release);
        QTRY_VERIFY(!keyboard->property("livePreview").toBool());
        // Ctrl-click selection must not activate a Ctrl layer while Live Preview is off.
        livePreview->setProperty("checked", false);
        QVERIFY(QMetaObject::invokeMethod(
            settings, "setLayer", Q_ARG(int, 0), Q_ARG(QStringList, QStringList { "ctrl" })));
        QVERIFY(QMetaObject::invokeMethod(itemNamed(module->subPage(1), "LayerChoiceBase"), "clicked"));
        keyboard->setProperty("selection", selected);
        QKeyEvent ctrlPress(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier, KEY_LEFTCTRL + 8, 0, 0);
        QKeyEvent ctrlRelease(
            QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier, KEY_LEFTCTRL + 8, 0, 0);
        QApplication::sendEvent(dialog.windowHandle(), &ctrlPress);
        QTest::qWait(150); // Let the selected layer's layout settle before clicking a key.
        QTest::mouseClick(host, Qt::LeftButton, Qt::ControlModifier,
            first->mapToScene(first->boundingRect().center()).toPoint());
        QVERIFY(!keyboard->property("livePreview").toBool());
        QCOMPARE(color(), QString("#0000ff"));
        auto ctrlSelection = selected;
        ctrlSelection.removeOne(36);
        QCOMPARE(keyboard->property("selection").toList(), ctrlSelection);
        QApplication::sendEvent(dialog.windowHandle(), &ctrlRelease);
        livePreview->setProperty("checked", true);
        QApplication::sendEvent(dialog.windowHandle(), &ctrlPress);
        QTRY_COMPARE(color(), QString("#ff0000"));
        livePreview->setProperty("checked", false); // Restore the edited layer even with Ctrl held.
        QTRY_COMPARE(color(), QString("#0000ff"));
        QCOMPARE(keyboard->property("selection").toList(), ctrlSelection);
        QApplication::sendEvent(dialog.windowHandle(), &ctrlRelease);
        QVERIFY(!keyboard->property("livePreview").toBool());
        QVERIFY(!key(77)->property("previewPressed").toBool());
        // Ordinary typing is outlined even without a matching layer.
        livePreview->setProperty("checked", true);
        QKeyEvent letterPress(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, KEY_W + 8, 0, 0);
        QKeyEvent letterRelease(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier, KEY_W + 8, 0, 0);
        QApplication::sendEvent(dialog.windowHandle(), &letterPress);
        QVERIFY(!keyboard->property("livePreview").toBool());
        QVERIFY(first->property("previewPressed").toBool());
        QCOMPARE(keyboard->property("selection").toList(), ctrlSelection);
        QApplication::sendEvent(dialog.windowHandle(), &letterRelease);
        QVERIFY(!first->property("previewPressed").toBool());
    }
    void notificationsEditorAndAssignments()
    {
        QStringList errors;
        KCMultiDialog dialog;
        QVERIFY(dialog.addModule(KPluginMetaData(CONFIG_PLUGIN_PATH)));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1240, 950);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host && host->rootObject());
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        auto* root = module->mainUi();
        QVERIFY(root);
        QTRY_COMPARE(module->depth(), 3);
        connect(module->engine().get(), &QQmlEngine::warnings, this, [&](const QList<QQmlError>& warnings) {
            for (const auto& e : warnings)
                errors << e.toString();
        });
        auto* settings = module->property("controller").value<QObject*>();
        QVERIFY(settings);
        QVERIFY(QMetaObject::invokeMethod(root, "selectSection", Q_ARG(QVariant, 2)));
        QTRY_COMPARE(module->depth(), 3);
        auto* add = module->subPage(0)->findChild<QObject*>("AddNotificationsEffect");
        QVERIFY(add);
        QVERIFY(QMetaObject::invokeMethod(add, "triggered"));
        auto* editor = module->subPage(1);
        QTRY_VERIFY(editor->findChild<QObject*>("NotificationsEditor"));
        auto* trigger = editor->findChild<QObject*>("NotificationTrigger");
        auto* applicationFilter = editor->findChild<QQuickItem*>("NotificationApplication");
        QVERIFY(trigger && applicationFilter);
        trigger->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(trigger, "activated", Q_ARG(int, 1)));
        QCOMPARE(
            settings->property("effectPreset").toMap()["trigger"].toString(), QString("power-connected"));
        QVERIFY(!applicationFilter->isVisible());
        trigger->setProperty("currentIndex", 0);
        QVERIFY(QMetaObject::invokeMethod(trigger, "activated", Q_ARG(int, 0)));
        QCOMPARE(settings->property("effectPreset").toMap()["trigger"].toString(), QString("notification"));
        QVERIFY(applicationFilter->isVisible());
        auto* pattern = editor->findChild<QObject*>("NotificationPattern");
        QVERIFY(pattern);
        pattern->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(pattern, "activated", Q_ARG(int, 2)));
        QCOMPARE(settings->property("effectPreset").toMap()["pattern"].toString(), QString("double"));
        auto* urgency = editor->findChild<QObject*>("NotificationUrgency");
        QVERIFY(urgency);
        urgency->setProperty("currentIndex", 3);
        QVERIFY(QMetaObject::invokeMethod(urgency, "activated", Q_ARG(int, 3)));
        QCOMPARE(settings->property("effectPreset").toMap()["urgency"].toInt(), 2);
        auto* app = editor->findChild<QObject*>("NotificationApplication");
        QVERIFY(app);
        app->setProperty("text", "org.kde.kate");
        QVERIFY(QMetaObject::invokeMethod(app, "textEdited"));
        QCOMPARE(
            settings->property("effectPreset").toMap()["application"].toString(), QString("org.kde.kate"));
        auto* choose = editor->findChild<QObject*>("ChooseNotificationColor");
        auto* picker = editor->findChild<QObject*>("NotificationColorDialog");
        QVERIFY(choose && picker);
        QVERIFY(QMetaObject::invokeMethod(choose, "clicked"));
        QTRY_VERIFY(picker->property("visible").toBool());
        picker->setProperty("color", QColor("#ff8800"));
        QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
        QTRY_VERIFY(!picker->property("visible").toBool());
        QCOMPARE(
            settings->property("effectPreset").toMap()["notificationColor"].toString(), QString("#ff8800"));
        const auto id = settings->property("effectPreset").toMap()["id"].toString();
        QVERIFY(snapshot(dialog, "notifications-wide"));
        dialog.resize(500, 950);
        QTRY_VERIFY(inView(editor, host));
        QVERIFY(snapshot(dialog, "notifications-narrow"));
        dialog.resize(1240, 950);
        QVERIFY(QMetaObject::invokeMethod(root, "selectSection", Q_ARG(QVariant, 1)));
        QTRY_COMPARE(module->depth(), 3);
        editor = module->subPage(1);
        QVERIFY(!editor->findChild<QObject*>("DefaultNotificationLayout"));
        auto* keyboard = editor->findChild<QQuickItem*>("KeyboardView");
        QVERIFY(keyboard);
        keyboard->setProperty("selection", QVariantList { 14, 15, 16, 17 });
        auto* assign = editor->findChild<QObject*>("AddAssignedEffect:" + id);
        QVERIFY(assign);
        QVERIFY(QMetaObject::invokeMethod(assign, "triggered"));
        QQuickItem* notificationRow = nullptr;
        QTRY_VERIFY((notificationRow = itemNamed(editor, "AssignedEffect:" + id)));
        QVERIFY(QMetaObject::invokeMethod(
            notificationRow->findChild<QObject*>("ExpandAssignedEffect"), "clicked"));
        auto* overrideLayers = notificationRow->findChild<QObject*>("AssignedEffectAboveLayers");
        QVERIFY(overrideLayers);
        overrideLayers->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(overrideLayers, "toggled"));
        QVariantList preview;
        QVERIFY(QMetaObject::invokeMethod(
            settings, "keyPreview", Q_RETURN_ARG(QVariantList, preview), Q_ARG(int, -1)));
        for (int key : { 14, 15, 16, 17 }) {
            QCOMPARE(preview[key].toMap()["notificationId"].toString(), id);
            QVERIFY(preview[key].toMap()["notificationAbove"].toBool());
            QCOMPARE(preview[key].toMap()["opacity"].toInt(), 0);
        }
        keyboard->setProperty("selection", QVariantList { 65536 });
        // Wait for the view to remove rows belonging to the previous key selection.
        QTRY_VERIFY(!itemNamed(editor, "AssignedEffect:" + id)
            && !itemNamed(editor, "AssignedEffect:" + id + ":above"));
        auto* addNotify = editor->findChild<QObject*>("AddAssignedEffect:" + id);
        QVERIFY(addNotify);
        QVERIFY(QMetaObject::invokeMethod(addNotify, "triggered"));
        QQuickItem* row = nullptr;
        QTRY_VERIFY((row = itemNamed(editor, "AssignedEffect:" + id)));
        QVERIFY(QMetaObject::invokeMethod(row->findChild<QObject*>("ExpandAssignedEffect"), "clicked"));
        auto* above = row->findChild<QObject*>("AssignedEffectAboveLayers");
        QVERIFY(above);
        above->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(above, "toggled"));
        QVERIFY(QMetaObject::invokeMethod(
            settings, "keyPreview", Q_RETURN_ARG(QVariantList, preview), Q_ARG(int, -1)));
        QCOMPARE(preview.last().toMap()["notificationId"].toString(), id);
        QVERIFY(preview.last().toMap()["notificationAbove"].toBool());
        // Add a second Notifications preset through the same menu without losing Ambient or critical.
        QVERIFY(QMetaObject::invokeMethod(
            settings, "addEffect", Q_ARG(QString, "notifications"), Q_ARG(bool, false)));
        const auto normalId = settings->property("effectPreset").toMap()["id"].toString();
        auto* addNormal = editor->findChild<QObject*>("AddAssignedEffect:" + normalId);
        QVERIFY(addNormal);
        QVERIFY(QMetaObject::invokeMethod(addNormal, "triggered"));
        QVariantMap stack;
        QVERIFY(QMetaObject::invokeMethod(settings, "selectionEffects", Q_RETURN_ARG(QVariantMap, stack),
            Q_ARG(int, -1), Q_ARG(QVariantList, QVariantList { 65536 })));
        QCOMPARE(stack["items"].toList().size(), 3);
        QTRY_VERIFY(itemNamed(editor, "AssignedEffect:ambient-default"));
        QTRY_VERIFY(itemNamed(editor, "AssignedEffect:" + id + ":above"));
        QTRY_VERIFY((row = itemNamed(editor, "AssignedEffect:" + normalId)));
        QVERIFY(QMetaObject::invokeMethod(row->findChild<QObject*>("ExpandAssignedEffect"), "clicked"));
        auto* intensity = row->findChild<QObject*>("AssignedEffectOpacity");
        QVERIFY(intensity);
        intensity->setProperty("value", 60);
        QVERIFY(QMetaObject::invokeMethod(intensity, "valueModified"));
        QVERIFY(QMetaObject::invokeMethod(settings, "selectionEffects", Q_RETURN_ARG(QVariantMap, stack),
            Q_ARG(int, -1), Q_ARG(QVariantList, QVariantList { 65536 })));
        QCOMPARE(stack["items"].toList()[1].toMap()["opacity"].toInt(), 60);
        // Collapse the edited settings before reordering the compact effect rows.
        QVERIFY(QMetaObject::invokeMethod(row->findChild<QObject*>("ExpandAssignedEffect"), "clicked"));
        // Scroll the list into view for responsive screenshots.
        auto* keyboardPage = editor->findChild<QObject*>("KeyboardPage");
        QVERIFY(keyboardPage);
        auto* flick = keyboardPage->property("flickable").value<QObject*>();
        QVERIFY(flick);
        flick->setProperty(
            "contentY", flick->property("contentHeight").toReal() - flick->property("height").toReal());
        QTest::qWait(100);
        const auto selectedStack = [&] {
            QVariantMap result;
            const bool ok
                = QMetaObject::invokeMethod(settings, "selectionEffects", Q_RETURN_ARG(QVariantMap, result),
                    Q_ARG(int, -1), Q_ARG(QVariantList, QVariantList { 65536 }));
            return ok ? result["items"].toList() : QVariantList();
        };
        const auto drag = [&](const QString& source, const QString& target, bool cancel = false) {
            auto* sourceRow = itemNamed(editor, "AssignedEffect:" + source);
            auto* targetRow = itemNamed(editor, "AssignedEffect:" + target);
            if (!sourceRow || !targetRow)
                return false;
            auto* handle = itemNamed(sourceRow, "AssignedEffectDragHandle");
            auto* targetHandle = itemNamed(targetRow, "AssignedEffectDragHandle");
            if (!handle || !targetHandle || !inView(handle, host) || !inView(targetHandle, host))
                return false;
            const auto from = handle->mapToScene(handle->boundingRect().center()).toPoint();
            const auto to = targetHandle->mapToScene(targetHandle->boundingRect().center()).toPoint();
            QTest::mousePress(host, Qt::LeftButton, Qt::NoModifier, from);
            // Cross the drag threshold, then move through the intervening rows.
            for (int step = 1; step <= 8; ++step)
                QTest::mouseMove(host, from + (to - from) * step / 8, 20);
            QTest::qWait(200);
            if (cancel)
                QTest::keyClick(host, Qt::Key_Escape);
            QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, to);
            return true;
        };
        QVERIFY(drag(normalId, "ambient-default"));
        QTRY_COMPARE(selectedStack().first().toMap()["id"].toString(), normalId);
        QCOMPARE(selectedStack().last().toMap()["id"].toString(), id);
        QVERIFY(selectedStack().last().toMap()["above"].toBool());
        // Dropping on the other priority group cannot promote a normal notification.
        QVERIFY(drag(normalId, id + ":above"));
        QTRY_COMPARE(selectedStack()[1].toMap()["id"].toString(), normalId);
        QVERIFY(!selectedStack()[1].toMap()["above"].toBool());
        QVERIFY(selectedStack().last().toMap()["above"].toBool());
        const auto beforeCancel = selectedStack();
        QVERIFY(drag(normalId, "ambient-default", true));
        QTest::qWait(200);
        QCOMPARE(selectedStack(), beforeCancel);
        keyboard->setProperty("selection", QVariantList { 14, 65536 });
        QTRY_VERIFY(!itemNamed(editor, "AssignedEffectDragHandle")->isEnabled());
        keyboard->setProperty("selection", QVariantList { 65536 });
        QVERIFY(snapshot(dialog, "notifications-layout"));
        dialog.resize(500, 950);
        QTRY_VERIFY(inView(editor, host));
        flick->setProperty(
            "contentY", flick->property("contentHeight").toReal() - flick->property("height").toReal());
        QTest::qWait(100);
        QVERIFY(snapshot(dialog, "effects-list-narrow"));
        QTRY_VERIFY((row = itemNamed(editor, "AssignedEffect:" + normalId)));
        QVERIFY(QMetaObject::invokeMethod(row->findChild<QObject*>("RemoveAssignedEffect"), "clicked"));
        const auto afterRemoval = selectedStack();
        QVERIFY(std::none_of(afterRemoval.cbegin(), afterRemoval.cend(),
            [&](const auto& item) { return item.toMap()["id"].toString() == normalId; }));
        QTRY_VERIFY(!itemNamed(editor, "AssignedEffect:" + normalId));
        QTRY_VERIFY(itemNamed(editor, "AssignedEffect:" + id + ":above"));
        module->load();
        QVERIFY(!module->needsSave());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void localEditorsAndDefaultLayout()
    {
        KCMultiDialog dialog;
        QVERIFY(dialog.addModule(KPluginMetaData(CONFIG_PLUGIN_PATH)));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1240, 950);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host && host->rootObject());
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 3);
        auto* root = module->mainUi();
        auto* settings = module->property("controller").value<QObject*>();
        QStringList errors;
        connect(module->engine().get(), &QQmlEngine::warnings, this, [&](const QList<QQmlError>& warnings) {
            for (const auto& e : warnings)
                errors << e.toString();
        });
        QVERIFY(QMetaObject::invokeMethod(root, "selectSection", Q_ARG(QVariant, 1)));
        QTRY_COMPARE(module->depth(), 3);
        auto* editor = module->subPage(1);
        auto* list = editor->findChild<QObject*>("AssignedEffectsEditor");
        auto* keyboard = editor->findChild<QObject*>("KeyboardView");
        QVERIFY(list && keyboard);
        QVERIFY(list->property("assignmentState").toMap()["defaults"].toBool());
        QVERIFY(!editor->findChild<QObject*>("DefaultEffect"));
        auto* addAmbient = editor->findChild<QObject*>("AddLocalEffect:ambient");
        QVERIFY(addAmbient);
        QVERIFY(QMetaObject::invokeMethod(addAmbient, "triggered"));
        const auto ambient = list->property("expandedEffect").toString();
        QQuickItem* row = nullptr;
        QTRY_VERIFY((row = itemNamed(editor, "AssignedEffect:" + ambient)));
        QObject* fps = nullptr;
        QTRY_VERIFY((fps = row->findChild<QObject*>("FramesPerSecond")));
        fps->setProperty("value", 4);
        QVERIFY(QMetaObject::invokeMethod(fps, "valueModified"));
        QVariantMap options;
        QVERIFY(QMetaObject::invokeMethod(
            settings, "localEffectOptions", Q_RETURN_ARG(QVariantMap, options), Q_ARG(QString, ambient)));
        QCOMPARE(options["fps"].toInt(), 4);
        QCOMPARE(settings->property("effectPresets").toList().size(), 1);
        QVariantList all;
        for (const auto& key : settings->property("keys").toList())
            all.append(key.toMap()["id"]);
        const auto defaults = list->property("assignmentState").toMap();
        keyboard->setProperty("selection", all);
        QCOMPARE(list->property("assignmentState").toMap(), defaults);
        keyboard->setProperty("selection", QVariantList { 14 });
        QVERIFY(!list->property("assignmentState").toMap()["defaults"].toBool());
        auto* addNotify = editor->findChild<QObject*>("AddLocalEffect:notifications");
        QVERIFY(addNotify);
        QVERIFY(QMetaObject::invokeMethod(addNotify, "triggered"));
        const auto notify = list->property("expandedEffect").toString();
        QTRY_VERIFY((row = itemNamed(editor, "AssignedEffect:" + notify)));
        QObject* urgency = nullptr;
        QTRY_VERIFY((urgency = row->findChild<QObject*>("NotificationUrgency")));
        urgency->setProperty("currentIndex", 3);
        QVERIFY(QMetaObject::invokeMethod(urgency, "activated", Q_ARG(int, 3)));
        QVERIFY(QMetaObject::invokeMethod(
            settings, "localEffectOptions", Q_RETURN_ARG(QVariantMap, options), Q_ARG(QString, notify)));
        QCOMPARE(options["urgency"].toInt(), 2);
        QCOMPARE(settings->property("effectPresets").toList().size(), 1);
        QVERIFY(snapshot(dialog, "local-notification-editor"));
        keyboard->setProperty("selection", QVariantList { });
        QCOMPARE(list->property("assignmentState").toMap(), defaults);
        QVERIFY(snapshot(dialog, "default-effects"));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void stripRangeAndGradientEditor()
    {
        QStringList errors;
        KCMultiDialog dialog;
        QVERIFY(dialog.addModule(KPluginMetaData(CONFIG_PLUGIN_PATH)));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(1440, 1100);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host);
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        QTRY_COMPARE(module->depth(), 3);
        connect(module->engine().get(), &QQmlEngine::warnings, this, [&](const QList<QQmlError>& warnings) {
            for (const auto& e : warnings)
                errors << e.toString();
        });
        auto* settings = module->property("controller").value<QObject*>();
        QVERIFY(settings);
        QCOMPARE(settings->property("devices").toList().size(), 1);
        QTRY_VERIFY(beside(module->mainUi(), module->subPage(0), host)
            && beside(module->subPage(0), module->subPage(1), host));
        QVERIFY(snapshot(dialog, "devices-wide"));
        QVERIFY(QMetaObject::invokeMethod(module->mainUi(), "selectSection", Q_ARG(QVariant, 1)));
        QTRY_COMPARE(module->depth(), 3);
        auto* editor = module->subPage(1);
        auto* keyboard = editor->findChild<QQuickItem*>("KeyboardView");
        QVERIFY(keyboard);
        QTRY_VERIFY(itemNamed(editor, "StripElement11"));
        const auto selectEveryElement = [&] {
            for (int i = 0; i < 12; ++i) {
                auto* element = itemNamed(editor, "StripElement" + QString::number(i));
                QVERIFY(element && inView(element, host));
                QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
                    element->mapToScene(element->boundingRect().center()).toPoint());
                const QList<int> ids { 89, 91, 93, 95, 97, 99, 98, 96, 94, 92, 90, 88 };
                QCOMPARE(keyboard->property("selection").toList(), QVariantList { ids[i] });
            }
        };
        selectEveryElement();
        auto* first = itemNamed(editor, "StripElement0");
        auto* last = itemNamed(editor, "StripElement11");
        QVERIFY(first && last);
        QTRY_VERIFY(inView(first, host) && inView(last, host));
        QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
            first->mapToScene(first->boundingRect().center()).toPoint());
        QCOMPARE(keyboard->property("selection").toList(), QVariantList { 89 });
        QTest::mouseClick(host, Qt::LeftButton, Qt::ShiftModifier,
            last->mapToScene(last->boundingRect().center()).toPoint());
        QCOMPARE(keyboard->property("selection").toList().size(), 12);
        auto* middle = itemNamed(editor, "StripElement5");
        QVERIFY(middle);
        QTest::mouseClick(host, Qt::LeftButton, Qt::ShiftModifier,
            middle->mapToScene(middle->boundingRect().center()).toPoint());
        QCOMPARE(keyboard->property("selection").toList(), (QVariantList { 89, 91, 93, 95, 97, 99 }));
        QVERIFY(!editor->findChild<QObject*>("AddGradient"));
        // Existing presets can still contain gradients; keep their editing coverage.
        QVERIFY(QMetaObject::invokeMethod(settings, "setGradient", Q_ARG(int, -1), Q_ARG(int, -1),
            Q_ARG(int, 65536), Q_ARG(int, 89), Q_ARG(int, 88), Q_ARG(QString, "#00ff00"),
            Q_ARG(QString, "#ffffff"), Q_ARG(int, 100), Q_ARG(bool, false), Q_ARG(QString, QString())));
        QQuickItem* edit = nullptr;
        QTRY_VERIFY((edit = itemNamed(editor, "EditGradient0")));
        QVERIFY(QMetaObject::invokeMethod(edit, "clicked"));
        auto* from = itemNamed(editor, "GradientFrom");
        auto* to = itemNamed(editor, "GradientTo");
        auto* start = itemNamed(editor, "GradientStart");
        auto* end = itemNamed(editor, "GradientEnd");
        auto* save = itemNamed(editor, "SaveGradient");
        QVERIFY(from && to && start && end && save);
        QCOMPARE(from->property("currentIndex").toInt(), 0);
        QCOMPARE(to->property("currentIndex").toInt(), 11);
        from->setProperty("currentIndex", 2);
        to->setProperty("currentIndex", 5);
        start->setProperty("text", "#ff0000");
        end->setProperty("text", "#0000ff");
        QVERIFY(snapshot(dialog, "gradient-wide"));
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QVariantList gradients;
        QVERIFY(QMetaObject::invokeMethod(
            settings, "gradients", Q_RETURN_ARG(QVariantList, gradients), Q_ARG(int, -1)));
        QCOMPARE(gradients.size(), 1);
        QCOMPARE(gradients.first().toMap()["from"].toInt(), 93);
        QCOMPARE(gradients.first().toMap()["to"].toInt(), 99);
        auto* stripEditor = itemNamed(editor, "StripView65536");
        QVERIFY(stripEditor);
        QCOMPARE(stripEditor->property("gradientList").toList().size(), 1);
        auto* layerChoice = itemNamed(editor, "LayerChoice0");
        auto* baseChoice = itemNamed(editor, "LayerChoiceBase");
        QVERIFY(layerChoice && baseChoice);
        QVERIFY(QMetaObject::invokeMethod(layerChoice, "clicked"));
        QTRY_VERIFY(stripEditor->property("gradientList").toList().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(baseChoice, "clicked"));
        QTRY_COMPARE(stripEditor->property("gradientList").toList().size(), 1);
        QVariantList preview;
        QVERIFY(QMetaObject::invokeMethod(
            settings, "keyPreview", Q_RETURN_ARG(QVariantList, preview), Q_ARG(int, -1)));
        auto color = [&](int id) {
            for (const auto& p : preview)
                if (p.toMap()["id"].toInt() == id)
                    return p.toMap()["color"].toString();
            return QString();
        };
        QCOMPARE(color(93), QString("#ff0000"));
        QCOMPARE(color(95), QString("#aa0055"));
        QCOMPARE(color(97), QString("#5500aa"));
        QCOMPARE(color(99), QString("#0000ff"));
        dialog.resize(500, 1100);
        QTRY_VERIFY(inView(editor, host));
        selectEveryElement();
        QVERIFY(snapshot(dialog, "strip-narrow"));
        module->load();
        QVERIFY(!module->needsSave());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void effectsLightbarLayersAndResponsiveNavigation()
    {
        QStringList errors;
        KCMultiDialog dialog;
        QVERIFY(dialog.addModule(KPluginMetaData(CONFIG_PLUGIN_PATH)));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::qWait(200);
        dialog.resize(1240, 950);
        QTest::qWait(200);
        auto* host = dialog.findChild<QQuickWidget*>();
        QVERIFY(host && host->rootObject());
        auto* module
            = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
        QVERIFY(module);
        auto* root = module->mainUi();
        QVERIFY(root);
        QTRY_COMPARE(module->depth(), 3);
        connect(module->engine().get(), &QQmlEngine::warnings, this, [&](const QList<QQmlError>& warnings) {
            for (const auto& e : warnings)
                errors << e.toString();
        });
        auto* settings = module->property("controller").value<QObject*>();
        auto* recorder = module->property("recorder").value<QObject*>();
        QVERIFY(settings && recorder);
        auto section = [&](int value) {
            return QMetaObject::invokeMethod(root, "selectSection", Q_ARG(QVariant, value));
        };
        QVERIFY(module->subPage(1)->findChild<QObject*>("EnableLightbar"));
        auto* pauseBattery = module->subPage(1)->findChild<QObject*>("PauseOnBattery");
        auto* pauseLow = module->subPage(1)->findChild<QObject*>("PauseOnLowBattery");
        QVERIFY(pauseBattery && pauseLow);
        QVERIFY(!pauseBattery->property("checked").toBool());
        QVERIFY(!pauseLow->property("checked").toBool());
        pauseLow->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(pauseLow, "toggled"));
        QVERIFY(settings->property("deviceSettings").toMap()["pauseOnLowBattery"].toBool());
        pauseBattery->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(pauseBattery, "toggled"));
        QVERIFY(settings->property("deviceSettings").toMap()["pauseOnBattery"].toBool());
        QVERIFY(!pauseLow->property("enabled").toBool());
        QVERIFY(!module->representsDefaults());
        module->defaults();
        QVERIFY(!pauseBattery->property("checked").toBool());
        QVERIFY(!pauseLow->property("checked").toBool());
        QVERIFY(module->representsDefaults());
        module->load();
        QVERIFY(!pauseBattery->property("checked").toBool());
        QVERIFY(!pauseLow->property("checked").toBool());
        QVERIFY(pauseLow->property("enabled").toBool());
        QVERIFY(section(2));
        QTRY_COMPARE(module->depth(), 3);
        auto* list = module->subPage(0);
        auto* editor = module->subPage(1);
        QTRY_VERIFY(beside(root, list, host) && beside(list, editor, host));
        auto* add = list->findChild<QObject*>("AddAmbientEffect");
        QVERIFY(add);
        QVERIFY(QMetaObject::invokeMethod(add, "triggered"));
        QCOMPARE(settings->property("effectPresets").toList().size(), 2);
        auto* effectName = editor->findChild<QObject*>("EffectName");
        QVERIFY(effectName);
        effectName->setProperty("text", "Warm and slow");
        QVERIFY(QMetaObject::invokeMethod(effectName, "textEdited"));
        QCOMPARE(settings->property("effectPreset").toMap()["name"].toString(), QString("Warm and slow"));
        auto* fps = editor->findChild<QObject*>("FramesPerSecond");
        QVERIFY(fps);
        fps->setProperty("value", 7);
        QVERIFY(QMetaObject::invokeMethod(fps, "valueModified"));
        const auto effectId = settings->property("effectPreset").toMap()["id"].toString();
        QVERIFY(snapshot(dialog, "effects-wide"));
        QVERIFY(section(1));
        QTRY_COMPARE(module->depth(), 3);
        list = module->subPage(0);
        editor = module->subPage(1);
        auto* keyboard = editor->findChild<QQuickItem*>("KeyboardView");
        QVERIFY(!editor->findChild<QObject*>("LightbarView"));
        auto* groupSelector = itemNamed(editor, "KeyGroupSelector");
        auto* effectChoice = editor->findChild<QObject*>("AddAssignedEffect:" + effectId);
        auto* layers = itemNamed(editor, "LayerChoice0");
        auto* addColor = editor->findChild<QObject*>("AddLocalEffect:solid");
        QVERIFY(keyboard && groupSelector && effectChoice && addColor && layers);
        QVariantList keyGroups;
        QVERIFY(QMetaObject::invokeMethod(settings, "groups", Q_RETURN_ARG(QVariantList, keyGroups)));
        const auto stripGroup = std::find_if(keyGroups.cbegin(), keyGroups.cend(),
            [](const auto& group) { return group.toMap()["ids"].toList() == QVariantList { 65536 }; });
        QVERIFY(stripGroup != keyGroups.cend());
        QCOMPARE(stripGroup->toMap()["name"].toString(),
            settings->property("strips").toList().first().toMap()["name"].toString());
        QVERIFY(!stripGroup->toMap()["custom"].toBool());
        const int stripIndex = int(std::distance(keyGroups.cbegin(), stripGroup));
        keyboard->setProperty("selection", QVariantList { 17 });
        groupSelector->setProperty("currentIndex", stripIndex);
        QVERIFY(QMetaObject::invokeMethod(groupSelector, "activated", Q_ARG(int, stripIndex)));
        QCOMPARE(keyboard->property("selection").toList(), QVariantList { 65536 });
        // A whole-strip group can still be combined with individual keys.
        QVERIFY(QMetaObject::invokeMethod(keyboard, "toggle", Q_ARG(QVariant, 17), Q_ARG(QVariant, true)));
        QCOMPARE(keyboard->property("selection").toList(), (QVariantList { 65536, 17 }));
        QVERIFY(QMetaObject::invokeMethod(effectChoice, "triggered"));
        QVERIFY(QMetaObject::invokeMethod(addColor, "triggered"));
        auto* assigned = editor->findChild<QObject*>("AssignedEffectsEditor");
        QVERIFY(assigned);
        const auto solidId = assigned->property("expandedEffect").toString();
        QQuickItem* colorRow = nullptr;
        QTRY_VERIFY((colorRow = itemNamed(editor, "AssignedEffect:" + solidId)));
        auto* color = colorRow->findChild<QObject*>("HexColorInput");
        auto* opacity = colorRow->findChild<QObject*>("AssignedEffectOpacity");
        auto* chooseColor = colorRow->findChild<QQuickItem*>("ColorSwatch");
        auto* colorDialog = colorRow->findChild<QObject*>("ColorPicker");
        QVERIFY(color && opacity && chooseColor && colorDialog);
        QVERIFY(chooseColor->width() > chooseColor->height());
        QVERIFY(QMetaObject::invokeMethod(chooseColor, "clicked"));
        QTRY_VERIFY(colorDialog->property("visible").toBool());
        colorDialog->setProperty("currentColor", QColor("#0000ff"));
        QVERIFY(QMetaObject::invokeMethod(colorDialog, "reject"));
        QCOMPARE(color->property("text").toString(), QString("#ffffff"));
        QVERIFY(QMetaObject::invokeMethod(chooseColor, "clicked"));
        QTRY_VERIFY(colorDialog->property("visible").toBool());
        colorDialog->setProperty("color", QColor("#ff0000"));
        QVERIFY(QMetaObject::invokeMethod(colorDialog, "accept"));
        QTRY_VERIFY(!colorDialog->property("visible").toBool());
        QCOMPARE(color->property("text").toString(), QString("#ff0000"));
        opacity->setProperty("value", 50);
        QVERIFY(QMetaObject::invokeMethod(opacity, "valueModified"));
        QVariantList preview;
        QVERIFY(QMetaObject::invokeMethod(
            settings, "keyPreview", Q_RETURN_ARG(QVariantList, preview), Q_ARG(int, -1)));
        QVariantMap assignments;
        QVERIFY(
            QMetaObject::invokeMethod(settings, "selectionEffects", Q_RETURN_ARG(QVariantMap, assignments),
                Q_ARG(int, -1), Q_ARG(QVariantList, (QVariantList { 17, 65536 }))));
        QCOMPARE(assignments["items"].toList().size(), 3);
        QCOMPARE(assignments["items"].toList().last().toMap()["id"].toString(), solidId);
        QVERIFY(!assignments["mixed"].toBool());
        QCOMPARE(preview[17].toMap()["opacity"].toInt(), 50);
        QCOMPARE(preview.last().toMap()["id"].toInt(), 65536);
        QCOMPARE(preview.last().toMap()["color"].toString(), QString("#ff0000"));
        QVERIFY(module->needsSave());
        QVERIFY(snapshot(dialog, "keyboard-wide"));
        dialog.resize(2050, 950);
        QTest::qWait(200);
        QVERIFY(keyboard->width() <= 1200);
        QVERIFY(keyboard->width() >= 1199);
        const auto formWidth
            = editor->findChild<QObject*>("KeyboardPage")->property("formMaximumWidth").toReal();
        QVERIFY(itemNamed(editor, "StripView65536")->width() <= formWidth + 1);
        QVERIFY(assigned->property("width").toReal() <= formWidth + 1);
        QVERIFY(snapshot(dialog, "keyboard-max-width"));
        dialog.resize(1240, 950);
        QTest::qWait(150);
        QVERIFY(QMetaObject::invokeMethod(layers, "clicked"));
        auto* record = itemNamed(editor, "RecordLayerKeys0");
        QVERIFY(record);
        QVERIFY(QMetaObject::invokeMethod(record, "clicked"));
        recordChord(
            dialog.windowHandle(), { { KEY_LEFTCTRL, Qt::Key_Control }, { KEY_RIGHTSHIFT, Qt::Key_Shift } });
        QCOMPARE(module->depth(), 3);
        keyboard->setProperty("selection", QVariantList { 65536 });
        // Add a separate local color to the modifier layer; editing it must not change the base.
        QVERIFY(QMetaObject::invokeMethod(addColor, "triggered"));
        const auto layerSolid = assigned->property("expandedEffect").toString();
        QTRY_VERIFY((colorRow = itemNamed(editor, "AssignedEffect:" + layerSolid)));
        color = colorRow->findChild<QObject*>("HexColorInput");
        QVERIFY(color);
        color->setProperty("text", "#00ff00");
        QVERIFY(QMetaObject::invokeMethod(color, "textEdited"));
        QVERIFY(QMetaObject::invokeMethod(
            settings, "keyPreview", Q_RETURN_ARG(QVariantList, preview), Q_ARG(int, 0)));
        QCOMPARE(preview.last().toMap()["color"].toString(), QString("#00ff00"));
        QVERIFY(snapshot(dialog, "layer-wide"));
        auto* addLayer = editor->findChild<QObject*>("AddLayer");
        QVERIFY(addLayer);
        QVERIFY(QMetaObject::invokeMethod(addLayer, "clicked"));
        recordChord(dialog.windowHandle(), { { KEY_LEFTALT, Qt::Key_Alt } });
        QTRY_COMPARE(settings->property("preset").toMap()["layers"].toList().size(), 2);
        QQuickItem* removeLayer = nullptr;
        QTRY_VERIFY((removeLayer = itemNamed(editor, "RemoveLayer1")));
        QVERIFY(QMetaObject::invokeMethod(removeLayer, "clicked"));
        QCOMPARE(settings->property("preset").toMap()["layers"].toList().size(), 1);
        QVERIFY(QMetaObject::invokeMethod(layers, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(recorder, "start", Q_ARG(QWindow*, host->quickWindow())));
        QVERIFY(recorder->property("recording").toBool());
        QVERIFY(section(2));
        QTRY_COMPARE(module->depth(), 3);
        QVERIFY(!recorder->property("recording").toBool());
        QCOMPARE(settings->property("effectPreset").toMap()["fps"].toInt(), 7);
        QVERIFY(section(3));
        QTRY_COMPARE(module->depth(), 3);
        list = module->subPage(0);
        editor = module->subPage(1);
        auto* addApplication = list->findChild<QObject*>("AddApplication");
        QVERIFY(addApplication);
        QVERIFY(QMetaObject::invokeMethod(addApplication, "clicked"));
        auto* windowClass = editor->findChild<QObject*>("WindowClass");
        QVERIFY(windowClass);
        windowClass->setProperty("text", "org.kde.konsole");
        QVERIFY(QMetaObject::invokeMethod(windowClass, "textEdited"));
        QCOMPARE(settings->property("rules").toList().first().toMap()["windowClass"].toString(),
            QString("org.kde.konsole"));
        QVERIFY(section(1));
        QTRY_COMPARE(module->depth(), 3);
        list = module->subPage(0);
        editor = module->subPage(1);
        dialog.resize(900, 950);
        QTest::qWait(200);
        QTRY_VERIFY(!beside(root, list, host));
        QTRY_VERIFY(beside(root, editor, host));
        QVERIFY(snapshot(dialog, "keyboard-medium"));
        auto* back = buttonWithText(host->rootObject(), "Navigate Back");
        QVERIFY(back && back->isEnabled());
        QTest::mouseClick(
            host, Qt::LeftButton, Qt::NoModifier, back->mapToScene(back->boundingRect().center()).toPoint());
        QTRY_VERIFY(beside(root, list, host));
        QVERIFY(QMetaObject::invokeMethod(list, "showEditor"));
        QTRY_VERIFY(!beside(root, list, host));
        dialog.resize(500, 950);
        QTest::qWait(200);
        QTRY_VERIFY(inView(editor, host));
        QVERIFY(snapshot(dialog, "keyboard-narrow"));
        module->setCurrentIndex(0);
        QTRY_VERIFY(inView(root, host));
        QVERIFY(snapshot(dialog, "sections-narrow"));
        module->setCurrentIndex(2);
        dialog.resize(1240, 950);
        QTRY_VERIFY(beside(root, list, host) && beside(list, editor, host));
        module->load();
        QCOMPARE(module->depth(), 3);
        QVERIFY(!module->needsSave());
        QCOMPARE(settings->property("effectPresets").toList().size(), 1);
        QVERIFY(section(2));
        QTRY_COMPARE(module->depth(), 3);
        QVERIFY(QMetaObject::invokeMethod(settings, "setDefaultEffect", Q_ARG(QString, QString())));
        bool removed = false;
        QVERIFY(QMetaObject::invokeMethod(settings, "removeEffect", Q_RETURN_ARG(bool, removed)));
        QVERIFY(removed);
        QCOMPARE(settings->property("selectedEffect").toInt(), -1);
        QTest::qWait(50);
        QVERIFY(snapshot(dialog, "effects-empty"));
        module->load();
        QCOMPARE(settings->property("effectPresets").toList().size(), 1);
        QVERIFY(section(0));
        QTRY_COMPARE(module->depth(), 3);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir dir;
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", dir.path().toUtf8());
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QApplication app(argc, argv);
    QuickTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_quick.moc"

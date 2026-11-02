// SPDX-License-Identifier: GPL-3.0-or-later
#include "condition_recorder.h"
#include "layer_key_tracker.h"
#include <KKeySequenceRecorder>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>
#include <linux/input-event-codes.h>
using namespace Ambient;

class KeySink : public QQuickWindow
{
public:
    int presses = 0, releases = 0;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        ++presses;
        event->ignore();
    }
    void keyReleaseEvent(QKeyEvent* event) override
    {
        ++releases;
        event->ignore();
    }
};

class LayerConditionsTest : public QObject
{
    Q_OBJECT
    static void key(QQuickWindow& dialog, QEvent::Type type, quint32 code, int qtKey, bool repeat = false)
    {
        QKeyEvent event(type, qtKey, Qt::NoModifier, code + 8, 0, 0, QString(), repeat);
        QApplication::sendEvent(&dialog, &event);
    }
    static void record(ConditionRecorder* editor)
    {
        if (editor->recording())
            editor->cancel();
        else
            editor->start(qobject_cast<QWindow*>(editor->parent()));
    }
private Q_SLOTS:
    void passivePreviewDoesNotConsumeKeys()
    {
        KeySink window;
        window.resize(300, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.isActive());
        LayerKeyTracker tracker;
        tracker.watch(&window);
        KeyboardLayer shift;
        KeyboardLayer chord;
        chord.conditions = { "ctrl-left", "shift-right" };
        key(window, QEvent::KeyPress, KEY_RIGHTSHIFT, Qt::Key_Shift);
        QVERIFY(layerActive(shift, tracker.state()));
        key(window, QEvent::KeyPress, KEY_LEFTCTRL, Qt::Key_Control);
        QVERIFY(!layerActive(shift, tracker.state()));
        QVERIFY(layerActive(chord, tracker.state()));
        key(window, QEvent::KeyPress, KEY_A, Qt::Key_A);
        QVERIFY(layerActive(chord, tracker.state()));
        QCOMPARE(window.presses, 3); // Input continues to the original receiver.
        key(window, QEvent::KeyRelease, KEY_A, Qt::Key_A);
        key(window, QEvent::KeyRelease, KEY_RIGHTSHIFT, Qt::Key_Shift, true);
        QVERIFY(layerActive(chord, tracker.state()));
        key(window, QEvent::KeyRelease, KEY_LEFTCTRL, Qt::Key_Control);
        QVERIFY(layerActive(shift, tracker.state()));
        key(window, QEvent::KeyRelease, KEY_RIGHTSHIFT, Qt::Key_Shift);
        QVERIFY(!layerActive(shift, tracker.state()));
        QCOMPARE(window.releases, 4);
        QVERIFY(tracker.state().pressed.isEmpty());
        tracker.watch(nullptr);
        key(window, QEvent::KeyPress, KEY_RIGHTSHIFT, Qt::Key_Shift);
        QVERIFY(!layerActive(shift, tracker.state()));
        QCOMPARE(window.presses, 4);
    }
    void passivePreviewResetsOnFocusLossAndWindowDestruction()
    {
        auto window = std::make_unique<KeySink>();
        window->resize(300, 200);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QTRY_VERIFY(window->isActive());
        LayerKeyTracker tracker;
        tracker.watch(window.get());
        key(*window, QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
        QVERIFY(conditionActive("shift-left", tracker.state()));
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(window.get(), &deactivate);
        QVERIFY(tracker.state() == ModifierState());
        key(*window, QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
        QVERIFY(tracker.state() == ModifierState());
        QEvent activate(QEvent::WindowActivate);
        QApplication::sendEvent(window.get(), &activate);
        key(*window, QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
        QVERIFY(conditionActive("shift-left", tracker.state()));
        window.reset();
        QVERIFY(tracker.state() == ModifierState());
    }
    void passivePreviewTracksTheWidgetHost()
    {
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* quick = new QQuickWidget(&dialog);
        layout->addWidget(quick);
        dialog.resize(300, 200);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTRY_VERIFY(dialog.windowHandle()->isActive());
        LayerKeyTracker tracker;
        tracker.watch(quick->quickWindow());
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier, KEY_RIGHTSHIFT + 8, 0, 0);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Shift, Qt::ShiftModifier, KEY_RIGHTSHIFT + 8, 0, 0);
        QApplication::sendEvent(dialog.windowHandle(), &press);
        QVERIFY(conditionActive("shift-right", tracker.state()));
        QApplication::sendEvent(dialog.windowHandle(), &release);
        QVERIFY(!conditionActive("shift", tracker.state()));
    }
    void passivePreviewCapsLockFallbackIgnoresDuplicateDelivery()
    {
        // The offscreen platform has no KModifierKeyInfo backend.
        KeySink window;
        window.resize(300, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.isActive());
        LayerKeyTracker tracker;
        tracker.watch(&window);
        for (bool locked : { true, false }) {
            key(window, QEvent::ShortcutOverride, KEY_CAPSLOCK, Qt::Key_CapsLock);
            key(window, QEvent::KeyPress, KEY_CAPSLOCK, Qt::Key_CapsLock);
            key(window, QEvent::KeyPress, KEY_CAPSLOCK, Qt::Key_CapsLock, true);
            key(window, QEvent::KeyRelease, KEY_CAPSLOCK, Qt::Key_CapsLock);
            QCOMPARE(tracker.state().capsLock, locked);
            QCOMPARE(layerActive(KeyboardLayer(), tracker.state()), locked);
        }
    }
    void widgetHostInhibitsTheVisibleWindow()
    {
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* quick = new QQuickWidget(&dialog);
        layout->addWidget(quick);
        dialog.resize(300, 200);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        ConditionRecorder editor;
        QSignalSpy accepted(&editor, &ConditionRecorder::accepted);
        editor.start(quick->quickWindow());
        QVERIFY(editor.recording());
        QCOMPARE(editor.findChild<KKeySequenceRecorder*>()->window(), dialog.windowHandle());
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier, KEY_RIGHTSHIFT + 8, 0, 0);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier, KEY_RIGHTSHIFT + 8, 0, 0);
        QApplication::sendEvent(dialog.windowHandle(), &press);
        QApplication::sendEvent(dialog.windowHandle(), &release);
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first().first().toStringList(), (QStringList { "shift-right" }));
    }
    void physicalChordAndModifierOnly()
    {
        QQuickWindow dialog;
        auto* editor = new ConditionRecorder(&dialog);
        QStringList combination { "shift" };
        connect(editor, &ConditionRecorder::accepted, &dialog,
            [&](const QStringList& value) { combination = value; });
        dialog.resize(300, 200);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        record(editor);
        QVERIFY(editor->recording());
        key(dialog, QEvent::KeyPress, KEY_LEFTCTRL, Qt::Key_Control);
        key(dialog, QEvent::KeyPress, KEY_RIGHTSHIFT, Qt::Key_Shift);
        key(dialog, QEvent::KeyPress, KEY_RIGHTSHIFT, Qt::Key_Shift, true);
        key(dialog, QEvent::KeyRelease, KEY_RIGHTSHIFT, Qt::Key_Shift, true);
        QVERIFY(editor->recording());
        key(dialog, QEvent::KeyRelease, KEY_LEFTCTRL, Qt::Key_Control);
        QVERIFY(editor->recording());
        key(dialog, QEvent::KeyRelease, KEY_RIGHTSHIFT, Qt::Key_Shift);
        QVERIFY(!editor->recording());
        QCOMPARE(combination, (QStringList { "ctrl-left", "shift-right" }));
        QVERIFY(!editor->findChild<KKeySequenceRecorder*>()->isRecording());
        record(editor);
        key(dialog, QEvent::KeyPress, KEY_RIGHTALT, Qt::Key_AltGr);
        key(dialog, QEvent::KeyRelease, KEY_RIGHTALT, Qt::Key_AltGr);
        QCOMPARE(combination, (QStringList { "alt-right" }));
    }
    void cancellationAndFocusLoss()
    {
        QQuickWindow dialog;
        auto* editor = new ConditionRecorder(&dialog);
        QStringList combination { "shift" };
        connect(editor, &ConditionRecorder::accepted, &dialog,
            [&](const QStringList& value) { combination = value; });
        dialog.resize(300, 200);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto previous = combination;
        for (int cancel = 0; cancel < 4; ++cancel) {
            record(editor);
            QVERIFY(editor->recording());
            key(dialog, QEvent::KeyPress, KEY_A, Qt::Key_A);
            if (cancel == 0) {
                key(dialog, QEvent::KeyPress, KEY_ESC, Qt::Key_Escape);
                key(dialog, QEvent::KeyRelease, KEY_ESC, Qt::Key_Escape);
                QVERIFY(dialog.isVisible()); // Esc cancels recording, not the dialog.
            } else if (cancel == 1) {
                record(editor); // The same button is now Cancel recording.
            } else if (cancel == 2) {
                QEvent deactivate(QEvent::WindowDeactivate);
                QApplication::sendEvent(&dialog, &deactivate);
            } else {
                dialog.close();
            }
            QVERIFY(!editor->recording());
            QVERIFY(!editor->findChild<KKeySequenceRecorder*>()->isRecording());
            QCOMPARE(combination, previous);
        }
    }
    void simultaneousKeysAndSequentialInput()
    {
        QQuickWindow dialog;
        auto* editor = new ConditionRecorder(&dialog);
        QStringList combination { "shift" };
        connect(editor, &ConditionRecorder::accepted, &dialog,
            [&](const QStringList& value) { combination = value; });
        dialog.resize(300, 200);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QShortcut shortcut(QKeySequence(Qt::Key_A), &dialog);
        QSignalSpy activated(&shortcut, &QShortcut::activated);
        record(editor);
        key(dialog, QEvent::KeyPress, KEY_A, Qt::Key_A);
        key(dialog, QEvent::KeyPress, KEY_B, Qt::Key_B);
        key(dialog, QEvent::KeyRelease, KEY_A, Qt::Key_A);
        key(dialog, QEvent::KeyRelease, KEY_B, Qt::Key_B);
        QCOMPARE(combination, (QStringList { "key:30", "key:48" }));
        QCOMPARE(activated.size(), 0);
        record(editor);
        key(dialog, QEvent::KeyPress, KEY_LEFTCTRL, Qt::Key_Control);
        key(dialog, QEvent::KeyPress, KEY_A, Qt::Key_A);
        key(dialog, QEvent::KeyRelease, KEY_A, Qt::Key_A);
        key(dialog, QEvent::KeyPress, KEY_B, Qt::Key_B);
        key(dialog, QEvent::KeyRelease, KEY_B, Qt::Key_B);
        key(dialog, QEvent::KeyRelease, KEY_LEFTCTRL, Qt::Key_Control);
        QCOMPARE(combination, (QStringList { "ctrl-left", "key:30" }));
    }
    void invalidRecordingKeepsPreviousCombination()
    {
        QQuickWindow dialog;
        auto* editor = new ConditionRecorder(&dialog);
        QStringList combination { "shift" };
        connect(editor, &ConditionRecorder::accepted, &dialog,
            [&](const QStringList& value) { combination = value; });
        dialog.resize(300, 200);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        record(editor);
        key(dialog, QEvent::KeyPress, KEY_VOLUMEUP, Qt::Key_VolumeUp);
        key(dialog, QEvent::KeyRelease, KEY_VOLUMEUP, Qt::Key_VolumeUp);
        QCOMPARE(combination, (QStringList { "shift" }));
        QVERIFY(!editor->recording());
        record(editor);
        for (quint32 code = KEY_1; code <= KEY_9; ++code)
            key(dialog, QEvent::KeyPress, code, Qt::Key_1);
        for (quint32 code = KEY_1; code <= KEY_9; ++code)
            key(dialog, QEvent::KeyRelease, code, Qt::Key_1);
        QCOMPARE(combination, (QStringList { "shift" }));
        QVERIFY(!editor->recording());
    }
};
int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QApplication app(argc, argv);
    LayerConditionsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_layer_conditions.moc"

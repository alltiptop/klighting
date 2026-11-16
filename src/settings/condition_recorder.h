// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <KKeySequenceRecorder>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QWindow>
namespace Ambient
{
class ConditionRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(QStringList captured READ captured NOTIFY changed)
    Q_PROPERTY(QString hint READ hint NOTIFY changed)
public:
    explicit ConditionRecorder(QObject* parent = nullptr);
    ~ConditionRecorder() override;
    bool recording() const { return m_recording; }
    QStringList captured() const { return m_captured; }
    QString hint() const;
    Q_INVOKABLE void start(QWindow* window);
    Q_INVOKABLE void cancel();
Q_SIGNALS:
    void changed();
    void accepted(const QStringList& conditions);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    KKeySequenceRecorder* m_recorder;
    QPointer<QWindow> m_window, m_inhibitionWindow;
    bool m_recording = false, m_releasing = false, m_invalid = false;
    QStringList m_captured;
    QSet<quint32> m_held;
    void stopRecording(bool accept);
};
}

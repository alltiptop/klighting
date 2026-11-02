// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QList>
#include <QObject>

namespace Ambient
{
struct LedFrame {
    QColor color;
    uint brightness = 0;
    bool keyboard = false, lightbar = false;
    QList<QColor> keyColors;
    QList<QColor> stripColors = { }; // Hardware lamp order 88..99.
    bool operator==(const LedFrame&) const = default;
};

// Called on the owning Qt thread. Implementations complete asynchronously.
class LedTransport : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void submit(const LedFrame& frame, quint64 operation) = 0;
    virtual void release(quint64 operation) = 0;
    virtual void shutdown() = 0;
Q_SIGNALS:
    void finished(quint64 operation, const QString& error);
};
}

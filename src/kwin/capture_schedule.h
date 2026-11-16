// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace Ambient
{
// Capture timing only. The caller owns the clock, render target and readback.
class CaptureSchedule
{
public:
    void reset(int intervalMs)
    {
        m_interval = std::max(1, intervalMs);
        m_nextCapture = 0;
        m_nextRepaint = 0;
        m_dirty = true; // Request one initial frame even when the desktop is idle.
    }
    bool captureOnPaint(double now, bool readbackPending)
    {
        // Observe actual painting, including compositor animations. A successful
        // capture consumes this change, including a paint requested by us.
        m_dirty = true;
        // Give natural painting one readback-poll interval to satisfy the sample
        // deadline before forcing an extra frame. New paints cannot extend it.
        m_nextRepaint = m_nextCapture > 0 ? m_nextCapture + ReadbackPollMs : 0;
        return !readbackPending && now >= m_nextCapture;
    }
    void captured(double now)
    {
        m_dirty = false;
        m_nextCapture = now + m_interval;
    }
    bool requestRepaint(double now, bool readbackPending)
    {
        if (!m_dirty || readbackPending || now < std::max(m_nextCapture, m_nextRepaint))
            return false;
        // A repaint may not produce a usable target (e.g. an output is off).
        // Bound retries to four per second instead of polling an overdue deadline.
        m_nextRepaint = now + RepaintRetryMs;
        return true;
    }
    int nextUpdateMs(double now, bool readbackPending) const
    {
        if (readbackPending)
            return ReadbackPollMs;
        if (!m_dirty)
            return -1;
        return int(std::ceil(std::max(0.0, std::max(m_nextCapture, m_nextRepaint) - now)));
    }

private:
    static constexpr int ReadbackPollMs = 16, RepaintRetryMs = 250;
    int m_interval = 1000;
    double m_nextCapture = 0, m_nextRepaint = 0;
    bool m_dirty = true;
};
}

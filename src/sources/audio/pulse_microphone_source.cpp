// SPDX-License-Identifier: GPL-3.0-or-later
#include "pulse_microphone_source.h"
#include <algorithm>
#include <atomic>
#include <pulse/pulseaudio.h>
#include <utility>

namespace Ambient
{
// Pulse callbacks update request state; snapshots are delivered on the Qt queue.
// Stopping the loop joins Pulse callbacks before this storage is released.
struct PulseMicrophoneSource::Connection {
    PulseMicrophoneSource* owner;
    quint64 generation;
    pa_threaded_mainloop* loop = nullptr;
    pa_context* context = nullptr;
    pa_operation* operation = nullptr;
    bool started = false;
    bool failed = false;
    bool pending = false;
    bool subscriptionReady = false;
    std::shared_ptr<std::atomic<quint64>> revision = std::make_shared<std::atomic<quint64>>(0);
    quint64 requestedRevision = 0;
    uint32_t selectedIndex = PA_INVALID_INDEX;
    MicrophoneState candidate;

    Connection(PulseMicrophoneSource* source, quint64 epoch)
        : owner(source)
        , generation(epoch)
    {
    }
    ~Connection()
    {
        // Never stop with the loop lock held: stop waits for the worker thread.
        if (started)
            pa_threaded_mainloop_stop(loop);
        if (operation) {
            pa_operation_cancel(operation);
            pa_operation_unref(operation);
        }
        if (context) {
            pa_context_set_state_callback(context, nullptr, nullptr);
            pa_context_set_subscribe_callback(context, nullptr, nullptr);
            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        if (loop)
            pa_threaded_mainloop_free(loop);
    }
    void post(MicrophoneState state, bool invalidation = false)
    {
        QMetaObject::invokeMethod(
            owner,
            [source = owner, epoch = generation, token = revision, expected = revision->load(),
                state = std::move(state), invalidation] {
                // Invalidation is ordered before later snapshots on this Qt queue.
                // Dropping it on a newer revision could leave an old known value lit.
                if (source->m_enabled && source->m_generation == epoch
                    && (invalidation || token->load() == expected))
                    source->publish(state);
            },
            Qt::QueuedConnection);
    }
    void error(const QString& reason)
    {
        if (failed)
            return;
        failed = true;
        ++*revision;
        QMetaObject::invokeMethod(
            owner,
            [source = owner, epoch = generation, reason] {
                if (source->m_enabled && source->m_generation == epoch)
                    source->fail(reason);
            },
            Qt::QueuedConnection);
    }
    void releaseOperation()
    {
        if (operation)
            pa_operation_unref(std::exchange(operation, nullptr));
    }
    MicrophoneState unknown(const QString& reason) const
    {
        MicrophoneState state;
        state.available = true;
        state.backend = QStringLiteral("pulseaudio");
        state.reason = reason;
        return state;
    }
    void refresh()
    {
        if (failed || pending || !subscriptionReady)
            return;
        pending = true;
        requestedRevision = revision->load();
        candidate = unknown(QStringLiteral("Default input is unavailable"));
        operation = pa_context_get_server_info(context, serverInfo, this);
        if (!operation)
            error(QStringLiteral("Cannot request the default input"));
    }
    void complete()
    {
        releaseOperation();
        pending = false;
        if (failed)
            return;
        if (requestedRevision != revision->load()) {
            refresh();
            return;
        }
        post(candidate);
    }
    static void serverInfo(pa_context* context, const pa_server_info* info, void* data)
    {
        auto& self = *static_cast<Connection*>(data);
        self.releaseOperation();
        if (!info) {
            self.error(QStringLiteral("Cannot read audio server information"));
            return;
        }
        self.selectedIndex = PA_INVALID_INDEX;
        if (!info->default_source_name || !*info->default_source_name) {
            self.complete();
            return;
        }
        self.candidate.sourceName = QString::fromUtf8(info->default_source_name).left(1024);
        self.operation
            = pa_context_get_source_info_by_name(context, info->default_source_name, sourceInfo, &self);
        if (!self.operation)
            self.error(QStringLiteral("Cannot request default input information"));
    }
    static void sourceInfo(pa_context* context, const pa_source_info* info, int eol, void* data)
    {
        auto& self = *static_cast<Connection*>(data);
        if (eol < 0) {
            if (pa_context_errno(context) == PA_ERR_NOENTITY) {
                // Keep the subscription alive when no input exists, so hotplug
                // recovers immediately instead of waiting for reconnect backoff.
                self.selectedIndex = PA_INVALID_INDEX;
                self.candidate = self.unknown(QStringLiteral("Default input is unavailable"));
                self.post(self.candidate, true);
                self.complete();
            } else {
                self.error(QStringLiteral("Cannot read default input information"));
            }
        } else if (eol > 0) {
            self.complete();
        } else if (info) {
            self.selectedIndex = info->index;
            self.candidate.description = QString::fromUtf8(info->description).left(1024);
            if (info->monitor_of_sink != PA_INVALID_INDEX) {
                self.candidate.reason = QStringLiteral("The default input is a sink monitor");
            } else {
                self.candidate.muteKnown = true;
                self.candidate.muted = info->mute != 0;
                self.candidate.reason.clear();
            }
        }
    }
    static void subscribed(pa_context*, int success, void* data)
    {
        auto& self = *static_cast<Connection*>(data);
        self.releaseOperation();
        if (!success)
            self.error(QStringLiteral("Cannot subscribe to audio changes"));
        else {
            self.subscriptionReady = true;
            self.refresh();
        }
    }
    static void event(pa_context*, pa_subscription_event_type_t type, uint32_t index, void* data)
    {
        auto& self = *static_cast<Connection*>(data);
        const auto facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        const auto change = type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
        if (facility != PA_SUBSCRIPTION_EVENT_SERVER
            && !(facility == PA_SUBSCRIPTION_EVENT_SOURCE
                && (index == self.selectedIndex || self.selectedIndex == PA_INVALID_INDEX)))
            return;
        ++*self.revision;
        if (facility == PA_SUBSCRIPTION_EVENT_SERVER || change == PA_SUBSCRIPTION_EVENT_REMOVE)
            self.post(self.unknown(QStringLiteral("Default input changed")), true);
        self.refresh();
    }
    static void contextState(pa_context* context, void* data)
    {
        auto& self = *static_cast<Connection*>(data);
        switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
            pa_context_set_subscribe_callback(context, event, &self);
            self.operation = pa_context_subscribe(context,
                static_cast<pa_subscription_mask_t>(
                    PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER),
                subscribed, &self);
            if (!self.operation)
                self.error(QStringLiteral("Cannot subscribe to audio changes"));
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            self.error(QStringLiteral("Audio server disconnected or unavailable"));
            break;
        default:
            break;
        }
    }
};

PulseMicrophoneSource::PulseMicrophoneSource()
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &PulseMicrophoneSource::start);
    m_state.backend = QStringLiteral("pulseaudio");
    m_state.reason = QStringLiteral("Observation disabled");
}
PulseMicrophoneSource::~PulseMicrophoneSource() { stop(); }
void PulseMicrophoneSource::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_retryDelay = 500;
    if (enabled)
        start();
    else {
        stop();
        MicrophoneState state;
        state.backend = QStringLiteral("pulseaudio");
        state.reason = QStringLiteral("Observation disabled");
        publish(state);
    }
}
void PulseMicrophoneSource::start()
{
    stop();
    if (!m_enabled)
        return;
    MicrophoneState state;
    state.backend = QStringLiteral("pulseaudio");
    state.reason = QStringLiteral("Connecting to audio server");
    const quint64 generation = m_generation;
    publish(state);
    if (!m_enabled || m_generation != generation)
        return;
    m_connection = std::make_unique<Connection>(this, m_generation);
    auto& connection = *m_connection;
    connection.loop = pa_threaded_mainloop_new();
    if (!connection.loop) {
        fail(QStringLiteral("Cannot allocate audio event loop"));
        return;
    }
    connection.context
        = pa_context_new(pa_threaded_mainloop_get_api(connection.loop), "klighting microphone observer");
    if (!connection.context) {
        fail(QStringLiteral("Cannot allocate audio connection"));
        return;
    }
    pa_context_set_state_callback(connection.context, Connection::contextState, &connection);
    const int result = pa_context_connect(connection.context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr);
    if (result < 0 || pa_threaded_mainloop_start(connection.loop) < 0) {
        fail(QStringLiteral("Cannot connect to audio server"));
        return;
    }
    connection.started = true;
}
void PulseMicrophoneSource::stop()
{
    ++m_generation;
    m_retry.stop();
    m_connection.reset();
}
void PulseMicrophoneSource::fail(const QString& reason)
{
    stop();
    const quint64 generation = m_generation;
    MicrophoneState state;
    state.backend = QStringLiteral("pulseaudio");
    state.reason = reason;
    publish(state);
    if (m_enabled && m_generation == generation) {
        m_retry.start(m_retryDelay);
        m_retryDelay = std::min(m_retryDelay * 2, 30000);
    }
}
void PulseMicrophoneSource::publish(MicrophoneState state)
{
    if (state.muteKnown)
        m_retryDelay = 500;
    if (state == m_state)
        return;
    m_state = std::move(state);
    Q_EMIT changed();
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "pulse_output_source.h"
#include <pulse/pulseaudio.h>

namespace Ambient
{
struct PulseOutputSource::Connection {
    PulseOutputSource* owner;
    quint64 generation;
    pa_threaded_mainloop* loop = nullptr;
    pa_context* context = nullptr;
    bool busy = false, dirty = false, playing = false, sinkSeen = false;
    QVariantMap values;
    Connection(PulseOutputSource* receiver, quint64 id)
        : owner(receiver)
        , generation(id)
    {
    }
    ~Connection()
    {
        if (loop)
            pa_threaded_mainloop_stop(loop);
        if (context) {
            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        if (loop)
            pa_threaded_mainloop_free(loop);
    }
    void publish(bool failed = false)
    {
        const auto snapshot = values;
        QMetaObject::invokeMethod(
            owner,
            [receiver = owner, id = generation, snapshot, failed] {
                receiver->deliver(id, snapshot, failed);
            },
            Qt::QueuedConnection);
    }
    bool operation(pa_operation* op)
    {
        if (op) {
            pa_operation_unref(op);
            return true;
        }
        values.clear();
        publish(true);
        return false;
    }
    void refresh()
    {
        if (busy) {
            dirty = true;
            return;
        }
        busy = true;
        dirty = false;
        playing = sinkSeen = false;
        values.clear();
        operation(pa_context_get_server_info(
            context,
            [](pa_context* context, const pa_server_info* info, void* data) {
                auto* self = static_cast<Connection*>(data);
                if (!info) {
                    self->publish(true);
                    return;
                }
                if (!info->default_sink_name || !*info->default_sink_name) {
                    self->inputs();
                    return;
                }
                self->operation(pa_context_get_sink_info_by_name(
                    context, info->default_sink_name,
                    [](pa_context*, const pa_sink_info* sink, int end, void* data) {
                        auto* self = static_cast<Connection*>(data);
                        if (end) {
                            self->inputs();
                            return;
                        }
                        if (!sink)
                            return;
                        self->sinkSeen = true;
                        self->values.insert("muted", bool(sink->mute));
                        if (pa_cvolume_valid(&sink->volume))
                            self->values.insert(
                                "volume", 100.0 * pa_cvolume_avg(&sink->volume) / PA_VOLUME_NORM);
                    },
                    self));
            },
            this));
    }
    void inputs()
    {
        operation(pa_context_get_sink_input_info_list(
            context,
            [](pa_context*, const pa_sink_input_info* input, int end, void* data) {
                auto* self = static_cast<Connection*>(data);
                if (!end) {
                    if (input && !input->corked)
                        self->playing = true;
                    return;
                }
                if (end > 0)
                    self->values.insert("playback", self->playing);
                self->busy = false;
                if (self->dirty)
                    self->refresh();
                else
                    self->publish();
            },
            this));
    }
    bool start()
    {
        loop = pa_threaded_mainloop_new();
        if (!loop)
            return false;
        context = pa_context_new(pa_threaded_mainloop_get_api(loop), "klighting output state");
        if (!context)
            return false;
        pa_context_set_state_callback(
            context,
            [](pa_context* context, void* data) {
                auto* self = static_cast<Connection*>(data);
                const auto state = pa_context_get_state(context);
                if (state == PA_CONTEXT_READY) {
                    pa_context_set_subscribe_callback(
                        context,
                        [](pa_context*, pa_subscription_event_type_t, uint32_t, void* data) {
                            static_cast<Connection*>(data)->refresh();
                        },
                        self);
                    self->operation(pa_context_subscribe(
                        context,
                        pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK
                            | PA_SUBSCRIPTION_MASK_SINK_INPUT),
                        [](pa_context*, int success, void* data) {
                            auto* self = static_cast<Connection*>(data);
                            if (success)
                                self->refresh();
                            else {
                                self->values.clear();
                                self->publish(true);
                            }
                        },
                        self));
                } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
                    self->values.clear();
                    self->publish(true);
                }
            },
            this);
        return pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) >= 0
            && pa_threaded_mainloop_start(loop) >= 0;
    }
};
PulseOutputSource::PulseOutputSource()
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &PulseOutputSource::start);
}
PulseOutputSource::~PulseOutputSource() = default;
void PulseOutputSource::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    ++m_generation;
    m_retry.stop();
    m_connection.reset();
    Q_EMIT changed({ });
    if (enabled)
        start();
}
void PulseOutputSource::start()
{
    if (!m_enabled)
        return;
    m_connection = std::make_unique<Connection>(this, ++m_generation);
    if (!m_connection->start()) {
        m_connection.reset();
        m_retry.start();
    }
}
void PulseOutputSource::deliver(quint64 generation, const QVariantMap& values, bool failed)
{
    if (!m_enabled || generation != m_generation)
        return;
    Q_EMIT changed(values);
    if (failed) {
        ++m_generation;
        m_connection.reset();
        m_retry.start();
    }
}
}

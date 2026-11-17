// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipewire_activity_source.h"
#include "media_activity_graph.h"
#include <QSocketNotifier>
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <pipewire/pipewire.h>

namespace Ambient
{
struct PipeWireActivitySource::Connection {
    struct Object {
        Connection* connection;
        quint32 id;
        pw_proxy* proxy = nullptr;
        spa_hook listener { };
        ~Object()
        {
            if (proxy) {
                spa_hook_remove(&listener);
                pw_proxy_destroy(proxy);
            }
        }
    };
    PipeWireActivitySource* owner;
    quint64 generation;
    pw_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook coreListener { }, registryListener { };
    std::unique_ptr<QSocketNotifier> notifier;
    std::map<quint32, std::unique_ptr<Object>> objects;
    MediaActivityGraph graph;
    QTimer publishTimer;
    int sequence = 0;
    bool ready = false, failed = false;

    Connection(PipeWireActivitySource* receiver, quint64 id)
        : owner(receiver)
        , generation(id)
    {
        publishTimer.setSingleShot(true);
        QObject::connect(&publishTimer, &QTimer::timeout, owner, [this] {
            const auto values = ready && !failed ? graph.values() : QVariantMap();
            // A consumer may disable us while handling the snapshot. Leave all connection
            // callbacks before delivery, and reject queued snapshots from older connections.
            QMetaObject::invokeMethod(
                owner,
                [receiver = owner, id = generation, values, error = failed] {
                    receiver->deliver(id, values, error);
                },
                Qt::QueuedConnection);
        });
    }
    ~Connection()
    {
        publishTimer.stop();
        notifier.reset();
        objects.clear();
        if (registry) {
            spa_hook_remove(&registryListener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
        }
        if (core) {
            spa_hook_remove(&coreListener);
            pw_core_disconnect(core);
        }
        if (context)
            pw_context_destroy(context);
        if (loop)
            pw_loop_destroy(loop);
    }
    void schedule(bool error = false)
    {
        failed |= error;
        if (!publishTimer.isActive())
            publishTimer.start(0);
    }
    static void merge(QVariantMap& target, const spa_dict* props)
    {
        if (!props)
            return;
        const spa_dict_item* item;
        spa_dict_for_each(item, props)
        {
            const auto key = QString::fromUtf8(item->key);
            if (item->value)
                target.insert(key, QString::fromUtf8(item->value));
            else
                target.remove(key);
        }
    }
    void added(uint32_t id, uint32_t permissions, const char* type, uint32_t version, const spa_dict* props)
    {
        const bool node = std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0;
        if (!node && std::strcmp(type, PW_TYPE_INTERFACE_Link) != 0)
            return;
        if (node)
            merge(graph.nodes[id].properties, props);
        else
            graph.links.insert(id, { });
        if (!(permissions & PW_PERM_R)) {
            schedule(); // Keep the incomplete snapshot unknown.
            return;
        }
        auto object = std::make_unique<Object>();
        object->connection = this;
        object->id = id;
        object->proxy = static_cast<pw_proxy*>(pw_registry_bind(registry, id, type,
            std::min(version, node ? uint32_t(PW_VERSION_NODE) : uint32_t(PW_VERSION_LINK)), 0));
        if (!object->proxy) {
            schedule(true);
            return;
        }
        if (node) {
            static const pw_node_events events = [] {
                pw_node_events e { };
                e.version = PW_VERSION_NODE_EVENTS;
                e.info = [](void* data, const pw_node_info* info) {
                    auto* object = static_cast<Object*>(data);
                    auto* self = object->connection;
                    auto& node = self->graph.nodes[object->id];
                    if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS)
                        merge(node.properties, info->props);
                    if (!node.known || (info->change_mask & PW_NODE_CHANGE_MASK_STATE))
                        node.running = info->state == PW_NODE_STATE_RUNNING;
                    node.known = true;
                    self->schedule();
                };
                return e;
            }();
            pw_node_add_listener(
                reinterpret_cast<pw_node*>(object->proxy), &object->listener, &events, object.get());
        } else {
            static const pw_link_events events = [] {
                pw_link_events e { };
                e.version = PW_VERSION_LINK_EVENTS;
                e.info = [](void* data, const pw_link_info* info) {
                    auto* object = static_cast<Object*>(data);
                    auto* self = object->connection;
                    auto& link = self->graph.links[object->id];
                    link.output = info->output_node_id;
                    link.input = info->input_node_id;
                    if (!link.known || (info->change_mask & PW_LINK_CHANGE_MASK_STATE))
                        link.active = info->state == PW_LINK_STATE_ACTIVE;
                    link.known = true;
                    self->schedule();
                };
                return e;
            }();
            pw_link_add_listener(
                reinterpret_cast<pw_link*>(object->proxy), &object->listener, &events, object.get());
        }
        objects.emplace(id, std::move(object));
        // Wait for the info events of every newly bound object before publishing absence.
        ready = false;
        sequence = pw_core_sync(core, PW_ID_CORE, sequence);
    }
    bool start()
    {
        static std::once_flag initialized;
        std::call_once(initialized, [] { pw_init(nullptr, nullptr); });
        loop = pw_loop_new(nullptr);
        if (!loop)
            return false;
        context = pw_context_new(loop, nullptr, 0);
        if (!context)
            return false;
        core = pw_context_connect(
            context, pw_properties_new(PW_KEY_APP_NAME, "klighting capture activity", nullptr), 0);
        if (!core)
            return false;
        static const pw_core_events coreEvents = [] {
            pw_core_events e { };
            e.version = PW_VERSION_CORE_EVENTS;
            e.done = [](void* data, uint32_t id, int seq) {
                auto* self = static_cast<Connection*>(data);
                if (id == PW_ID_CORE && seq == self->sequence) {
                    self->ready = true;
                    self->schedule();
                }
            };
            e.error = [](void* data, uint32_t, int, int, const char*) {
                static_cast<Connection*>(data)->schedule(true);
            };
            return e;
        }();
        pw_core_add_listener(core, &coreListener, &coreEvents, this);
        registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        if (!registry)
            return false;
        static const pw_registry_events registryEvents = [] {
            pw_registry_events e { };
            e.version = PW_VERSION_REGISTRY_EVENTS;
            e.global = [](void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version,
                           const spa_dict* props) {
                static_cast<Connection*>(data)->added(id, permissions, type, version, props);
            };
            e.global_remove = [](void* data, uint32_t id) {
                auto* self = static_cast<Connection*>(data);
                self->objects.erase(id);
                self->graph.nodes.remove(id);
                self->graph.links.remove(id);
                self->schedule();
            };
            return e;
        }();
        pw_registry_add_listener(registry, &registryListener, &registryEvents, this);
        sequence = pw_core_sync(core, PW_ID_CORE, sequence);
        notifier = std::make_unique<QSocketNotifier>(pw_loop_get_fd(loop), QSocketNotifier::Read);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, owner, [this] {
            pw_loop_enter(loop);
            const auto result = pw_loop_iterate(loop, 0);
            pw_loop_leave(loop);
            if (result < 0)
                schedule(true);
        });
        return true;
    }
};

PipeWireActivitySource::PipeWireActivitySource()
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &PipeWireActivitySource::start);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { deliver(m_generation, { }, true); });
}
PipeWireActivitySource::~PipeWireActivitySource() = default;
void PipeWireActivitySource::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    ++m_generation;
    m_retry.stop();
    m_timeout.stop();
    m_connection.reset();
    m_values.clear();
    Q_EMIT changed({ });
    if (enabled)
        start();
}
void PipeWireActivitySource::start()
{
    if (!m_enabled)
        return;
    m_connection = std::make_unique<Connection>(this, ++m_generation);
    if (!m_connection->start()) {
        m_connection.reset();
        m_retry.start();
    } else
        m_timeout.start();
}
void PipeWireActivitySource::deliver(quint64 generation, const QVariantMap& values, bool failed)
{
    if (!m_enabled || generation != m_generation)
        return;
    if (!values.isEmpty())
        m_timeout.stop();
    if (failed) {
        ++m_generation;
        m_timeout.stop();
        m_connection.reset();
        m_retry.start();
    }
    if (m_values != values) {
        m_values = values;
        Q_EMIT changed(values);
    }
}
}

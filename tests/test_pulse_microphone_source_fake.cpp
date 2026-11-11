// SPDX-License-Identifier: GPL-3.0-or-later
#include "pulse_microphone_source.h"
#include <QTest>
#include <memory>
#include <pulse/pulseaudio.h>
#include <utility>
#include <vector>

// Deterministic C-ABI fixture: no audio server or worker thread is used. Callback
// order and operation failures are controlled independently of the adapter.
struct pa_threaded_mainloop { };
struct pa_context {
    pa_context_notify_cb_t stateCallback = nullptr;
    void* stateData = nullptr;
    pa_context_subscribe_cb_t eventCallback = nullptr;
    void* eventData = nullptr;
    pa_context_state_t state = PA_CONTEXT_CONNECTING;
    int error = PA_OK;
};
struct pa_operation {
    pa_context_success_cb_t success = nullptr;
    pa_server_info_cb_t server = nullptr;
    pa_source_info_cb_t source = nullptr;
    void* data = nullptr;
    bool released = false;
    bool cancelled = false;
};
namespace
{
pa_context* currentContext = nullptr;
std::vector<std::unique_ptr<pa_operation>> operations;
bool failOperation = false;
bool wrongFlags = false;
pa_operation* operation(void* data)
{
    if (std::exchange(failOperation, false))
        return nullptr;
    operations.push_back(std::make_unique<pa_operation>());
    operations.back()->data = data;
    return operations.back().get();
}
void ready()
{
    currentContext->state = PA_CONTEXT_READY;
    currentContext->stateCallback(currentContext, currentContext->stateData);
}
void subscribed(bool success = true)
{
    auto* op = operations.back().get();
    op->success(currentContext, success, op->data);
}
void server(const char* name)
{
    auto* op = operations.back().get();
    pa_server_info info { };
    info.default_source_name = name;
    op->server(currentContext, &info, op->data);
}
void source(bool muted, uint32_t monitor = PA_INVALID_INDEX)
{
    auto* op = operations.back().get();
    pa_source_info info { };
    info.index = 42;
    info.name = "fixture";
    info.description = "Synthetic input";
    info.mute = muted;
    info.monitor_of_sink = monitor;
    // Suspension, zero volume and absent recording streams do not imply mute.
    info.state = PA_SOURCE_SUSPENDED;
    op->source(currentContext, &info, 0, op->data);
    op->source(currentContext, nullptr, 1, op->data);
}
void sendEvent(pa_subscription_event_type_t type, uint32_t index = 42)
{
    currentContext->eventCallback(currentContext, type, index, currentContext->eventData);
}
void drain() { QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
}

extern "C" {
pa_threaded_mainloop* pa_threaded_mainloop_new() { return new pa_threaded_mainloop; }
void pa_threaded_mainloop_free(pa_threaded_mainloop* loop) { delete loop; }
int pa_threaded_mainloop_start(pa_threaded_mainloop*) { return 0; }
void pa_threaded_mainloop_stop(pa_threaded_mainloop*) { }
pa_mainloop_api* pa_threaded_mainloop_get_api(pa_threaded_mainloop*) { return nullptr; }
pa_context* pa_context_new(pa_mainloop_api*, const char*) { return currentContext = new pa_context; }
void pa_context_unref(pa_context* context)
{
    delete context;
    currentContext = nullptr;
}
void pa_context_disconnect(pa_context*) { }
void pa_context_set_state_callback(pa_context* context, pa_context_notify_cb_t callback, void* data)
{
    context->stateCallback = callback;
    context->stateData = data;
}
void pa_context_set_subscribe_callback(pa_context* context, pa_context_subscribe_cb_t callback, void* data)
{
    context->eventCallback = callback;
    context->eventData = data;
}
int pa_context_connect(pa_context*, const char* serverName, pa_context_flags_t flags, const pa_spawn_api*)
{
    wrongFlags |= serverName != nullptr || flags != PA_CONTEXT_NOAUTOSPAWN;
    return 0;
}
pa_context_state_t pa_context_get_state(const pa_context* context) { return context->state; }
int pa_context_errno(const pa_context* context) { return context->error; }
pa_operation* pa_context_subscribe(
    pa_context*, pa_subscription_mask_t mask, pa_context_success_cb_t callback, void* data)
{
    wrongFlags |= mask != (PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER);
    auto* op = operation(data);
    if (op)
        op->success = callback;
    return op;
}
pa_operation* pa_context_get_server_info(pa_context*, pa_server_info_cb_t callback, void* data)
{
    auto* op = operation(data);
    if (op)
        op->server = callback;
    return op;
}
pa_operation* pa_context_get_source_info_by_name(
    pa_context*, const char*, pa_source_info_cb_t callback, void* data)
{
    auto* op = operation(data);
    if (op)
        op->source = callback;
    return op;
}
void pa_operation_unref(pa_operation* op) { op->released = true; }
void pa_operation_cancel(pa_operation* op) { op->cancelled = true; }
}

class PulseMicrophoneFakeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        operations.clear();
        failOperation = false;
        wrongFlags = false;
    }
    void cleanup()
    {
        QVERIFY(!currentContext);
        QVERIFY(!wrongFlags);
        for (const auto& op : operations)
            QVERIFY(op->released);
    }
    void subscriptionPrecedesLookupAndQueuedSnapshot()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        QCOMPARE(operations.size(), size_t(1));
        QVERIFY(operations.back()->success);
        subscribed();
        server("virtual_input");
        source(false);
        QVERIFY(!observer.state().muteKnown);
        drain();
        QVERIFY(observer.state().available);
        QVERIFY(observer.state().muteKnown);
        QVERIFY(!observer.state().muted);
        QCOMPARE(observer.state().sourceName, QStringLiteral("virtual_input"));
    }
    void coalescesChangesAndRejectsOldSelection()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed();
        server("old_default");
        const auto pendingCount = operations.size();
        for (int n = 0; n < 30; ++n)
            sendEvent(PA_SUBSCRIPTION_EVENT_SERVER);
        QCOMPARE(operations.size(), pendingCount);
        source(false);
        drain();
        QVERIFY(!observer.state().muteKnown);
        QCOMPARE(operations.size(), pendingCount + 1);
        server("new_default");
        source(true);
        drain();
        QCOMPARE(observer.state().sourceName, QStringLiteral("new_default"));
        QVERIFY(observer.state().muted);
        sendEvent(static_cast<pa_subscription_event_type_t>(
            PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_REMOVE));
        drain();
        QVERIFY(!observer.state().muteKnown);
    }
    void eventBeforeAcknowledgementWaitsForSubscription()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        sendEvent(PA_SUBSCRIPTION_EVENT_SERVER);
        QCOMPARE(operations.size(), size_t(1));
        subscribed();
        server("after_ack");
        source(false);
        drain();
        QVERIFY(observer.state().muteKnown);
    }
    void queuedSnapshotCannotOutliveNewSelectionOrFailure()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed();
        server("old");
        source(false);
        sendEvent(PA_SUBSCRIPTION_EVENT_SERVER);
        drain();
        QVERIFY(!observer.state().muteKnown);
        server("new");
        source(false);
        currentContext->state = PA_CONTEXT_FAILED;
        currentContext->stateCallback(currentContext, currentContext->stateData);
        drain();
        QVERIFY(!observer.state().available);
        QVERIFY(!observer.state().muteKnown);
    }
    void queuedInvalidationSurvivesLaterSourceChange()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed();
        server("old");
        source(false);
        drain();
        QVERIFY(observer.state().muteKnown);
        sendEvent(PA_SUBSCRIPTION_EVENT_SERVER);
        sendEvent(static_cast<pa_subscription_event_type_t>(
            PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_CHANGE));
        drain();
        QVERIFY(!observer.state().muteKnown);
        server("new");
        source(true);
        drain();
        // The first lookup was invalidated by the extra event and is repeated.
        QVERIFY(!observer.state().muteKnown);
        server("new");
        source(true);
        drain();
        QVERIFY(observer.state().muteKnown);
        QVERIFY(observer.state().muted);
    }
    void disableDropsQueuedGenerationAndCancelsPendingRequest()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed();
        server("old");
        source(false);
        observer.setEnabled(false);
        observer.setEnabled(true);
        ready();
        drain();
        QVERIFY(!observer.state().muteKnown);
        auto* pending = operations.back().get();
        observer.setEnabled(false);
        QVERIFY(pending->cancelled);
        QVERIFY(pending->released);
    }
    void destructionCancelsPendingSourceRead()
    {
        auto observer = std::make_unique<Ambient::PulseMicrophoneSource>();
        observer->setEnabled(true);
        ready();
        subscribed();
        server("pending");
        auto* pending = operations.back().get();
        QVERIFY(pending->source);
        observer.reset();
        QVERIFY(pending->cancelled);
        QVERIFY(pending->released);
        drain();
        QVERIFY(!currentContext);
    }
    void disablingStopsScheduledRetry()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed(false);
        drain();
        QVERIFY(!currentContext);
        observer.setEnabled(false);
        QTest::qWait(650);
        QVERIFY(!currentContext);
        QVERIFY(!observer.state().muteKnown);
    }
    void changedHandlerMayDisableObservation()
    {
        Ambient::PulseMicrophoneSource observer;
        connect(&observer, &Ambient::MicrophoneSource::changed, &observer,
            [&observer] { observer.setEnabled(false); });
        observer.setEnabled(true);
        QVERIFY(!currentContext);
        QVERIFY(!observer.state().muteKnown);
    }
    void monitorAndMissingDefaultRemainUnknown()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed();
        server("not_named_monitor");
        source(false, 1);
        drain();
        QVERIFY(observer.state().available);
        QVERIFY(!observer.state().muteKnown);
        sendEvent(PA_SUBSCRIPTION_EVENT_SERVER);
        server(nullptr);
        drain();
        QVERIFY(!observer.state().muteKnown);
        QVERIFY(observer.state().sourceName.isEmpty());
    }
    void missingInputKeepsSubscriptionAndRecoversOnHotplug()
    {
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed();
        server("@DEFAULT_SOURCE@");
        auto* context = currentContext;
        auto* pending = operations.back().get();
        context->error = PA_ERR_NOENTITY;
        pending->source(context, nullptr, -1, pending->data);
        drain();
        QVERIFY(observer.state().available);
        QVERIFY(!observer.state().muteKnown);
        QVERIFY(pending->released);
        QTest::qWait(650);
        QCOMPARE(currentContext, context);
        QCOMPARE(operations.back().get(), pending);
        sendEvent(static_cast<pa_subscription_event_type_t>(
                      PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_NEW),
            73);
        server("hotplugged");
        source(false);
        drain();
        QCOMPARE(currentContext, context);
        QVERIFY(observer.state().muteKnown);
        QVERIFY(!observer.state().muted);
        QCOMPARE(observer.state().sourceName, QStringLiteral("hotplugged"));
    }
    void missingInputInvalidationSurvivesHotplug_data()
    {
        QTest::addColumn<bool>("eventBeforeError");
        QTest::newRow("event-during-lookup") << true;
        QTest::newRow("event-before-delivery") << false;
    }
    void missingInputInvalidationSurvivesHotplug()
    {
        QFETCH(bool, eventBeforeError);
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        ready();
        subscribed();
        server("old");
        source(false);
        drain();
        QVERIFY(observer.state().muteKnown);
        auto* context = currentContext;
        sendEvent(static_cast<pa_subscription_event_type_t>(
            PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_CHANGE));
        server("old");
        auto* pending = operations.back().get();
        const auto hotplug = [] {
            sendEvent(static_cast<pa_subscription_event_type_t>(
                          PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_NEW),
                73);
        };
        if (eventBeforeError)
            hotplug();
        context->error = PA_ERR_NOENTITY;
        pending->source(context, nullptr, -1, pending->data);
        if (!eventBeforeError)
            hotplug();
        drain();
        QCOMPARE(currentContext, context);
        QVERIFY(observer.state().available);
        QVERIFY(!observer.state().muteKnown);
        server("replacement");
        source(true);
        drain();
        QVERIFY(observer.state().muteKnown);
        QVERIFY(observer.state().muted);
        QCOMPARE(observer.state().sourceName, QStringLiteral("replacement"));
    }
    void failuresRetryWithoutEvents_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("subscribe-allocation") << 0;
        QTest::newRow("subscribe-ack") << 1;
        QTest::newRow("server-allocation") << 2;
        QTest::newRow("server-null") << 3;
        QTest::newRow("source-allocation") << 4;
        QTest::newRow("source-error") << 5;
        QTest::newRow("disconnect") << 6;
    }
    void failuresRetryWithoutEvents()
    {
        QFETCH(int, failure);
        Ambient::PulseMicrophoneSource observer;
        observer.setEnabled(true);
        if (failure == 0)
            failOperation = true;
        ready();
        if (failure != 0) {
            if (failure == 2)
                failOperation = true;
            subscribed(failure != 1);
        }
        if (failure >= 3) {
            if (failure == 3) {
                auto* op = operations.back().get();
                op->server(currentContext, nullptr, op->data);
            } else {
                if (failure == 4)
                    failOperation = true;
                server("fixture");
                if (failure == 5) {
                    currentContext->error = PA_ERR_INTERNAL;
                    auto* op = operations.back().get();
                    op->source(currentContext, nullptr, -1, op->data);
                } else if (failure == 6) {
                    currentContext->state = PA_CONTEXT_FAILED;
                    currentContext->stateCallback(currentContext, currentContext->stateData);
                }
            }
        }
        drain();
        QVERIFY(!observer.state().available);
        QVERIFY(!currentContext);
        QTRY_VERIFY_WITH_TIMEOUT(currentContext != nullptr, 1500);
        ready();
        subscribed();
        server("recovered");
        source(false);
        drain();
        QVERIFY(observer.state().muteKnown);
    }
};
QTEST_GUILESS_MAIN(PulseMicrophoneFakeTest)
#include "test_pulse_microphone_source_fake.moc"

# Runtime measurements

Build with `BUILD_TESTING=ON`, then run `./build/runtime_benchmark`. It prints JSON
to stdout and is not part of CTest. `--runs 1` provides a shorter smoke run; the default
three repetitions are intended for before/after comparisons.

The benchmark constructs `LightingRuntime` with the synthetic strip plugin in its
build directory and temporary configuration. It never constructs `LightingService`,
connects to D-Bus, captures a screen or opens a hardware backend. Each case warms up
for one second and measures two seconds. Failed device/effect initialization rejects
the run instead of reporting an idle result.

| Scenario | Purpose |
| --- | --- |
| Solid idle | Static output without screen input |
| Settled Ambient | Repeated identical samples at 20 FPS, LED setting 60 FPS |
| Changing Ambient | Alternating input with smoothing and LED setting 60 FPS |
| Ambient 20 FPS / output 1 FPS | Independent input/output cadence |
| Notifications only | Pulse animation without screen capture |
| Three devices, mixed Ambient | Shared fast/slow states, three independently composed outputs |

Each fake device has three strip elements. The mixed case declares two modifier
layers but keeps Base active; it combines Ambient states at 20/4 sampling FPS and
60/10 LED FPS. Metadata records CPU model, Qt/compiler, build type/identity and
scenario parameters. Compare identical parameters and avoid concurrent builds or
other benchmarks. CPU time is process CPU time for this fixture, not whole-session
or compositor CPU use; short-run timing differences can be noise.

## Counters

Runtime status exposes the following counters:

- `samples`: accepted screen samples, after validation and sampling rate limiting.
- `animationWakeups`: runtime scheduling timer callbacks, including deferred sample
  processing; excludes the external input producer and discovery timers.
- `ticks`: runtime tick calls, including calls caused by input or configuration.
- `effectUpdates`: effect-state updates after configuration/selection changes.
- `profileResolutions`: per-device profile/layer resolutions.
- `compositions`: per-device composition calls, before adaptation and submission.
- `sent`: backend-reported completed frames. It does not count individual USB packets.

The runtime/composition counters accumulate for that runtime's lifetime. `sent` sums
the current backends and can reset when a backend is replaced; compare it only while
the backend set is unchanged.

The benchmark reports deltas over the measurement interval, labels `sent` as
`fakeBackendSubmissions`, and separately records injected samples and Qt dispatcher
`awake` signals. The latter includes the synthetic input producer and is not a kernel
wakeup or power measurement. A fake backend accepts every submission, so its frame
count is not evidence of physical USB traffic or backend deduplication.

KWin capture and USB workers require separate measurements before their optimizations.
Record repaint requests, captures/readbacks and real transfers in those components;
do not infer them from these CPU results. GPU frame pacing, energy, firmware diffusion,
direct scanout and hotplug behavior still require an explicitly authorized live test.

## Capture scheduling

`./build/capture_schedule_benchmark` runs the same `CaptureSchedule` used by the KWin
effect, with synthetic paints and readback results. It uses no compositor, D-Bus, GL
context or hardware. The default is three runs per scenario with one-second warmup
and two-second measurement; `--runs 1` is a smoke check. Cases cover static screens,
continuous paints, a final change before a capture deadline, slow readback and a final
change while readback is pending. Missing initial/final synthetic frames fail the run.

After the initial frame, an idle screen requires no capture timer. Actual screen
paints mark input dirty; changes before the sampling deadline or during readback
produce one deferred capture. Pending readback is polled every 16 ms independently
of capture FPS. Natural paints can capture at the sampling deadline; forced repaint
waits an additional 16 ms to avoid competing with the next animation frame. This
grace period is fixed to the deadline, so continued paints cannot postpone it.
A repaint request without a usable target retries at most every
250 ms; it does not spin on an overdue capture deadline. Continued paints, even if
their pixels happen to be identical, still require sampling. The policy does not
compare whole-screen images or remove the direct-scanout restriction.

The counters measure this policy harness. They do not include KWin rendering or GPU
cost, and the synthetic completion delay is not a measured GPU latency. For later
live verification, the effect's existing `debug` output also includes `timerWakeups`,
`repaintRequests` and `polls`, alongside `paints`, `captures` and `samples`. Compare
deltas within one effect instance; reloading resets them. Reading those counters and
observing the final frame in a real session remain separate from this benchmark.

## Profile resolution

`./build/profile_benchmark` measures synchronous runtime inputs with static fake devices:
tracked keys that leave the selected layer unchanged, windows sharing a preset, Shift,
Ctrl+key, Caps Lock, application presets, battery lightbar policy, preset edits and three
devices changing layers together. Each case warms up with 200 events and measures 1000,
with three repetitions by default. Metadata records the environment and scenario sizes.
The harness checks device initialization and final Base output; behavioral regressions
are covered separately by `profile_resolution_tests`.

There is no D-Bus, screen capture or hardware I/O. Input loops run synchronously without
an event loop, so this measures computation and fake submissions rather than desktop
latency, animation or periodic discovery cost. Compare `profileResolutions`,
`effectUpdates` and `compositions` as well as CPU time. Unchanged-layer/preset inputs
should not rebuild or resubmit; real transitions still produce output. Do not infer
USB writes or physical input responsiveness from the synthetic counters.

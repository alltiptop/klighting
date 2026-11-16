// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_sources.h"
#if defined(KLIGHTING_HAVE_PULSEAUDIO)
#include "pulse_microphone_source.h"
#endif

namespace Ambient
{
namespace
{
    class UnavailableMicrophoneSource final : public MicrophoneSource
    {
    public:
        void setEnabled(bool) override { }
        MicrophoneState state() const override
        {
            MicrophoneState result;
            result.reason = QStringLiteral("PulseAudio support was not compiled");
            return result;
        }
    };
}
std::unique_ptr<MicrophoneSource> microphoneSource()
{
#if defined(KLIGHTING_HAVE_PULSEAUDIO)
    return std::make_unique<PulseMicrophoneSource>();
#else
    return std::make_unique<UnavailableMicrophoneSource>();
#endif
}
}

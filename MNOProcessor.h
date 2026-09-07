#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "MNOOscillator.h"
#include "MNOModulation.h"
#include "MNOParameters.h"
#include "MNOPlatform.h"
#include "MNOTypes.h"

namespace mno
{

inline constexpr clap_id parameterIdBase = 0x4d4e0001u;
inline constexpr std::size_t parameterCount = mnoParameterEndpoints.size();

struct MNOKey
{
    int32_t noteId = -1;
    int16_t port = 0;
    int16_t channel = 0;
    int16_t note = 0;

    friend bool operator== (const MNOKey& a, const MNOKey& b) noexcept
    {
        return a.noteId == b.noteId
            && a.port == b.port
            && a.channel == b.channel
            && a.note == b.note;
    }

    bool matches (const clap_event_note_t& address) const noexcept
    {
        return (address.note_id < 0 || noteId == address.note_id)
            && (address.port_index < 0 || port == address.port_index)
            && (address.channel < 0 || channel == address.channel)
            && (address.key < 0 || note == address.key);
    }
};

class MNOScopeRing
{
public:
    // Holds two cycles of the on-screen keyboard's lowest note (C1)
    // at the app's supported maximum 96 kHz sample rate.
    static constexpr size_t capacity = 16384;
    static constexpr size_t triggerHistoryCapacity = 512;
    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<uint64_t>::is_always_lock_free);

    void reset() noexcept
    {
        writePosition = 0;
        triggerPosition = 0;
        writeCount.store (0, std::memory_order_relaxed);
        triggerCount.store (0, std::memory_order_relaxed);
    }

    void push (float audio, float trigger) noexcept
    {
        const auto index = size_t (writePosition % capacity);
        audioSamples[index].store (audio, std::memory_order_relaxed);

        if (trigger >= 0.5f)
        {
            triggerPositions[
                triggerPosition % triggerHistoryCapacity
            ].store (writePosition, std::memory_order_relaxed);
            ++triggerPosition;
            triggerCount.store (
                triggerPosition, std::memory_order_release);
        }

        ++writePosition;
        writeCount.store (writePosition, std::memory_order_release);
    }

    int copyTriggered (
        float* audio, int requestedCapacity) const noexcept
    {
        if (audio == nullptr || requestedCapacity <= 0)
            return 0;

        const auto requested = std::min<size_t> (
            static_cast<size_t> (requestedCapacity), capacity);
        const auto end = writeCount.load (std::memory_order_acquire);
        const auto available = std::min<uint64_t> (end, capacity);
        const auto count = std::min<size_t> (requested, size_t (available));
        if (count == 0)
            return 0;

        const auto oldestAvailable = end - available;
        const auto latestStart = end - count;
        auto start = latestStart;

        const auto triggers =
            triggerCount.load (std::memory_order_acquire);
        if (triggers >= 2)
        {
            const auto latest = triggerPositions[
                size_t ((triggers - 1) % triggerHistoryCapacity)
            ].load (std::memory_order_relaxed);
            const auto previous = triggerPositions[
                size_t ((triggers - 2) % triggerHistoryCapacity)
            ].load (std::memory_order_relaxed);

            if (previous >= oldestAvailable
                && previous < latest
                && latest <= end)
            {
                const auto interval = latest - previous;
                const auto reconstructSparseWindow =
                    interval < sparseDisplaySampleThreshold;
                const auto outputCount = reconstructSparseWindow
                    ? requested
                    : std::min<uint64_t> (interval, requested);

                if (outputCount == 1)
                {
                    audio[0] = sampleAt (previous);
                }
                else if (outputCount == interval)
                {
                    for (uint64_t i = 0; i < outputCount; ++i)
                        audio[i] = sampleAt (previous + i);
                }
                else
                {
                    const auto scale =
                        double (interval - 1) / double (outputCount - 1);
                    for (uint64_t i = 0; i < outputCount; ++i)
                        audio[i] = displaySample (
                            previous,
                            interval,
                            double (i) * scale,
                            reconstructSparseWindow);
                }

                return int (outputCount);
            }
        }

        if (count == requested)
        {
            const auto history = std::min<uint64_t> (
                triggers, triggerHistoryCapacity);

            for (uint64_t offset = 0; offset < history; ++offset)
            {
                const auto slot = size_t (
                    (triggers - 1 - offset)
                    % triggerHistoryCapacity);
                const auto candidate = triggerPositions[slot].load (
                    std::memory_order_relaxed);
                if (candidate >= oldestAvailable
                    && candidate <= latestStart)
                {
                    start = candidate;
                    break;
                }
            }
        }

        for (size_t i = 0; i < count; ++i)
            audio[i] = sampleAt (start + i);
        return int (count);
    }

private:
    // At 48 kHz a two-cycle window drops below this around F#4/G4,
    // where individual sample-to-sample segments become visible.
    static constexpr uint64_t sparseDisplaySampleThreshold = 256;

    float displaySample (
        uint64_t start,
        uint64_t interval,
        double position,
        bool interpolateCubic) const noexcept
    {
        const auto last = interval - 1;
        const auto lower = std::min<uint64_t> (
            uint64_t (position), last);
        const auto upper = std::min<uint64_t> (lower + 1, last);
        const auto fraction = float (position - double (lower));
        const auto p1 = sampleAt (start + lower);
        const auto p2 = sampleAt (start + upper);

        if (! interpolateCubic || lower == upper || fraction <= 0.0f)
            return p1 + (p2 - p1) * fraction;

        const auto p0 = sampleAt (
            start + (lower > 0 ? lower - 1 : lower));
        const auto p3 = sampleAt (
            start + std::min<uint64_t> (upper + 1, last));
        const auto t2 = fraction * fraction;
        const auto t3 = t2 * fraction;
        const auto reconstructed = 0.5f * (
            (2.0f * p1)
            + (-p0 + p2) * fraction
            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);

        // Avoid adding display-only overshoot around sharp BLEP landmarks.
        return std::clamp (
            reconstructed, std::min (p1, p2), std::max (p1, p2));
    }

    float sampleAt (uint64_t position) const noexcept
    {
        return audioSamples[size_t (position % capacity)].load (
            std::memory_order_relaxed);
    }

    std::array<std::atomic<float>, capacity> audioSamples {};
    std::array<
        std::atomic<uint64_t>,
        triggerHistoryCapacity
    > triggerPositions {};
    std::atomic<uint64_t> writeCount { 0 };
    std::atomic<uint64_t> triggerCount { 0 };
    uint64_t writePosition = 0;
    uint64_t triggerPosition = 0;
};

class MNOADSRDisplayTracker
{
public:
    enum class Stage : int32_t
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    void reset() noexcept
    {
        stage = Stage::idle;
        progress = 0.0f;
    }

    void trigger() noexcept
    {
        stage = Stage::attack;
        progress = 0.0f;
    }

    void release() noexcept
    {
        stage = Stage::release;
        progress = 0.0f;
    }

    void restartIf (Stage parameterStage) noexcept
    {
        if (stage == parameterStage)
            progress = 0.0f;
    }

    void advance (
        chardsp::ADSREnvelope<float>::Stage envelopeStage,
        float envelopeProgress,
        float attack,
        float decay,
        float releaseTime,
        float sampleRate) noexcept
    {
        const auto nextProgress = std::clamp (
            envelopeProgress, 0.0f, 1.0f);

        if (envelopeStage == chardsp::ADSREnvelope<float>::Stage::idle)
        {
            reset();
            return;
        }

        if (envelopeStage == chardsp::ADSREnvelope<float>::Stage::decay
            && nextProgress >= 1.0f)
        {
            stage = Stage::sustain;
            progress = 0.0f;
            return;
        }

        const auto nextStage = [&]
        {
            switch (envelopeStage)
            {
                case chardsp::ADSREnvelope<float>::Stage::attack:
                    return Stage::attack;
                case chardsp::ADSREnvelope<float>::Stage::decay:
                    return Stage::decay;
                case chardsp::ADSREnvelope<float>::Stage::release:
                case chardsp::ADSREnvelope<float>::Stage::forcedRelease:
                    return Stage::release;
                case chardsp::ADSREnvelope<float>::Stage::idle:
                    break;
            }

            return Stage::idle;
        }();

        if (stage != nextStage)
            progress = 0.0f;
        stage = nextStage;

        const auto seconds = stage == Stage::attack ? attack
            : stage == Stage::decay ? decay : releaseTime;
        progress = std::min (
            1.0f,
            progress + 1.0f / std::max (1.0f, seconds * sampleRate));
    }

    int32_t getStage() const noexcept
    {
        return static_cast<int32_t> (stage);
    }

    float getProgress() const noexcept { return progress; }

private:
    Stage stage = Stage::idle;
    float progress = 0.0f;
};

class MNOModulationSnapshot
{
public:
    static constexpr size_t valueCount = 17;
    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<uint64_t>::is_always_lock_free);

    MNOModulationSnapshot() noexcept { store ({}); }

    void store (const MNOModulationState& state) noexcept
    {
        const std::array<float, valueCount> next {
            state.lfoPhase,
            state.lfoValue,
            state.adsrValue,
            float (state.adsrActive),
            float (state.adsrStage),
            state.adsrStageProgress,
            state.adsrVelocityScale,
            state.lfo2Phase,
            state.lfo2Value,
            state.adsr2Value,
            float (state.adsr2Active),
            float (state.adsr2Stage),
            state.adsr2StageProgress,
            state.osc1Shape,
            state.osc2Shape,
            state.filterCutoff,
            state.filterResonance
        };

        sequence.fetch_add (1, std::memory_order_acq_rel);
        for (size_t index = 0; index < values.size(); ++index)
            values[index].store (next[index], std::memory_order_relaxed);
        sequence.fetch_add (1, std::memory_order_release);
    }

    MNOModulationState load() const noexcept
    {
        std::array<float, valueCount> snapshot {};
        for (;;)
        {
            const auto before =
                sequence.load (std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;

            for (size_t index = 0; index < values.size(); ++index)
                snapshot[index] =
                    values[index].load (std::memory_order_relaxed);

            std::atomic_thread_fence (std::memory_order_seq_cst);
            const auto after =
                sequence.load (std::memory_order_acquire);
            if (before == after)
                break;
        }

        return {
            snapshot[0],
            snapshot[1],
            snapshot[2],
            int32_t (snapshot[3]),
            int32_t (snapshot[4]),
            snapshot[5],
            snapshot[6],
            snapshot[7],
            snapshot[8],
            snapshot[9],
            int32_t (snapshot[10]),
            int32_t (snapshot[11]),
            snapshot[12],
            snapshot[13],
            snapshot[14],
            snapshot[15],
            snapshot[16]
        };
    }

private:
    mutable std::atomic<uint64_t> sequence { 0 };
    std::array<std::atomic<float>, valueCount> values {};
};

class MNOProcessor
{
public:
    using MonoController =
        chardsp::MonoVoiceController<float, MNOKey, 16>;

    void setScopeRing (MNOScopeRing* ring) noexcept { scopeRing = ring; }

    MNOProcessor() noexcept;

    bool prepare (double newSampleRate,
                  uint32_t,
                  uint32_t newMaximumFrameCount) noexcept
    {
        if (! std::isfinite (newSampleRate) || newSampleRate <= 0.0
            || newMaximumFrameCount == 0)
            return false;
        sampleRate = float (newSampleRate);
        maximumFrameCount = newMaximumFrameCount;
        oscillator1.prepare (sampleRate);
        oscillator2.prepare (sampleRate);
        controller.prepare (sampleRate);
        portamento.prepare (sampleRate);
        envelope1.prepare (sampleRate);
        envelope2.prepare (sampleRate);
        filter.prepare (sampleRate);
        reset();
        return true;
    }

    void reset() noexcept
    {
        oscillator1.reset();
        oscillator2.reset();
        controller.reset();
        portamento.reset (69.0f);
        envelope1.reset();
        envelope1.setShape (chardsp::EnvelopeShape::exponential);
        envelope2.reset();
        envelope2.setShape (chardsp::EnvelopeShape::exponential);
        filter.reset();
        filter.setMode (chardsp::FilterMode::lowpass);
        applyAllParameters();
        pitchBend = 0.0f;
        noteVelocity.reset (0.0f);
        lfo1Phase = 0.0f;
        lfo2Phase = 0.0f;
        osc1PWMPhase = 0.0f;
        osc2PWMPhase = 0.0f;
        adsr1Display.reset();
        adsr2Display.reset();
        latestModulationState.store ({});
        resetScopeTrigger();
        scopeUsesOscillator2 = false;
        if (scopeRing != nullptr)
            scopeRing->reset();
    }

    clap_process_status process (const clap_process_t& raw,
                                 uint16_t parameterRampSpaceId) noexcept
    {
        if (raw.frames_count > maximumFrameCount
            || raw.audio_outputs_count == 0 || raw.audio_outputs == nullptr)
            return CLAP_PROCESS_ERROR;
        auto& outputBuffer = raw.audio_outputs[0];
        if ((outputBuffer.data32 == nullptr) == (outputBuffer.data64 == nullptr))
            return CLAP_PROCESS_ERROR;

        for (auto& parameterState : parameters)
            (void) parameterState.consumePublishedBaseOnAudioThread();

        MNOModulationState blockModulationState {};
        const mno::ProcessView processView { raw };
        mno::processEventChunks (
            processView.inputEvents(),
            raw.frames_count,
            [&] (const clap_event_header_t& event) noexcept
            {
                handleEvent (event, parameterRampSpaceId);
            },
            [&] (uint32_t begin, uint32_t end) noexcept
        {
            for (uint32_t frame = begin; frame < end; ++frame)
            {
                for (auto& parameterState : parameters)
                    (void) parameterState.nextGlobalValue();
                applyAllParameters();
                applyAction (controller.next());

            lfo1Phase += value (MNOParameter::lfoRate) / sampleRate;
            lfo1Phase -= std::floor (lfo1Phase);
            lfo2Phase +=
                value (MNOParameter::lfo2Rate) / sampleRate;
            lfo2Phase -= std::floor (lfo2Phase);
            osc1PWMPhase +=
                value (MNOParameter::osc1PWMRate) / sampleRate;
            osc1PWMPhase -= std::floor (osc1PWMPhase);
            osc2PWMPhase +=
                value (MNOParameter::osc2PWMRate) / sampleRate;
            osc2PWMPhase -= std::floor (osc2PWMPhase);

            const auto rawLFO1 = lfoSample (
                lfo1Phase,
                value (MNOParameter::lfoWaveform));
            const auto rawLFO2 = lfoSample (
                lfo2Phase,
                value (MNOParameter::lfo2Waveform));
            const auto envelope1Value = envelope1.next();
            const auto envelope2Value = envelope2.next();
            adsr1Display.advance (
                envelope1.getStage(), envelope1.getStageProgress(),
                value (MNOParameter::attack),
                value (MNOParameter::decay),
                value (MNOParameter::release), sampleRate);
            adsr2Display.advance (
                envelope2.getStage(), envelope2.getStageProgress(),
                value (MNOParameter::adsr2Attack),
                value (MNOParameter::adsr2Decay),
                value (MNOParameter::adsr2Release), sampleRate);

            const auto velocityAmount =
                value (MNOParameter::velocitySensitivity) * 0.01f;
            const auto velocityScale =
                1.0f + velocityAmount * (noteVelocity.next() - 1.0f);
            const MNOModulationFrame modulation {
                {
                    rawLFO1
                        * value (MNOParameter::lfoDepth) * 0.01f,
                    rawLFO2
                        * value (MNOParameter::lfo2Depth) * 0.01f,
                    envelope1Value * velocityScale,
                    envelope2Value
                }
            };

            const auto modulated = [&] (
                MNOParameter destination,
                float directContribution = 0.0f)
            {
                const auto& routes = mnoModulationRoutes (destination);
                if (! routes.isRoutable)
                    return value (destination);

                std::array<float, mnoModulationSourceCount> amounts {};
                for (size_t index = 0;
                     index < mnoModulationSourceCount; ++index)
                    amounts[index] = value (routes.amounts[index]);

                return mnoModulatedValue (
                    destination,
                    value (destination),
                    modulation,
                    amounts,
                    directContribution);
            };

            const auto pitch = portamento.next()
                             + pitchBend
                                 * value (
                                     MNOParameter::pitchBendRange);
            const auto frequency = midiToFrequency (pitch);
            const auto frequency1 = frequency * centsRatio (
                modulated (MNOParameter::osc1Tune));
            const auto frequency2 = frequency * centsRatio (
                modulated (MNOParameter::osc2Tune));

            const auto osc1Shape = modulated (
                MNOParameter::osc1Shape,
                lfoSample (osc1PWMPhase, 0.0f)
                    * value (MNOParameter::osc1PWMDepth) * 0.01f);
            const auto first = oscillator1.next (
                frequency1,
                std::nullopt,
                osc1Shape - value (MNOParameter::osc1Shape));
            std::optional<float> sync;
            if (value (MNOParameter::hardSync) >= 0.5f
                && first.trigger > 0.5f)
                sync = first.wrapOffset;

            const auto osc2Shape = modulated (
                MNOParameter::osc2Shape,
                lfoSample (osc2PWMPhase, 0.0f)
                    * value (MNOParameter::osc2PWMDepth) * 0.01f);
            const auto second = oscillator2.next (
                frequency2,
                sync,
                osc2Shape - value (MNOParameter::osc2Shape));
            const auto osc1Level = modulated (MNOParameter::osc1Level);
            const auto osc2Level = modulated (MNOParameter::osc2Level);
            const auto mixed =
                osc1Level * first.sample + osc2Level * second.sample;

            const auto cutoff = std::clamp (
                modulated (MNOParameter::cutoff),
                20.0f, sampleRate * 0.45f);
            const auto resonance =
                modulated (MNOParameter::resonance);
            const auto q = 0.5f + 0.05f * resonance;
            filter.setCutoffAndQ (cutoff, q);

            const auto scopeOutput = filter.process (mixed)
                                   * modulation.get (
                                       MNOModulationSource::adsr1)
                                   * value (MNOParameter::output);
            // Leave headroom for oscillator summing and resonant peaks without
            // shrinking the synth's own waveform display.
            constexpr auto outputHeadroom = 0.25f;
            const auto output = outputHeadroom * scopeOutput;

            blockModulationState = {
                lfo1Phase,
                modulation.get (MNOModulationSource::lfo1),
                modulation.get (MNOModulationSource::adsr1),
                envelope1.isActive() ? 1 : 0,
                adsr1Display.getStage(),
                adsr1Display.getProgress(),
                velocityScale,
                lfo2Phase,
                modulation.get (MNOModulationSource::lfo2),
                modulation.get (MNOModulationSource::adsr2),
                envelope2.isActive() ? 1 : 0,
                adsr2Display.getStage(),
                adsr2Display.getProgress(),
                first.shape,
                second.shape,
                cutoff,
                resonance
            };

                for (uint32_t channel = 0;
                     channel < outputBuffer.channel_count; ++channel)
                {
                    if (outputBuffer.data32 != nullptr
                        && outputBuffer.data32[channel] != nullptr)
                        outputBuffer.data32[channel][frame] = output;
                    if (outputBuffer.data64 != nullptr
                        && outputBuffer.data64[channel] != nullptr)
                        outputBuffer.data64[channel][frame] = output;
                }

            const auto useOscillator2ForScope =
                value (MNOParameter::osc2Tune)
                    < value (MNOParameter::osc1Tune);
            if (useOscillator2ForScope != scopeUsesOscillator2)
            {
                resetScopeTrigger();
                scopeUsesOscillator2 = useOscillator2ForScope;
            }

            const auto& scopeOscillatorOutput =
                scopeUsesOscillator2 ? second : first;
            const auto scopeCyclePhase = scopeUsesOscillator2
                ? oscillator2.scopeCyclePhase()
                : oscillator1.scopeCyclePhase();
            float scopeTrigger = 0.0f;
            if (scopeOscillatorOutput.trigger > 0.5f
                && scopeCyclePhase == 0)
                scopeTrigger = 1.0f;

                if (scopeRing != nullptr)
                    scopeRing->push (
                        scopeOutput,
                        delayScopeTrigger (
                            scopeTrigger,
                            scopeUsesOscillator2
                                ? oscillator2.scopeLatencySamples()
                                : oscillator1.scopeLatencySamples()));
            }
        });
        latestModulationState.store (blockModulationState);
        return envelope1.isActive() || envelope2.isActive()
            ? CLAP_PROCESS_CONTINUE
            : CLAP_PROCESS_SLEEP;
    }

    void flushParameters (const clap_input_events_t* events,
                          uint16_t parameterRampSpaceId,
                          bool active) noexcept;

    [[nodiscard]] mno::ParameterRenderState&
    parameter (std::size_t index) noexcept { return parameters[index]; }
    [[nodiscard]] const mno::ParameterRenderState&
    parameter (std::size_t index) const noexcept { return parameters[index]; }

    [[nodiscard]] static const std::array<
        mno::ParameterDefinition, parameterCount>&
    parameterDefinitions() noexcept;
    [[nodiscard]] static int parameterIndex (clap_id id) noexcept;

    [[nodiscard]] uint32_t latencySamples() const noexcept { return 0; }
    [[nodiscard]] uint32_t tailSamples() const noexcept
    {
        const auto releaseIndex =
            static_cast<std::size_t> (MNOParameter::release);
        return static_cast<uint32_t> (
            std::ceil (
                parameters[releaseIndex].baseValueForMainThread()
                * sampleRate));
    }

    float getParameterValue (size_t index) const noexcept
    {
        return index < static_cast<size_t> (MNOParameter::count)
            ? static_cast<float> (
                parameters[index].baseValueForMainThread())
            : 0.0f;
    }
    MNOModulationState getModulationState() const noexcept
    {
        return latestModulationState.load();
    }
    int copyScope(float* audio, int capacity) const noexcept
    {
        return scopeRing != nullptr
            ? scopeRing->copyTriggered(audio, capacity) : 0;
    }

private:
    void handleEvent (const clap_event_header_t& event,
                      uint16_t parameterRampSpaceId) noexcept;

    bool rememberEnvelopeParameter (MNOParameter parameter,
                                    float next) noexcept
    {
        const auto index = static_cast<std::size_t> (parameter);
        if (envelopeParameterApplied[index]
            && envelopeParameterValues[index] == next)
            return false;

        envelopeParameterValues[index] = next;
        envelopeParameterApplied[index] = true;
        return true;
    }

    float value (MNOParameter parameter) const noexcept
    {
        return static_cast<float> (
            parameters[static_cast<std::size_t> (parameter)]
                .currentGlobalValue());
    }

    void applyAction (const MonoController::Action& action) noexcept
    {
        if (action.hasPitch())
        {
            portamento.setTarget (action.pitch, action.glide);
            if (action.trigger)
            {
                const auto wasActive =
                    envelope1.isActive() || envelope2.isActive();
                if (wasActive)
                {
                    noteVelocity.setTarget (
                        action.velocity,
                        uint32_t (sampleRate * 0.005f));
                    envelope1.retrigger();
                    envelope2.retrigger();
                }
                else
                {
                    noteVelocity.reset (action.velocity);
                    envelope1.trigger();
                    envelope2.trigger();
                }
                adsr1Display.trigger();
                adsr2Display.trigger();
                resetScopeTrigger();
            }
            else
            {
                noteVelocity.setTarget (
                    action.velocity,
                    uint32_t (sampleRate * 0.005f));
            }
        }
        else if (action.shouldRelease())
        {
            envelope1.release();
            envelope2.release();
            adsr1Display.release();
            adsr2Display.release();
        }
    }

    void applyAllParameters() noexcept
    {
        for (size_t index = 0;
             index < static_cast<size_t> (MNOParameter::count); ++index)
            applyParameter (static_cast<MNOParameter> (index));
    }

    void applyParameter (MNOParameter parameter) noexcept
    {
        switch (parameter)
        {
            case MNOParameter::osc1Waveform:
                oscillator1.setWaveform (waveform (
                    value (parameter), false));
                break;
            case MNOParameter::osc1Shape:
                oscillator1.setShape (value (parameter));
                break;
            case MNOParameter::osc2Waveform:
                oscillator2.setWaveform (waveform (
                    value (parameter), true));
                break;
            case MNOParameter::osc2Shape:
                oscillator2.setShape (value (parameter));
                break;
            case MNOParameter::attack:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope1.setAttack (next);
                    adsr1Display.restartIf (
                        MNOADSRDisplayTracker::Stage::attack);
                }
                break;
            }
            case MNOParameter::decay:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope1.setDecay (next);
                    adsr1Display.restartIf (
                        MNOADSRDisplayTracker::Stage::decay);
                }
                break;
            }
            case MNOParameter::sustain:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope1.setSustain (next);
                    adsr1Display.restartIf (
                        MNOADSRDisplayTracker::Stage::decay);
                }
                break;
            }
            case MNOParameter::release:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope1.setRelease (next);
                    adsr1Display.restartIf (
                        MNOADSRDisplayTracker::Stage::release);
                }
                break;
            }
            case MNOParameter::adsr2Attack:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope2.setAttack (next);
                    adsr2Display.restartIf (
                        MNOADSRDisplayTracker::Stage::attack);
                }
                break;
            }
            case MNOParameter::adsr2Decay:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope2.setDecay (next);
                    adsr2Display.restartIf (
                        MNOADSRDisplayTracker::Stage::decay);
                }
                break;
            }
            case MNOParameter::adsr2Sustain:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope2.setSustain (next);
                    adsr2Display.restartIf (
                        MNOADSRDisplayTracker::Stage::decay);
                }
                break;
            }
            case MNOParameter::adsr2Release:
            {
                const auto next = value (parameter);
                if (rememberEnvelopeParameter (parameter, next))
                {
                    envelope2.setRelease (next);
                    adsr2Display.restartIf (
                        MNOADSRDisplayTracker::Stage::release);
                }
                break;
            }
            case MNOParameter::legato:
            {
                const auto enabled = value (parameter) >= 0.5f;
                controller.setLegato (enabled);
                controller.setPortamentoMode (
                    enabled ? chardsp::PortamentoMode::fingered
                            : chardsp::PortamentoMode::off);
                break;
            }
            case MNOParameter::glideTime:
                portamento.setTime (value (parameter));
                break;
            case MNOParameter::glideShape:
                portamento.setShape (value (parameter));
                break;
            case MNOParameter::glideTimeRateMorph:
                portamento.setTimeRateMorph (value (parameter));
                break;
            default:
                break;
        }
    }

    static MNOWaveform waveform (float value, bool allowsNoise) noexcept
    {
        const auto choice = std::clamp (
            int (std::round (value)), 0, allowsNoise ? 5 : 4);
        switch (choice)
        {
            case 1: return MNOWaveform::ramp;
            case 2: return MNOWaveform::pulse;
            case 3: return MNOWaveform::triangle;
            case 4: return MNOWaveform::sine;
            case 5: return MNOWaveform::noise;
            default: return MNOWaveform::saw;
        }
    }

    static float midiToFrequency (float note) noexcept
    {
        return 440.0f * std::exp2 ((note - 69.0f) / 12.0f);
    }

    static float centsRatio (float cents) noexcept
    {
        return std::exp2 (cents / 1200.0f);
    }

    static float lfoSample (float phase, float waveform) noexcept
    {
        switch (std::clamp (int (std::round (waveform)), 0, 2))
        {
            case 1:
                return 1.0f - 4.0f * std::abs (phase - 0.5f);
            case 2:
                return phase < 0.5f ? 1.0f : -1.0f;
            default:
                return std::sin (phase * 6.283185307179586f);
        }
    }

    void resetScopeTrigger() noexcept
    {
        scopeTriggerDelay.fill (0.0f);
        scopeTriggerWrite = 0;
    }

    float delayScopeTrigger (float input, int delaySamples) noexcept
    {
        const auto delay = std::clamp (
            delaySamples, 0, int (scopeTriggerDelay.size()) - 1);
        scopeTriggerDelay[scopeTriggerWrite] = input;
        const auto read = (
            scopeTriggerWrite + scopeTriggerDelay.size()
            - size_t (delay)) % scopeTriggerDelay.size();
        const auto output = scopeTriggerDelay[read];
        scopeTriggerWrite =
            (scopeTriggerWrite + 1) % scopeTriggerDelay.size();
        return output;
    }

    MNOOscillator oscillator1 { 12643383u };
    MNOOscillator oscillator2 { 982451653u };
    MonoController controller;
    chardsp::Portamento<float> portamento;
    chardsp::ADSREnvelope<float> envelope1;
    chardsp::ADSREnvelope<float> envelope2;
    chardsp::StateVariableFilter<float> filter;
    chardsp::SmoothedValue<float> noteVelocity;
    std::array<
        mno::ParameterRenderState,
        parameterCount
    > parameters {};
    std::array<float, parameterCount> envelopeParameterValues {};
    std::array<bool, parameterCount> envelopeParameterApplied {};
    uint32_t maximumFrameCount = 0;
    float sampleRate = 48000.0f;
    float pitchBend = 0.0f;
    float lfo1Phase = 0.0f;
    float lfo2Phase = 0.0f;
    float osc1PWMPhase = 0.0f;
    float osc2PWMPhase = 0.0f;
    MNOADSRDisplayTracker adsr1Display;
    MNOADSRDisplayTracker adsr2Display;
    MNOModulationSnapshot latestModulationState;
    std::array<float, 32> scopeTriggerDelay {};
    size_t scopeTriggerWrite = 0;
    bool scopeUsesOscillator2 = false;
    MNOScopeRing scopeRingStorage;
    MNOScopeRing* scopeRing = &scopeRingStorage;
};

} // namespace mno

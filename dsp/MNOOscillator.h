#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include "MNOPlatform.h"

namespace mno
{

enum class MNOWaveform : int
{
    saw,
    ramp,
    pulse,
    triangle,
    sine,
    noise
};

struct MNOOscillatorOutput
{
    float sample = 0.0f;
    float shape = 0.0f;
    float trigger = 0.0f;
    float wrapOffset = 0.0f;
};

// App-level oscillator. Waveform policy stays here while phase timing,
// Elliptic correction, smoothing, and downsampling come from Chardio.
class MNOOscillator
{
public:
    explicit MNOOscillator (uint32_t seed = 1) : initialSeed (seed) {}

    void prepare (float newSampleRate)
    {
        sampleRate = newSampleRate > 0.0f ? newSampleRate : 48000.0f;
        phase.prepare (sampleRate);
        baseCorrection.prepare (sampleRate);
        oversampledCorrection.prepare (sampleRate * 4.0f);
    }

    void reset() noexcept
    {
        shapeSmoother.reset (0.0f);
        shape = 0.0f;
        previousPulseWidth = pulseWidth();
        renderPulseWidth = previousPulseWidth;
        noiseSeed = initialSeed;
        noiseBrown = 0.0f;
        noisePink0 = 0.0f;
        noisePink1 = 0.0f;
        noisePink2 = 0.0f;
        previousWhite = 0.0f;
        randomisePhase();
        randomiseMicroDrift();
        downsampler.reset();
        wasOversampled = false;
    }

    void setWaveform (MNOWaveform next) noexcept
    {
        if (waveform != next)
        {
            waveform = next;
            baseCorrection.reset();
            oversampledCorrection.reset();
            downsampler.reset();
        }
        previousPulseWidth = pulseWidth();
        renderPulseWidth = previousPulseWidth;
        heldSawShape = sawFamilyShape();
    }

    void setShape (float next) noexcept
    {
        shapeSmoother.setTarget (
            std::clamp (next, 0.0f, 1.0f),
            static_cast<uint32_t> (std::max (1.0f, sampleRate * 0.05f)));
    }

    void resetPhase() noexcept
    {
        randomisePhase();
    }

    // The phase trigger describes the oscillator's uncorrected wrap time.
    // Return the measured audio-rate delay to the corresponding rendered
    // landmark after elliptic correction and, where used, 4x downsampling.
    int scopeLatencySamples() const noexcept
    {
        switch (waveform)
        {
            case MNOWaveform::saw:
            case MNOWaveform::ramp:
                return 13;
            case MNOWaveform::pulse:
                return 8;
            case MNOWaveform::triangle:
                return 7;
            case MNOWaveform::sine:
                return wasOversampled ? 5 : 0;
            case MNOWaveform::noise:
                return 2;
        }
        return 0;
    }

    // Folded-saw polarity alternates each cycle; scope on a fixed two-cycle phase.
    int scopeCyclePhase() const noexcept { return cycleIndex & 1; }

    MNOOscillatorOutput next (
        float frequency,
        std::optional<float> syncOffset = std::nullopt,
        float shapeModulation = 0.0f) noexcept
    {
        shape = std::clamp (
            shapeSmoother.next() + shapeModulation, 0.0f, 1.0f);
        const auto driftedFrequency = std::clamp (
            frequency * updateMicroDriftRatio(), 0.0f, 24000.0f);
        const auto dt = driftedFrequency / sampleRate;
        const auto oversampled = shouldOversample (syncOffset.has_value());
        const auto nextPulseWidth = pulseWidth();

        if (oversampled != wasOversampled)
        {
            baseCorrection.reset();
            oversampledCorrection.reset();
            downsampler.reset();
            wasOversampled = oversampled;
        }

        if (! oversampled)
        {
            previousPulseWidth = nextPulseWidth;
            renderPulseWidth = nextPulseWidth;
            return processBaseSample (dt, syncOffset);
        }

        std::array<float, 4> samples {};
        bool wrapped = false;
        float firstWrapOffset = 0.0f;

        for (int sub = 0; sub < 4; ++sub)
        {
            const auto startAmount = float (sub) * 0.25f;
            const auto endAmount = float (sub + 1) * 0.25f;
            const auto pulseWidthStart = mix (
                previousPulseWidth, nextPulseWidth, startAmount);
            const auto pulseWidthEnd = mix (
                previousPulseWidth, nextPulseWidth, endAmount);
            std::optional<float> localSync;
            if (syncOffset)
            {
                const auto position = std::clamp (*syncOffset, 0.0f, 1.0f)
                                    * 4.0f;
                const auto syncSub = std::min (3, static_cast<int> (position));
                if (syncSub == sub)
                    localSync = std::min (1.0f, position - float (syncSub));
            }

            const auto result = processOversampledSubsample (
                dt * 0.25f, localSync,
                pulseWidthStart, pulseWidthEnd);
            samples[static_cast<size_t> (sub)] = result.sample;

            if (! wrapped && result.trigger > 0.5f)
            {
                wrapped = true;
                firstWrapOffset =
                    (float (sub) + result.wrapOffset) * 0.25f;
            }
        }

        previousPulseWidth = nextPulseWidth;

        return {
            downsampler.process (samples),
            currentShapeDebug(),
            wrapped ? 1.0f : 0.0f,
            firstWrapOffset
        };
    }

private:
    struct Subsample
    {
        float sample = 0.0f;
        float trigger = 0.0f;
        float wrapOffset = 0.0f;
    };

    bool isSawFamily() const noexcept
    {
        return waveform == MNOWaveform::saw
            || waveform == MNOWaveform::ramp;
    }

    bool shouldOversample (bool isSynced) const noexcept
    {
        if (isSawFamily())
            return false;
        return waveform != MNOWaveform::sine
            || shape > 0.000001f || isSynced;
    }

    MNOOscillatorOutput processBaseSample (
        float dt, std::optional<float> syncOffset) noexcept
    {
        if (waveform == MNOWaveform::sine && ! syncOffset)
        {
            const auto advance = phase.advanceBy (dt);
            return {
                renderSine (advance.phase),
                shape,
                advance.wrapped ? 1.0f : 0.0f,
                advance.wrapOffset
            };
        }

        baseCorrection.beginSample();
        const auto advance = advanceCorrected (
            dt, syncOffset, baseCorrection,
            renderPulseWidth, renderPulseWidth);
        const auto raw = renderSaw (
            phase.phase, heldSawShape, cycleIndex);
        return {
            baseCorrection.process (raw),
            currentShapeDebug(),
            advance.wrapped ? 1.0f : 0.0f,
            advance.wrapOffset
        };
    }

    Subsample processOversampledSubsample (
        float dt, std::optional<float> syncOffset,
        float pulseWidthStart, float pulseWidthEnd) noexcept
    {
        oversampledCorrection.beginSample();
        const auto advance = advanceCorrected (
            dt, syncOffset, oversampledCorrection,
            pulseWidthStart, pulseWidthEnd);
        const auto raw = waveform == MNOWaveform::noise
            ? renderNoise() : renderRaw (phase.phase);
        return {
            waveform == MNOWaveform::noise
                ? raw : oversampledCorrection.process (raw),
            advance.wrapped ? 1.0f : 0.0f,
            advance.wrapOffset
        };
    }

    chardsp::OscillatorPhaseAdvance<float> advanceCorrected (
        float dt, std::optional<float> syncOffset,
        chardsp::EllipticBLEPCorrection<float>& correction,
        float pulseWidthStart, float pulseWidthEnd) noexcept
    {
        const auto start = phase.phase;

        if (! syncOffset)
        {
            scheduleNaturalEvents (
                start, start + dt, 0.0f, dt, correction,
                pulseWidthStart, pulseWidthEnd);
            const auto advance = phase.advanceBy (dt);
            renderPulseWidth = pulseWidthEnd;
            if (advance.wrapped)
            {
                ++cycleIndex;
                heldSawShape = sawFamilyShape();
            }
            return advance;
        }

        const auto offset = std::clamp (*syncOffset, 0.0f, 1.0f);
        const auto beforeDistance = dt * offset;
        const auto beforeUnwrapped = start + beforeDistance;
        const auto pulseWidthAtReset = mix (
            pulseWidthStart, pulseWidthEnd, offset);
        scheduleNaturalEvents (
            start, beforeUnwrapped, 0.0f, dt, correction,
            pulseWidthStart, pulseWidthAtReset);

        const auto phaseAtReset =
            beforeUnwrapped - std::floor (beforeUnwrapped);
        const auto cycleAtReset =
            cycleIndex + (beforeUnwrapped >= 1.0f ? 1 : 0);
        const auto before = renderRawAt (
            phaseAtReset, heldSawShape, cycleAtReset,
            pulseWidthAtReset);
        const auto nextCycle = cycleAtReset + 1;
        const auto after = renderRawAt (
            0.0f, sawFamilyShape(), nextCycle,
            pulseWidthAtReset);
        correction.addEvent (
            offset, after - before,
            (slopeAt (0.0f, sawFamilyShape(), nextCycle)
             - slopeAt (phaseAtReset, heldSawShape, cycleAtReset)) * dt);

        const auto afterDistance = dt * (1.0f - offset);
        scheduleNaturalEvents (
            0.0f, afterDistance, offset, dt, correction,
            pulseWidthAtReset, pulseWidthEnd);
        phase.reset (afterDistance);
        renderPulseWidth = pulseWidthEnd;
        cycleIndex = nextCycle + (afterDistance >= 1.0f ? 1 : 0);
        heldSawShape = sawFamilyShape();

        chardsp::OscillatorPhaseAdvance<float> result;
        result.startPhase = start;
        result.phase = phase.phase;
        result.increment = dt;
        result.wrapped = true;
        result.wrapOffset = offset;
        return result;
    }

    void scheduleNaturalEvents (
        float start, float end, float sampleOffset, float dt,
        chardsp::EllipticBLEPCorrection<float>& correction,
        float pulseWidthStart, float pulseWidthEnd) noexcept
    {
        if (dt <= 0.0f || waveform == MNOWaveform::noise)
            return;

        if (isSawFamily())
        {
            scheduleSawEvents (
                start, end, heldSawShape, cycleIndex,
                sampleOffset, dt, correction);
            return;
        }

        if (waveform == MNOWaveform::pulse)
        {
            chardsp::forEachMovingPulseEdge (
                start, end,
                pulseWidthStart, pulseWidthEnd,
                sampleOffset,
                sampleOffset + (end - start) / dt,
                [&] (chardsp::PulseEdgeEvent<float> event)
                {
                    correction.addStep (event.offset, event.step);
                });
            return;
        }

        if (waveform == MNOWaveform::triangle)
        {
            scheduleSlopeAtPhase (
                start, end, 0.5f, -8.0f * dt,
                sampleOffset, dt, correction);
            if (end >= 1.0f)
                correction.addSlopeJump (
                    sampleOffset + (1.0f - start) / dt,
                    8.0f * dt);
        }
    }

    static void scheduleSlopeAtPhase (
        float start, float end, float point, float jump,
        float sampleOffset, float dt,
        chardsp::EllipticBLEPCorrection<float>& correction) noexcept
    {
        if (point > start && point <= end)
            correction.addSlopeJump (
                sampleOffset + (point - start) / dt, jump);
    }

    void scheduleSawEvents (
        float start, float end, float sawShape, int renderCycle,
        float sampleOffset, float dt,
        chardsp::EllipticBLEPCorrection<float>& correction) noexcept
    {
        const auto scheduleFold = [&] (
            float segmentStart, float segmentEnd, float offset,
            float segmentShape, int segmentCycle)
        {
            const auto tau = sawTau (segmentShape);
            if (tau >= 0.999999f)
                return;
            const auto baseSign = sawBaseSign (segmentShape);
            const auto polarity = sawPolarity (segmentCycle, segmentShape);
            const auto point =
                0.5f * (1.0f + tau * polarity * baseSign);
            if (point > segmentStart && point <= segmentEnd)
            {
                const auto eventOffset =
                    offset + (point - segmentStart) / dt;
                const auto step =
                    4.0f * baseSign * tau / (1.0f + tau);
                const auto epsilon = 0.00001f;
                const auto slopeBefore = sawSlope (
                    std::max (0.0f, point - epsilon),
                    segmentShape, segmentCycle);
                const auto slopeAfter = sawSlope (
                    std::min (1.0f, point + epsilon),
                    segmentShape, segmentCycle);
                correction.addEvent (
                    eventOffset, step,
                    (slopeAfter - slopeBefore) * dt);
            }
        };

        if (end < 1.0f)
        {
            scheduleFold (
                start, end, sampleOffset, sawShape, renderCycle);
            return;
        }

        scheduleFold (
            start, 1.0f, sampleOffset, sawShape, renderCycle);
        const auto wrapOffset =
            sampleOffset + (1.0f - start) / dt;
        const auto nextShape = sawFamilyShape();
        const auto nextCycle = renderCycle + 1;
        const auto before = renderSaw (1.0f, sawShape, renderCycle);
        const auto after = renderSaw (0.0f, nextShape, nextCycle);
        const auto slopeBefore = sawSlope (
            1.0f - 0.00001f, sawShape, renderCycle);
        const auto slopeAfter = sawSlope (
            0.00001f, nextShape, nextCycle);
        correction.addEvent (
            wrapOffset, after - before,
            (slopeAfter - slopeBefore) * dt);
        scheduleFold (
            0.0f, end - 1.0f, wrapOffset, nextShape, nextCycle);
    }

    float renderRaw (float p) noexcept
    {
        return renderRawAt (
            p, heldSawShape, cycleIndex, renderPulseWidth);
    }

    float renderRawAt (
        float p, float sawShape, int renderCycle,
        float pulseWidthValue) noexcept
    {
        switch (waveform)
        {
            case MNOWaveform::saw:
            case MNOWaveform::ramp:
                return renderSaw (p, sawShape, renderCycle);
            case MNOWaveform::pulse:
                return p < pulseWidthValue ? 1.0f : -1.0f;
            case MNOWaveform::triangle:
                return mirrorFold (rawTriangle (p) * foldDrive());
            case MNOWaveform::sine:
                return renderSine (p);
            case MNOWaveform::noise:
                return 0.0f;
        }
        return 0.0f;
    }

    float slopeAt (float p, float sawShape, int renderCycle) noexcept
    {
        if (isSawFamily())
            return sawSlope (p, sawShape, renderCycle);
        if (waveform == MNOWaveform::pulse)
            return 0.0f;
        const auto epsilon = 0.00001f;
        const auto before = renderRawAt (
            chardsp::OscillatorPhase<float>::wrap (p - epsilon),
            sawShape, renderCycle, renderPulseWidth);
        const auto after = renderRawAt (
            chardsp::OscillatorPhase<float>::wrap (p + epsilon),
            sawShape, renderCycle, renderPulseWidth);
        return (after - before) / (2.0f * epsilon);
    }

    float sawFamilyShape() const noexcept
    {
        return waveform == MNOWaveform::saw
            ? 2.0f + shape * 2.0f : shape * 2.0f;
    }

    float pulseWidth() const noexcept
    {
        return 0.02f + shape * 0.96f;
    }

    float foldDrive() const noexcept { return 1.0f + shape * 3.0f; }

    float currentShapeDebug() const noexcept
    {
        if (isSawFamily())
            return sawFamilyShape();
        if (waveform == MNOWaveform::pulse)
            return pulseWidth();
        return shape;
    }

    static float rawTriangle (float p) noexcept
    {
        return p < 0.5f ? p * 4.0f - 1.0f : 3.0f - p * 4.0f;
    }

    float renderSine (float p) const noexcept
    {
        return mirrorFold (
            std::sin (p * 6.283185307179586f) * foldDrive());
    }

    static float mirrorFold (float value) noexcept
    {
        while (value > 1.0f || value < -1.0f)
            value = value > 1.0f ? 2.0f - value : -2.0f - value;
        return value;
    }

    static float sawFoldDepth (float value) noexcept
    {
        if (value < 1.0f) return value;
        if (value < 2.0f) return 2.0f - value;
        if (value < 3.0f) return value - 2.0f;
        return 4.0f - value;
    }

    static float sawTau (float value) noexcept
    {
        return 1.0f - sawFoldDepth (value);
    }

    static float sawBaseSign (float value) noexcept
    {
        return value < 1.0f || value >= 3.0f ? 1.0f : -1.0f;
    }

    static float sawPolarity (int cycle, float value) noexcept
    {
        const auto cyclePolarity = (cycle & 1) == 0 ? -1.0f : 1.0f;
        return value < 2.0f ? cyclePolarity : -cyclePolarity;
    }

    static float renderSaw (float p, float value, int cycle) noexcept
    {
        const auto baseSign = sawBaseSign (value);
        const auto saw = baseSign * (1.0f - 2.0f * p);
        const auto tau = sawTau (value);
        if (tau >= 0.999999f)
            return saw;
        const auto polarity = sawPolarity (cycle, value);
        const auto folded = polarity * saw < -tau ? -saw : saw;
        return (2.0f * folded - polarity * (1.0f - tau))
             / (1.0f + tau);
    }

    static float sawSlope (float p, float value, int cycle) noexcept
    {
        const auto tau = sawTau (value);
        const auto baseSign = sawBaseSign (value);
        const auto saw = baseSign * (1.0f - 2.0f * p);
        const auto rawSlope = -2.0f * baseSign;
        if (tau >= 0.999999f)
            return rawSlope;
        const auto polarity = sawPolarity (cycle, value);
        const auto foldedSlope =
            polarity * saw < -tau ? -rawSlope : rawSlope;
        return 2.0f * foldedSlope / (1.0f + tau);
    }

    float renderNoise() noexcept
    {
        const auto white = nextRandomBipolar();
        noiseBrown += 0.018f * white - 0.0012f * noiseBrown;
        noiseBrown = std::clamp (noiseBrown, -1.25f, 1.25f);
        noisePink0 = 0.99765f * noisePink0 + 0.099046f * white;
        noisePink1 = 0.963f * noisePink1 + 0.2965164f * white;
        noisePink2 = 0.57f * noisePink2 + 1.0526913f * white;
        const auto brown = std::clamp (noiseBrown * 0.95f, -1.0f, 1.0f);
        const auto pink = std::clamp (
            (noisePink0 + noisePink1 + noisePink2 + 0.1848f * white)
                * 0.21f, -1.0f, 1.0f);
        const auto blue = std::clamp (
            (white - previousWhite) * 0.95f, -1.0f, 1.0f);
        previousWhite = white;
        const auto color = shape * 3.0f;
        if (color < 1.0f)
            return mix (brown, pink, smoothstep (color));
        if (color < 2.0f)
            return mix (pink, white, smoothstep (color - 1.0f));
        return mix (white, blue, smoothstep (color - 2.0f));
    }

    static float mix (float a, float b, float amount) noexcept
    {
        return a + (b - a) * amount;
    }

    static float smoothstep (float value) noexcept
    {
        const auto x = std::clamp (value, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    float nextRandomBipolar() noexcept
    {
        noiseSeed = 1664525u * noiseSeed + 1013904223u;
        const auto signedSeed = noiseSeed <= 0x7fffffffu
            ? int64_t (noiseSeed)
            : int64_t (noiseSeed) - int64_t { 0x100000000 };
        return 4.656613e-10f * float (signedSeed);
    }

    float nextRandomUnipolar() noexcept
    {
        return 0.5f + 0.5f * nextRandomBipolar();
    }

    void randomisePhase() noexcept
    {
        phase.reset (nextRandomUnipolar());
        cycleIndex = int (std::floor (nextRandomUnipolar() * 1024.0f));
        heldSawShape = sawFamilyShape();
        baseCorrection.reset();
        oversampledCorrection.reset();
        downsampler.reset();
    }

    void randomiseMicroDrift() noexcept
    {
        movingDrift = microDriftCents * nextRandomBipolar();
        chooseDriftTarget();
    }

    void chooseDriftTarget() noexcept
    {
        targetDrift = microDriftCents * nextRandomBipolar();
        driftSamples = int (
            (30.0f + 60.0f * nextRandomUnipolar()) * sampleRate);
        driftSlew = 1.0f / std::max (
            1.0f, (20.0f + 25.0f * nextRandomUnipolar()) * sampleRate);
    }

    float updateMicroDriftRatio() noexcept
    {
        if (--driftSamples <= 0)
            chooseDriftTarget();
        movingDrift += (targetDrift - movingDrift) * driftSlew;
        return 1.0f + movingDrift * 0.00057762265f;
    }

    static constexpr float microDriftCents = 0.04f;
    uint32_t initialSeed = 1;
    uint32_t noiseSeed = 1;
    float sampleRate = 48000.0f;
    float shape = 0.0f;
    float heldSawShape = 0.0f;
    float previousPulseWidth = 0.02f;
    float renderPulseWidth = 0.02f;
    float movingDrift = 0.0f;
    float targetDrift = 0.0f;
    float driftSlew = 0.0f;
    int driftSamples = 1;
    int cycleIndex = 0;
    float noiseBrown = 0.0f;
    float noisePink0 = 0.0f;
    float noisePink1 = 0.0f;
    float noisePink2 = 0.0f;
    float previousWhite = 0.0f;
    bool wasOversampled = false;
    MNOWaveform waveform = MNOWaveform::saw;
    chardsp::OscillatorPhase<float> phase;
    chardsp::EllipticBLEPCorrection<float> baseCorrection;
    chardsp::EllipticBLEPCorrection<float> oversampledCorrection;
    chardsp::SimpleDownsampler<float, 4> downsampler;
    chardsp::SmoothedValue<float> shapeSmoother;
};

} // namespace mno

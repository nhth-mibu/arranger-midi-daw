#include "arranger/engine/Transport.h"

#include <cmath>

namespace arranger
{

std::int64_t ProcessContext::sampleForTick (double tick) const noexcept
{
    if (ticksPerSample <= 0.0)
        return anchorSample;

    // The small epsilon keeps a click that lands exactly on a sample from
    // slipping one sample late because of floating point noise.
    const double samplesFromAnchor = (tick - anchorTick) / ticksPerSample;
    return anchorSample + static_cast<std::int64_t> (std::ceil (samplesFromAnchor - 1.0e-7));
}

void Transport::prepare (double newSampleRate) noexcept
{
    if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
        return;

    // Keep the song position, re-anchor at the new rate.
    const double now = positionTicks();
    sampleRate = newSampleRate;
    anchorSample = sampleTime;
    anchorTick = now;
}

double Transport::ticksPerSample() const noexcept
{
    return tempoBpm * static_cast<double> (kTicksPerQuarter) / (60.0 * sampleRate);
}

double Transport::positionTicks() const noexcept
{
    if (! playing)
        return anchorTick;
    return anchorTick + static_cast<double> (sampleTime - anchorSample) * ticksPerSample();
}

void Transport::play() noexcept
{
    if (playing)
        return;
    sampleTime = 0;
    anchorSample = 0;
    playing = true;
}

void Transport::stop() noexcept
{
    if (! playing)
        return;
    anchorTick = positionTicks();
    playing = false;
}

void Transport::locate (double tick) noexcept
{
    anchorTick = tick;
    anchorSample = sampleTime;
}

void Transport::setTempo (double bpm) noexcept
{
    bpm = clampTempo (bpm);
    if (bpm == tempoBpm)
        return;
    const double now = positionTicks();
    tempoBpm = bpm;
    anchorTick = now;
    anchorSample = sampleTime;
}

void Transport::setTimeSignature (TimeSignature newSig) noexcept
{
    if (newSig.isValid())
        sig = newSig;
}

ProcessContext Transport::contextForBlock (int numSamples) const noexcept
{
    ProcessContext ctx;
    ctx.playing = playing;
    ctx.sampleTime = sampleTime;
    ctx.numSamples = numSamples;
    ctx.tempoBpm = tempoBpm;
    ctx.timeSignature = sig;
    ctx.anchorSample = anchorSample;
    ctx.anchorTick = anchorTick;
    ctx.ticksPerSample = playing ? ticksPerSample() : 0.0;
    ctx.tickAtStart = positionTicks();
    return ctx;
}

void Transport::advance (int numSamples) noexcept
{
    if (playing)
        sampleTime += numSamples;
}

} // namespace arranger

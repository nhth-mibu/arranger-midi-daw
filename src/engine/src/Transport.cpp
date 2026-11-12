#include "arranger/engine/Transport.h"

#include <cmath>

namespace arranger
{

std::int64_t ProcessContext::sampleForTick (double tick) const noexcept
{
    if (tempo == nullptr)
        return anchorSample;
    // The small epsilon keeps an event that lands exactly on a sample from
    // slipping one sample late because of floating point noise.
    const double samples = (tempo->secondsAt (tick) - anchorSeconds) * sampleRate;
    return anchorSample + static_cast<std::int64_t> (std::ceil (samples - 1.0e-6));
}

double ProcessContext::tickAtSample (std::int64_t sample) const noexcept
{
    if (tempo == nullptr || ! playing)
        return anchorTick;
    return tempo->tickAtSeconds (anchorSeconds + static_cast<double> (sample - anchorSample) / sampleRate);
}

Transport::Transport() = default;

void Transport::reanchor (double tick) noexcept
{
    anchorTick = tick;
    anchorSample = sampleTime;
    anchorSeconds = tempo->secondsAt (tick);
}

void Transport::prepare (double newRate) noexcept
{
    if (newRate <= 0.0 || newRate == rate)
        return;
    const double now = positionTicks();
    rate = newRate;
    reanchor (now);
}

double Transport::positionTicks() const noexcept
{
    if (! playing)
        return anchorTick;
    return tempo->tickAtSeconds (anchorSeconds + static_cast<double> (sampleTime - anchorSample) / rate);
}

double Transport::tempoNow() const noexcept
{
    return tempo->tempoAt (positionTicks());
}

void Transport::play() noexcept
{
    if (playing)
        return;
    sampleTime = 0;
    reanchor (anchorTick);
    playing = true;
}

void Transport::stop() noexcept
{
    if (! playing)
        return;
    const double now = positionTicks();
    playing = false;
    reanchor (now);
}

void Transport::locate (double tick) noexcept
{
    reanchor (tick);
}

void Transport::setTempoMap (const TempoMap* map) noexcept
{
    const double now = positionTicks();
    tempo = map != nullptr ? map : &fallbackTempo;
    reanchor (now);
}

void Transport::setTimeSignature (TimeSignature newSig) noexcept
{
    if (newSig.isValid())
        sig = newSig;
}

ProcessContext Transport::contextFor (int numSamples, int blockOffset) const noexcept
{
    ProcessContext ctx;
    ctx.playing = playing;
    ctx.sampleTime = sampleTime;
    ctx.numSamples = numSamples;
    ctx.blockOffset = blockOffset;
    ctx.sampleRate = rate;
    ctx.timeSignature = sig;
    ctx.tempo = tempo;
    ctx.anchorSample = anchorSample;
    ctx.anchorTick = anchorTick;
    ctx.anchorSeconds = anchorSeconds;
    ctx.tickAtStart = positionTicks();
    ctx.tickAtEnd = playing ? ctx.tickAtSample (sampleTime + numSamples) : ctx.tickAtStart;
    ctx.tempoBpm = tempo->tempoAt (ctx.tickAtStart);
    ctx.ticksPerSample = playing ? ctx.tempoBpm * static_cast<double> (kTicksPerQuarter) / (60.0 * rate) : 0.0;
    return ctx;
}

void Transport::advance (int numSamples) noexcept
{
    if (playing)
        sampleTime += numSamples;
}

} // namespace arranger

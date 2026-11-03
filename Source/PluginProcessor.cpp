#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

NzDrumProcessor::NzDrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void NzDrumProcessor::prepareToPlay (double sampleRate, int)
{
    // prepareToPlay corre fuera del hilo de audio: acá sí se puede calcular,
    // reservar memoria, etc. Lo que dependa del sample rate se resuelve ahora.
    currentSampleRate = sampleRate;

    // Multiplicador por muestra para que la envolvente caiga a ~37% en 40 ms.
    constexpr double decaySeconds = 0.04;
    envelopeDecay = (float) std::exp (-1.0 / (decaySeconds * sampleRate));

    envelope = 0.0f;
    phase = 0.0;
}

bool NzDrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void NzDrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Reglas del hilo de audio: sin new/malloc, sin locks, sin disco, sin logs.
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            hostBpm.store (position->getBpm().orFallback (0.0), std::memory_order_relaxed);
            hostPpq.store (position->getPpqPosition().orFallback (0.0), std::memory_order_relaxed);
            hostPlaying.store (position->getIsPlaying(), std::memory_order_relaxed);
        }
    }

    // Cada evento MIDI trae su posición dentro del bloque (samplePosition).
    // Renderizamos hasta ese punto, disparamos, y seguimos: así la nota suena
    // en la muestra exacta y no redondeada al inicio del bloque.
    const int numSamples = buffer.getNumSamples();
    int cursor = 0;

    for (const auto metadata : midi)
    {
        const int eventPosition = juce::jlimit (0, numSamples, metadata.samplePosition);
        renderBlip (buffer, cursor, eventPosition);
        cursor = eventPosition;

        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            triggerBlip (message);
    }

    renderBlip (buffer, cursor, numSamples);
}

void NzDrumProcessor::triggerBlip (const juce::MidiMessage& noteOn)
{
    const double frequency = juce::MidiMessage::getMidiNoteInHertz (noteOn.getNoteNumber());
    phaseDelta = juce::MathConstants<double>::twoPi * frequency / currentSampleRate;
    phase = 0.0;
    envelope = 1.0f;
    velocityGain = noteOn.getFloatVelocity();
}

void NzDrumProcessor::renderBlip (juce::AudioBuffer<float>& buffer, int startSample, int endSample)
{
    constexpr float silence = 1.0e-4f;
    constexpr float outputLevel = 0.5f;

    for (int i = startSample; i < endSample && envelope > silence; ++i)
    {
        const float sample = (float) std::sin (phase) * envelope * velocityGain * outputLevel;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addSample (channel, i, sample);

        phase += phaseDelta;
        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        envelope *= envelopeDecay;
    }
}

NzDrumProcessor::TransportSnapshot NzDrumProcessor::getTransport() const noexcept
{
    return { hostBpm.load (std::memory_order_relaxed),
             hostPpq.load (std::memory_order_relaxed),
             hostPlaying.load (std::memory_order_relaxed) };
}

juce::AudioProcessorEditor* NzDrumProcessor::createEditor()
{
    return new NzDrumEditor (*this);
}

// El host llama a esta función para crear el plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NzDrumProcessor();
}

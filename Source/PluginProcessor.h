#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

// Etapa 1: un instrumento mínimo que
//   - recibe MIDI del host y, por cada nota, toca un "blip" de prueba,
//   - lee el transporte del host (tempo, play, posición) para mostrarlo en la UI.
// Sirve para validar el toolchain y la carga en Studio Pro antes de sumar el sampler.
class NzDrumProcessor final : public juce::AudioProcessor
{
public:
    NzDrumProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Todavía no hay estado que guardar; llega en la etapa 6.
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Lo que la UI necesita saber del host. Se lee desde el hilo de UI.
    struct TransportSnapshot
    {
        double bpm = 0.0;
        double ppq = 0.0;     // posición en negras desde el inicio del tema
        bool playing = false;
    };

    TransportSnapshot getTransport() const noexcept;

private:
    void triggerBlip (const juce::MidiMessage& noteOn);
    void renderBlip (juce::AudioBuffer<float>& buffer, int startSample, int endSample);

    // Escritos por el hilo de audio, leídos por el de UI. Son atómicos para que
    // ninguno de los dos tenga que esperar al otro (nada de locks en el audio).
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false };

    // Estado de la voz de prueba. Solo lo toca el hilo de audio, así que no
    // necesita ser atómico.
    double currentSampleRate = 44100.0;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float envelope = 0.0f;
    float envelopeDecay = 0.0f;
    float velocityGain = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumProcessor)
};

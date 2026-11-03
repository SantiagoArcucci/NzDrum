#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>

#include "SampleSlot.h"

// Etapa 2: sampler de 16 pads.
//   - Cada pad carga un archivo de audio (en segundo plano) y suena con un
//     click en la UI o con una nota MIDI (pad 1 = C1, nota 36).
//   - Desde el pad se puede pasar al archivo anterior o siguiente de la misma
//     carpeta.
//   - Seguimos leyendo el transporte del host para la etapa 3.
class NzDrumProcessor final : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    static constexpr int numPads = 16;
    static constexpr int firstMidiNote = 36;   // C1 en Studio Pro

    NzDrumProcessor();
    ~NzDrumProcessor() override;

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

    // Guardar las rutas de los samples llega en la etapa 6.
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // ---- API para la UI (hilo de UI) ----

    // Pide cargar un archivo en un pad. Vuelve enseguida; la carga corre en
    // segundo plano.
    void loadSample (int pad, const juce::File& file);

    // Pasa al archivo anterior (-1) o siguiente (+1) de la carpeta del pad.
    void stepSample (int pad, int direction);

    // Dispara un pad desde la UI.
    void triggerFromUi (int pad, float velocity) noexcept;

    juce::File getPadFile (int pad) const;
    bool isLoading (int pad) const noexcept;
    uint32_t getTriggerCount (int pad) const noexcept;
    bool isSupportedFile (const juce::File& file) const;
    juce::String getSupportedWildcard() const;

    struct TransportSnapshot
    {
        double bpm = 0.0;
        double ppq = 0.0;
        bool playing = false;
    };

    TransportSnapshot getTransport() const noexcept;

private:
    // Una voz por pad: si el pad se dispara de nuevo, vuelve a empezar
    // (como en una MPC). Solo la toca el hilo de audio.
    struct PadVoice
    {
        double position = 0.0;
        double increment = 1.0;   // relación entre el sample rate del archivo y el del host
        float gain = 0.0f;
        bool playing = false;
    };

    void timerCallback() override;
    void startVoice (int pad, const LoadedSample* sample, float velocity) noexcept;
    void renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept;
    std::unique_ptr<LoadedSample> readFile (const juce::File& file);
    juce::File findNeighbour (const juce::File& current, int direction) const;
    void finishLoad (int pad, const juce::File& file, std::unique_ptr<LoadedSample> sample);

    // Formatos de audio: se registran en el constructor y después solo se
    // leen, así que se pueden usar desde el hilo de carga.
    juce::AudioFormatManager formats;

    // Un único hilo de carga: los pedidos se procesan en orden.
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withThreadName ("NzDrum loader")
                                                           .withNumberOfThreads (1) };

    std::array<SampleSlot, numPads> slots;

    // Lo que la UI muestra de cada pad. Lo comparten la UI y el hilo de carga
    // (nunca el de audio), así que acá un lock está bien.
    mutable juce::CriticalSection padFilesLock;
    std::array<juce::File, numPads> padFiles;
    std::array<std::atomic<int>, numPads> pendingLoads {};

    // Comunicación UI <-> audio sin locks.
    std::array<std::atomic<float>, numPads> uiTriggers {};       // velocity pedida por la UI, 0 = nada
    std::array<std::atomic<uint32_t>, numPads> triggerCounts {}; // para que la UI ilumine el pad

    // Estado del hilo de audio.
    double currentSampleRate = 44100.0;
    std::array<PadVoice, numPads> voices;
    std::array<const LoadedSample*, numPads> blockSamples {};

    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumProcessor)
};

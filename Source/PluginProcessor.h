#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>

#include "SampleSlot.h"

// Nivel de un paso del secuenciador. off = el paso no suena.
enum class StepLevel : uint8_t { off = 0, low, normal, accent };

// NzDrum: sampler de 16 pads con secuenciador por pasos.
//   - Cada pad carga un archivo de audio (en segundo plano) y suena con un
//     click en la UI o con una nota MIDI (pad 1 = C1, nota 36).
//   - Desde el pad se puede pasar al archivo anterior o siguiente de la misma
//     carpeta.
//   - Un secuenciador de 16 pasos por pad, con tres niveles (bajo, normal y
//     acento), sigue el play y el tempo del host con precisión de muestra.
class NzDrumProcessor final : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    static constexpr int numPads = 16;
    static constexpr int firstMidiNote = 36;   // C1 en Studio Pro
    static constexpr int numSteps = 16;        // semicorcheas; el largo variable llega en la etapa 4
    static constexpr double stepLengthPpq = 0.25;

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
    double getTailLengthSeconds() const override;

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

    // ---- Secuenciador (hilo de UI) ----
    void setStep (int pad, int step, StepLevel level) noexcept;
    StepLevel getStep (int pad, int step) const noexcept;

    // Paso que está sonando ahora, o -1 si el host está detenido.
    int getPlayingStep() const noexcept;

    // Cuánto suena cada nivel. Por ahora fijo; editable por pad más adelante.
    static float gainFor (StepLevel level) noexcept;

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

    // Un disparo dentro del bloque actual, venga del secuenciador o del MIDI.
    struct TriggerEvent
    {
        int offset = 0;     // muestra dentro del bloque
        int pad = 0;
        float velocity = 0.0f;
    };

    void timerCallback() override;
    void addEvent (int offset, int pad, float velocity) noexcept;
    void collectSequencerEvents (const juce::AudioPlayHead::PositionInfo& position, int numSamples) noexcept;
    void startVoice (int pad, const LoadedSample* sample, float velocity) noexcept;
    void renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept;
    std::unique_ptr<LoadedSample> readFile (const juce::File& file);
    juce::Array<juce::File> listAudioFilesNextTo (const juce::File& current) const;
    void finishLoad (int pad, const juce::File& file, std::unique_ptr<LoadedSample> sample);

    // Formatos de audio: se registran en el constructor y después solo se
    // leen, así que se pueden usar desde el hilo de carga.
    juce::AudioFormatManager formats;

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

    // Lista de disparos del bloque, de tamaño fijo para no reservar memoria
    // en el hilo de audio.
    static constexpr int maxEventsPerBlock = 1024;
    std::array<TriggerEvent, maxEventsPerBlock> events {};
    int numEvents = 0;

    // El patrón: un atómico por paso. La UI escribe pasos sueltos y el audio
    // los lee; como cada paso es independiente, no hace falta copiar el
    // patrón entero ni sincronizar nada más.
    std::array<std::atomic<uint8_t>, (size_t) (numPads * numSteps)> steps {};
    std::atomic<int> playingStep { -1 };

    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false };

    // Duración de cada sample cargado, para informar el "tail" al host.
    std::array<std::atomic<double>, numPads> sampleSeconds {};

    // Un único hilo de carga: los pedidos se procesan en orden.
    // Va último a propósito: los miembros se destruyen en orden inverso, así
    // que el pool (y cualquier carga en curso) termina antes que los slots,
    // padFiles y contadores que esas cargas usan.
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withThreadName ("NzDrum loader")
                                                           .withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumProcessor)
};

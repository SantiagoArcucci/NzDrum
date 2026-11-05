#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <optional>

#include "SampleSlot.h"

// Nivel de un paso del secuenciador. off = el paso no suena.
enum class StepLevel : uint8_t { off = 0, low, normal, accent };

// NzDrum: sampler de 16 pads con secuenciador por pasos.
//   - Cada pad carga un archivo de audio (en segundo plano) y suena con un
//     click en la UI o con una nota MIDI (pad 1 = C1, nota 36).
//   - Desde el pad se puede pasar al archivo anterior o siguiente de la misma
//     carpeta.
//   - Un banco de 16 patrones. Cada patrón tiene un largo (4, 8, 16 o 32
//     semicorcheas) y, por pad, pasos con tres niveles (bajo, normal y
//     acento). Sigue el play y el tempo del host con precisión de muestra,
//     con swing global. Al elegir otro patrón mientras suena, el cambio
//     espera a que termine el actual.
//   - Cada pad tiene volumen, pan, pitch, decay, inicio, fin, mute y solo como parámetros
//     del host: se pueden automatizar en el DAW.
class NzDrumProcessor final : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    static constexpr int numPads = 16;
    static constexpr int firstMidiNote = 36;   // C1 en Studio Pro
    static constexpr int numPatterns = 16;     // un banco
    static constexpr int maxSteps = 32;        // semicorcheas
    static constexpr int defaultPatternLength = 16;
    static constexpr double stepLengthPpq = 0.25;

    // Swing global, al estilo MPC: 50% es recto y 75% es el shuffle más
    // marcado. Corre las semicorcheas pares (2, 4, 6...).
    static constexpr float minSwing = 50.0f;
    static constexpr float maxSwing = 75.0f;

    // Largos de patrón permitidos.
    static bool isValidPatternLength (int length) noexcept;

    // Rangos de los parámetros por pad.
    static constexpr float minLevelDb = -48.0f;    // el mínimo equivale a silencio
    static constexpr float maxLevelDb = 6.0f;
    static constexpr float maxPitchSemitones = 24.0f;
    static constexpr float minDecaySeconds = 0.02f;
    static constexpr float maxDecaySeconds = 5.0f; // el máximo equivale a "sin decay"

    // IDs de los parámetros de un pad, por ejemplo "pad3_level".
    static juce::String paramId (int pad, const char* name);

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
    void setStep (int pattern, int pad, int step, StepLevel level) noexcept;
    StepLevel getStep (int pattern, int pad, int step) const noexcept;

    void setPatternLength (int pattern, int length) noexcept;
    int getPatternLength (int pattern) const noexcept;

    // Pide pasar a otro patrón. Con el host detenido cambia enseguida;
    // sonando, al terminar el patrón actual. El patrón elegido es el
    // parámetro "pattern" del host, así que también se puede automatizar.
    void selectPattern (int pattern);
    int getSelectedPattern() const noexcept;   // el último pedido
    int getPlayingPattern() const noexcept;    // el que suena (o sonaría)

    // Borrar un patrón: todos los pasos apagados y largo 16.
    void clearPattern (int pattern) noexcept;

    // Portapapeles de patrón: copiar guarda una foto de los pasos y el largo
    // (cambios posteriores al original no afectan lo copiado).
    void copyPattern (int pattern);
    void pastePattern (int pattern) noexcept;
    bool hasCopiedPattern() const noexcept { return patternClipboard.has_value(); }

    // Portapapeles de pad: el archivo y los ajustes (volumen, pan, pitch,
    // decay, inicio y fin). Mute y solo no se copian.
    void copyPad (int pad);
    void pastePad (int pad);
    bool hasCopiedPad() const noexcept { return padClipboard.has_value(); }

    // Paso del patrón que suena, o -1 si el host está detenido.
    int getPlayingStep() const noexcept;

    // Si el pad se escucha según mute y solo: si hay algún pad en solo, solo
    // suenan esos; un pad muteado nunca suena. Se puede llamar desde
    // cualquier hilo.
    bool isPadAudible (int pad) const noexcept;

    // Cuánto suena cada nivel. Por ahora fijo; editable por pad más adelante.
    static float gainFor (StepLevel level) noexcept;

    // Parámetros del host (volumen, pan, etc. de cada pad). La UI se conecta
    // con attachments; el audio los lee con punteros atómicos.
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    // Para que la UI dibuje la forma de onda con los mismos formatos.
    juce::AudioFormatManager& getFormatManager() noexcept { return formats; }

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
        double endPosition = 0.0; // dónde termina, según el parámetro de fin
        double increment = 1.0;   // sample rate del archivo / del host, por el pitch
        float gainLeft = 0.0f;    // velocity, volumen y pan ya combinados
        float gainRight = 0.0f;
        float envelope = 1.0f;    // decay: baja de a poco desde 1
        float envelopeCoefficient = 1.0f;
        bool playing = false;
    };

    // Punteros a los valores crudos de los parámetros de un pad. Son
    // std::atomic<float>, así que el audio los lee sin locks.
    struct PadParameters
    {
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* start = nullptr;
        std::atomic<float>* end = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

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

    juce::AudioProcessorValueTreeState parameters;
    std::array<PadParameters, numPads> padParameters;

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
    std::array<bool, numPads> blockAudible {};
    std::array<float, numPads> audibleGains {};   // 0..1, sigue a blockAudible de a poco

    // Lista de disparos del bloque, de tamaño fijo para no reservar memoria
    // en el hilo de audio.
    static constexpr int maxEventsPerBlock = 1024;
    std::array<TriggerEvent, maxEventsPerBlock> events {};
    int numEvents = 0;

    // Los patrones: un atómico por paso. La UI escribe pasos sueltos y el
    // audio los lee; como cada paso es independiente, no hace falta copiar el
    // patrón entero ni sincronizar nada más. Son 16 x 16 x 32 = 8192 bytes.
    static size_t stepIndex (int pattern, int pad, int step) noexcept
    {
        return (size_t) ((pattern * numPads + pad) * maxSteps + step);
    }

    std::array<std::atomic<uint8_t>, (size_t) (numPatterns * numPads * maxSteps)> steps {};
    std::array<std::atomic<int>, numPatterns> patternLengths;
    std::atomic<float>* patternParameter = nullptr;              // lo lee el audio
    juce::RangedAudioParameter* patternParameterObject = nullptr; // lo escribe la UI
    std::atomic<int> playingPattern { 0 };
    std::atomic<int> playingStep { -1 };
    std::atomic<float>* swingParameter = nullptr;

    // Portapapeles: solo los usa el hilo de UI.
    struct PatternClipboard
    {
        std::array<uint8_t, (size_t) (numPads * maxSteps)> steps {};
        int length = defaultPatternLength;
    };

    static constexpr std::array<const char*, 6> copiedPadParameters { "level", "pan", "pitch", "decay", "start", "end" };

    struct PadClipboard
    {
        juce::File file;
        std::array<float, copiedPadParameters.size()> values {};
    };

    std::optional<PatternClipboard> patternClipboard;
    std::optional<PadClipboard> padClipboard;

    // Estado del secuenciador en el hilo de audio.
    int currentPattern = 0;
    juce::int64 patternAnchor = 0;   // semicorchea absoluta en la que empezó el patrón actual
    double expectedBlockStart = 0.0; // dónde debería empezar el próximo bloque, en negras
    bool sequencerRunning = false;

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

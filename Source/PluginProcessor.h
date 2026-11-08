#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <optional>

#include "SampleSlot.h"

// Level of a sequencer step. off = the step doesn't play.
enum class StepLevel : uint8_t { off = 0, low, normal, accent };

// NzDrum: a 16-pad sampler with a step sequencer.
//   - Each pad loads an audio file (in the background) and plays on a click
//     in the UI or on a MIDI note (pad 1 = C1, note 36).
//   - From the pad you can step to the previous or next file in the same
//     folder.
//   - A bank of 16 patterns. Each pattern has a length (4, 8, 16 or 32
//     sixteenths) and, per pad, steps with three levels (soft, normal and
//     accent) and an optional semitone. It follows the host's transport and
//     tempo with sample accuracy, with global swing. Picking another pattern
//     while playing waits for the current one to finish.
//   - Each pad has level, pan, pitch, decay, start, end, mute and solo as
//     host parameters, so they can be automated in the DAW.
class NzDrumProcessor final : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    static constexpr int numPads = 16;
    static constexpr int firstMidiNote = 36;   // C1 in Studio Pro

    // Chromatic mode (an MPC's "16 levels"): one pad is spread across all 16,
    // one semitone apart. Pad 9 plays at its original pitch, lower pads lower
    // and higher pads higher.
    static constexpr int chromaticRootPad = 8;
    static constexpr int chromaticOffsetFor (int pad) noexcept { return pad - chromaticRootPad; }

    // Each step can store a semitone, in the same range as 16 levels, so a
    // step recorded in chromatic mode sounds the same.
    static constexpr int minStepPitch = 0 - chromaticRootPad;
    static constexpr int maxStepPitch = (numPads - 1) - chromaticRootPad;
    static constexpr int numPatterns = 16;     // one bank
    static constexpr int maxSteps = 32;        // sixteenths
    static constexpr int defaultPatternLength = 16;
    static constexpr double stepLengthPpq = 0.25;

    // Global MPC-style swing: 50% is straight and 75% is the heaviest
    // shuffle. It delays the even sixteenths (2, 4, 6...).
    static constexpr float minSwing = 50.0f;
    static constexpr float maxSwing = 75.0f;

    // Allowed pattern lengths.
    static bool isValidPatternLength (int length) noexcept;

    // Ranges of the per-pad parameters.
    static constexpr float minLevelDb = -48.0f;    // the minimum means silence
    static constexpr float maxLevelDb = 6.0f;
    static constexpr float maxPitchSemitones = 24.0f;
    static constexpr float minDecaySeconds = 0.02f;
    static constexpr float maxDecaySeconds = 5.0f; // the maximum means "no decay"

    // IDs of a pad's parameters, for example "pad3_level".
    static juce::String paramId (int pad, const char* name);

    // How values are shown (knobs and host) and parsed back when typed in.
    // Public so they can be tested.
    static juce::String levelToText (float db);
    static float textToLevel (const juce::String& text);
    static juce::String panToText (float pan);
    static float textToPan (const juce::String& text);
    static juce::String pitchToText (float semitones);
    static float textToPitch (const juce::String& text);
    static juce::String decayToText (float seconds);
    static float textToDecay (const juce::String& text);

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

    // Session state: parameters, patterns with their lengths and each pad's
    // sample path (the audio itself isn't saved, only the path).
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- API for the UI (UI thread) ----

    // Asks to load a file into a pad. Returns right away; loading runs in the
    // background.
    void loadSample (int pad, const juce::File& file);

    // Steps to the previous (-1) or next (+1) file in the pad's folder.
    void stepSample (int pad, int direction);

    // Triggers a pad from the UI.
    void triggerFromUi (int pad, float velocity) noexcept;

    // Turns chromatic mode on with `pad` as the source, or off with -1. It only
    // affects live playing (clicks and MIDI): the sequencer keeps playing the
    // pads as usual. Not saved with the session.
    void setChromaticSource (int pad) noexcept;

    // Live recording (overdub): while the DAW plays, every pad hit (click or
    // MIDI) is written to the nearest step of the playing pattern, replacing
    // whatever was there. In 16 levels it records into the source pad with
    // the semitone. Not saved with the session.
    void setRecording (bool shouldRecord) noexcept { recording.store (shouldRecord, std::memory_order_relaxed); }
    bool isRecording() const noexcept { return recording.load (std::memory_order_relaxed); }

    // The step level closest to a velocity (MIDI or click).
    static StepLevel levelForVelocity (float velocity) noexcept;
    int getChromaticSource() const noexcept { return chromaticSource.load (std::memory_order_relaxed); }

    juce::File getPadFile (int pad) const;

    // true if the session asked for a file that is gone (or couldn't be
    // read). getPadFile still returns that path, so it can be shown.
    bool isPadMissing (int pad) const;
    bool isLoading (int pad) const noexcept;
    uint32_t getTriggerCount (int pad) const noexcept;
    bool isSupportedFile (const juce::File& file) const;
    juce::String getSupportedWildcard() const;

    // ---- Sequencer (UI thread) ----
    void setStep (int pattern, int pad, int step, StepLevel level) noexcept;
    StepLevel getStep (int pattern, int pad, int step) const noexcept;

    // A step's semitone, from minStepPitch to maxStepPitch (0 = unchanged).
    // Added to the pad's pitch. Turning a step off resets it to 0.
    void setStepPitch (int pattern, int pad, int step, int semitones) noexcept;
    int getStepPitch (int pattern, int pad, int step) const noexcept;

    void setPatternLength (int pattern, int length) noexcept;
    int getPatternLength (int pattern) const noexcept;

    // Asks to switch to another pattern. With the host stopped it switches
    // right away; while playing, when the current pattern ends. The selected
    // pattern is the host's "pattern" parameter, so it can be automated too.
    void selectPattern (int pattern);
    int getSelectedPattern() const noexcept;   // the latest request
    int getPlayingPattern() const noexcept;    // the one playing (or that would play)

    // Clears a pattern: all steps off and length 16.
    void clearPattern (int pattern) noexcept;

    // Pattern clipboard: copying takes a snapshot of the steps and length
    // (later changes to the original don't affect the copy).
    void copyPattern (int pattern);
    void pastePattern (int pattern) noexcept;
    bool hasCopiedPattern() const noexcept { return patternClipboard.has_value(); }

    // Pad clipboard: the file and the settings (level, pan, pitch, decay,
    // start and end). Mute and solo aren't copied.
    void copyPad (int pad);
    void pastePad (int pad);
    bool hasCopiedPad() const noexcept { return padClipboard.has_value(); }

    // Step of the playing pattern, or -1 if the host is stopped.
    int getPlayingStep() const noexcept;

    // Whether the pad is heard given mute and solo: if any pad is soloed,
    // only those play; a muted pad never plays. Safe to call from any thread.
    bool isPadAudible (int pad) const noexcept;

    // How loud each level plays. Fixed for now; could be per pad later.
    static float gainFor (StepLevel level) noexcept;

    // Host parameters (each pad's level, pan, etc.). The UI connects through
    // attachments; the audio thread reads them through atomic pointers.
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    // So the UI can draw the waveform with the same formats.
    juce::AudioFormatManager& getFormatManager() noexcept { return formats; }

    struct TransportSnapshot
    {
        double bpm = 0.0;
        double ppq = 0.0;
        bool playing = false;
    };

    TransportSnapshot getTransport() const noexcept;

private:
    // One voice per pad: if the pad is triggered again it restarts (as on an
    // MPC). Only touched by the audio thread.
    struct PadVoice
    {
        double position = 0.0;
        double endPosition = 0.0; // where it ends, from the end parameter
        double increment = 1.0;   // file sample rate / host sample rate, times the pitch
        float gainLeft = 0.0f;    // velocity, level and pan already combined
        float gainRight = 0.0f;
        float envelope = 1.0f;    // decay: falls gradually from 1
        float envelopeCoefficient = 1.0f;
        int sourcePad = 0;        // which pad it takes the sample and mute from
        bool playing = false;
    };

    // Pointers to a pad's raw parameter values. They are std::atomic<float>,
    // so the audio thread reads them without locks.
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

    // A trigger inside the current block, from the sequencer, MIDI or the UI.
    struct TriggerEvent
    {
        int offset = 0;     // sample within the block
        int pad = 0;
        float velocity = 0.0f;
        bool chromatic = false;   // played in chromatic mode: the source sounds
        int semitones = 0;        // step semitone (sequencer only)
        juce::int64 stepIndex = -1; // absolute sixteenth (sequencer only)
        bool live = false;        // click or MIDI: can be recorded
        bool cancelled = false;   // overwritten by a recording in this same block
    };

    void timerCallback() override;
    void addEvent (const TriggerEvent& event) noexcept;
    void recordLiveHit (const TriggerEvent& event, int eventIndex, int chromatic) noexcept;
    double stepTimeFor (juce::int64 index) const noexcept;
    int patternStepFor (juce::int64 index) const noexcept;
    void collectSequencerEvents (const juce::AudioPlayHead::PositionInfo& position, int numSamples) noexcept;
    void startVoice (int voiceIndex, int sourcePad, float velocity, float extraSemitones) noexcept;
    void renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept;
    std::unique_ptr<LoadedSample> readFile (const juce::File& file);
    juce::Array<juce::File> listAudioFilesNextTo (const juce::File& current) const;
    void finishLoad (int pad, const juce::File& file, std::unique_ptr<LoadedSample> sample, uint32_t generation);
    bool isSuperseded (int pad, uint32_t generation) const noexcept;

    // Puts the pad back the way the session saved it: loads `file`, marks it
    // missing if it can't be read, or empties it if `file` is empty.
    void restorePad (int pad, const juce::File& file);

    // Audio formats: registered in the constructor and only read afterwards,
    // so they can be used from the loader thread.
    juce::AudioFormatManager formats;

    juce::AudioProcessorValueTreeState parameters;
    std::array<PadParameters, numPads> padParameters;

    std::array<SampleSlot, numPads> slots;

    // What the UI shows for each pad. Shared by the UI and the loader thread
    // (never the audio thread), so a lock is fine here.
    mutable juce::CriticalSection padFilesLock;
    std::array<juce::File, numPads> padFiles;
    std::array<bool, numPads> padMissing {};
    std::array<std::atomic<int>, numPads> pendingLoads {};

    // Goes up with every state restore; loads requested with an older value
    // are dropped when they finish.
    std::array<std::atomic<uint32_t>, numPads> restoreGeneration {};

    // Lock-free UI <-> audio communication.
    std::array<std::atomic<float>, numPads> uiTriggers {};       // velocity requested by the UI, 0 = none
    std::array<std::atomic<uint32_t>, numPads> triggerCounts {}; // so the UI can flash the pad

    // Audio thread state.
    double currentSampleRate = 44100.0;

    // One voice per pad (0 to 15) and another per pad for chromatic mode (16
    // to 31), so playing chromatically doesn't cut what the source pad is
    // playing from the sequencer.
    static constexpr int numVoices = numPads * 2;
    std::array<PadVoice, numVoices> voices;
    std::array<float, numVoices> audibleGains {};   // 0..1, follows blockAudible gradually
    std::array<const LoadedSample*, numPads> blockSamples {};
    std::array<bool, numPads> blockAudible {};
    std::atomic<int> chromaticSource { -1 };

    // The block's trigger list, fixed size so the audio thread never
    // allocates.
    static constexpr int maxEventsPerBlock = 1024;
    std::array<TriggerEvent, maxEventsPerBlock> events {};
    int numEvents = 0;

    // The patterns: one atomic per step. The UI writes single steps and the
    // audio thread reads them; since each step is independent, there's no
    // need to copy the whole pattern or synchronise anything else. That's
    // 16 x 16 x 32 = 8192 bytes.
    static size_t stepIndex (int pattern, int pad, int step) noexcept
    {
        return (size_t) ((pattern * numPads + pad) * maxSteps + step);
    }

    std::array<std::atomic<uint8_t>, (size_t) (numPatterns * numPads * maxSteps)> steps {};
    std::array<std::atomic<int8_t>, (size_t) (numPatterns * numPads * maxSteps)> stepPitches {};
    std::array<std::atomic<int>, numPatterns> patternLengths;
    std::atomic<float>* patternParameter = nullptr;              // read by the audio thread
    juce::RangedAudioParameter* patternParameterObject = nullptr; // written by the UI
    std::atomic<int> playingPattern { 0 };
    std::atomic<int> playingStep { -1 };
    std::atomic<float>* swingParameter = nullptr;

    // Clipboards: only used by the UI thread.
    struct PatternClipboard
    {
        std::array<uint8_t, (size_t) (numPads * maxSteps)> steps {};
        std::array<int8_t, (size_t) (numPads * maxSteps)> pitches {};
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

    // Sequencer state on the audio thread.
    int currentPattern = 0;
    juce::int64 patternAnchor = 0;   // absolute sixteenth where the current pattern started
    double expectedBlockStart = 0.0; // where the next block should start, in quarter notes
    bool sequencerRunning = false;

    // Timing of the current block, for quantising recorded hits.
    double blockStartPpq = 0.0, blockEndPpq = 0.0, samplesPerQuarterNow = 0.0, swingOffsetNow = 0.0;

    // A hit recorded on a step that hasn't played yet isn't repeated when the
    // sequencer reaches that step: this holds the sixteenth to skip.
    std::array<juce::int64, numPads> skipStepIndex {};
    std::atomic<bool> recording { false };

    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false };

    // Length of each loaded sample, to report the "tail" to the host.
    std::array<std::atomic<double>, numPads> sampleSeconds {};

    // A single loader thread: requests are processed in order.
    // It comes last on purpose: members are destroyed in reverse order, so the
    // pool (and any load in progress) finishes before the slots, padFiles and
    // counters those loads use.
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withThreadName ("NzDrum loader")
                                                           .withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumProcessor)
};

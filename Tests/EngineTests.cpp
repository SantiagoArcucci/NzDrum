// Audio engine tests without a DAW: they load real samples from disk,
// play MIDI notes and check the audio that comes out.
// Run with: cmake --build build --target NzDrumTests && ./build/NzDrumTests_artefacts/Release/NzDrumTests

#include "../Source/PluginProcessor.h"
#include "../Source/StepSequencerComponent.h"

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int blockSize = 512;

    // Writes a mono test WAV: a short sine.
    juce::File writeTestWav (const juce::File& folder, const juce::String& name, int lengthSamples)
    {
        auto file = folder.getChildFile (name);
        juce::AudioBuffer<float> audio (1, lengthSamples);

        for (int i = 0; i < lengthSamples; ++i)
            audio.setSample (0, i, 0.5f * std::sin ((float) i * 0.05f));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        jassert (stream != nullptr);

        auto writer = wav.createWriterFor (stream,
                                           juce::AudioFormatWriterOptions{}.withSampleRate (testSampleRate)
                                                                          .withNumChannels (1)
                                                                          .withBitsPerSample (24));
        jassert (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, lengthSamples);
        return file;
    }

    bool waitUntilLoaded (NzDrumProcessor& p, int pad)
    {
        for (int i = 0; i < 200 && p.isLoading (pad); ++i)
            juce::Thread::sleep (10);

        return ! p.isLoading (pad);
    }

    float peak (const juce::AudioBuffer<float>& b, int start, int end)
    {
        float result = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < end; ++i)
                result = juce::jmax (result, std::abs (b.getSample (ch, i)));
        return result;
    }

    // A fake host: reports tempo, play state and the quarter note the block starts on.
    struct FakePlayHead final : public juce::AudioPlayHead
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            return info;
        }
    };

    void setParameter (NzDrumProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.getParameters().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    juce::AudioBuffer<float> runBlock (NzDrumProcessor& p, juce::MidiBuffer midi = {})
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        p.processBlock (buffer, midi);
        return buffer;
    }
}

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-tests");
        folder.createDirectory();
        const auto kick1 = writeTestWav (folder, "kick1.wav", 4000);
        const auto kick2 = writeTestWav (folder, "kick2.wav", 4000);
        const auto kick10 = writeTestWav (folder, "kick10.wav", 4000);
        folder.getChildFile ("notes.txt").replaceWithText ("not audio");
        folder.getChildFile ("kick1b.wav").replaceWithText ("audio extension, broken content");

        NzDrumProcessor p;
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);

        beginTest ("An empty pad is silent");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 0);
            expectEquals (peak (runBlock (p, midi), 0, blockSize), 0.0f);
        }

        beginTest ("Loads in the background");
        {
            p.loadSample (0, kick1);
            expect (waitUntilLoaded (p, 0), "the load did not finish");
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick1.wav"));
        }

        beginTest ("The tail reports the length of the longest sample (counting chromatic mode)");
        {
            // 8 semitones down, the lowest chromatic mode can play.
            expectWithinAbsoluteError (p.getTailLengthSeconds(), 4000.0 / testSampleRate * std::exp2 (8.0 / 12.0), 1.0e-6);
        }

        beginTest ("A MIDI note plays at the exact sample");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 100);
            const auto out = runBlock (p, midi);
            expectEquals (peak (out, 0, 101), 0.0f, "sounded before the note");
            expectGreaterThan (peak (out, 101, blockSize), 0.1f, "did not sound after the note");
        }

        beginTest ("The voice ends when the sample runs out");
        {
            for (int i = 0; i < 10; ++i)
                runBlock (p);
            expectEquals (peak (runBlock (p), 0, blockSize), 0.0f);
        }

        beginTest ("Out-of-range notes are ignored");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote - 1, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + 16, 1.0f), 0);
            expectEquals (peak (runBlock (p, midi), 0, blockSize), 0.0f);
        }

        beginTest ("A UI click triggers the pad");
        {
            const auto before = p.getTriggerCount (0);
            p.triggerFromUi (0, 0.8f);
            expectGreaterThan (peak (runBlock (p), 0, blockSize), 0.1f);
            expectEquals ((int) p.getTriggerCount (0), (int) before + 1);
        }

        beginTest ("Next skips a broken file instead of getting stuck");
        {
            p.loadSample (0, kick1);
            expect (waitUntilLoaded (p, 0));
            p.stepSample (0, +1);   // the neighbour is kick1b.wav, which can't be read
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick2.wav"));

            p.stepSample (0, -1);   // and backwards it skips it too
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick1.wav"));
        }

        beginTest ("Next walks the folder in natural order and ignores non-audio files");
        {
            p.stepSample (0, +1);
            p.stepSample (0, +1);   // two clicks in a row: each one starts from the previous
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));

            p.stepSample (0, +1);   // wraps around
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick1.wav"));

            p.stepSample (0, -1);
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));
        }

        beginTest ("An invalid file doesn't change the pad");
        {
            p.loadSample (0, folder.getChildFile ("notes.txt"));
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));

            // A valid WAV with no audio isn't published either.
            auto emptyFolder = folder.getChildFile ("empty");
            emptyFolder.createDirectory();
            p.loadSample (0, writeTestWav (emptyFolder, "silence.wav", 0));
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));
        }

        beginTest ("Changing the sample while it plays breaks nothing");
        {
            p.triggerFromUi (0, 1.0f);
            runBlock (p);
            p.loadSample (0, kick2);
            expect (waitUntilLoaded (p, 0));
            runBlock (p);   // here the new sample is picked up and the old voice is cut
            expectEquals (peak (runBlock (p), 0, blockSize), 0.0f);
        }

        runSequencerTests (kick1);
        runClickTests();

        folder.deleteRecursively();
    }

    void runSequencerTests (const juce::File& sampleFile)
    {
        // At 120 BPM and 48 kHz a quarter note lasts 24000 samples and a step, 6000.
        constexpr double samplesPerQuarter = 24000.0;

        NzDrumProcessor p;
        FakePlayHead host;
        p.setPlayHead (&host);
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);
        p.loadSample (0, sampleFile);
        expect (waitUntilLoaded (p, 0));

        const auto blockStartingAt = [&] (double ppq)
        {
            host.ppq = ppq;
            return runBlock (p);
        };

        beginTest ("With no active steps the sequencer is silent");
        {
            expectEquals (peak (blockStartingAt (0.0), 0, blockSize), 0.0f);
        }

        beginTest ("A step plays at the exact sample inside the block");
        {
            p.setStep (0, 0, 1, StepLevel::normal);   // step 2 falls on quarter note 0.25
            const auto out = blockStartingAt (0.25 - 100.0 / samplesPerQuarter);
            expectEquals (peak (out, 0, 100), 0.0f, "sounded before the step");
            expectGreaterThan (peak (out, 100, blockSize), 0.1f, "did not sound on the step");
        }

        beginTest ("A step right on the block edge plays only once");
        {
            p.setStep (0, 0, 1, StepLevel::off);
            p.setStep (0, 0, 2, StepLevel::normal);   // quarter note 0.5
            const int before = (int) p.getTriggerCount (0);
            blockStartingAt (0.5 - blockSize / samplesPerQuarter);   // ends right on the step
            blockStartingAt (0.5);                                    // starts right on the step
            expectEquals ((int) p.getTriggerCount (0) - before, 1);
            p.setStep (0, 0, 2, StepLevel::off);
        }

        beginTest ("With the host stopped nothing plays and there is no current step");
        {
            p.setStep (0, 0, 0, StepLevel::normal);
            host.playing = false;
            for (int i = 0; i < 20; ++i) runBlock (p);
            expectEquals (peak (blockStartingAt (0.0), 0, blockSize), 0.0f);
            expectEquals (p.getPlayingStep(), -1);
            host.playing = true;
        }

        beginTest ("The pattern wraps around after step 16");
        {
            for (int i = 0; i < 20; ++i) runBlock (p);   // let the previous voice finish
            host.playing = false; runBlock (p); host.playing = true;
            const int before = (int) p.getTriggerCount (0);
            blockStartingAt (4.0);   // quarter note 4 = step 17 = step 1 of the pattern
            expectEquals ((int) p.getTriggerCount (0) - before, 1);
            expectEquals (p.getPlayingStep(), 0);
        }

        beginTest ("Accent is louder than soft, in proportion to their levels");
        {
            const auto peakFor = [&] (StepLevel level)
            {
                p.setStep (0, 0, 0, level);
                for (int i = 0; i < 20; ++i) { host.playing = false; runBlock (p); }
                host.playing = true;
                return peak (blockStartingAt (0.0), 0, blockSize);
            };

            const float low = peakFor (StepLevel::low);
            const float accent = peakFor (StepLevel::accent);
            expectWithinAbsoluteError (accent / low,
                                       NzDrumProcessor::gainFor (StepLevel::accent) / NzDrumProcessor::gainFor (StepLevel::low),
                                       0.01f);
            p.setStep (0, 0, 0, StepLevel::off);
        }

        p.setPlayHead (nullptr);
    }

    void runClickTests()
    {
        using Seq = StepSequencerComponent;
        const juce::ModifierKeys none, shift (juce::ModifierKeys::shiftModifier), alt (juce::ModifierKeys::altModifier);

        beginTest ("Click: normal, Shift: accent, Option: soft; repeating turns it off");
        {
            expect (Seq::levelAfterClick (StepLevel::off, none) == StepLevel::normal);
            expect (Seq::levelAfterClick (StepLevel::normal, none) == StepLevel::off);
            expect (Seq::levelAfterClick (StepLevel::off, shift) == StepLevel::accent);
            expect (Seq::levelAfterClick (StepLevel::accent, shift) == StepLevel::off);
            expect (Seq::levelAfterClick (StepLevel::off, alt) == StepLevel::low);
            expect (Seq::levelAfterClick (StepLevel::low, alt) == StepLevel::off);
            expect (Seq::levelAfterClick (StepLevel::accent, none) == StepLevel::normal);
            expect (Seq::levelAfterClick (StepLevel::normal, shift) == StepLevel::accent);
        }
    }
};

static EngineTests engineTests;

// Per-pad settings: level, pan, pitch, start/end, decay, mute and solo.
class PadSettingsTests final : public juce::UnitTest
{
public:
    PadSettingsTests() : juce::UnitTest ("Pad settings", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-settings");
        folder.createDirectory();
        const auto tone = writeTestWav (folder, "tone.wav", 4000);

        NzDrumProcessor p;
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);

        for (int pad : { 0, 1 })
        {
            p.loadSample (pad, tone);
            expect (waitUntilLoaded (p, pad), "the load did not finish");
        }

        // The baseline, with nothing touched.
        const auto reference = hit (p, 0);
        const float referencePeak = peak (reference, 0, reference.getNumSamples());
        const int referenceLength = soundingLength (reference);

        beginTest ("Level: -6 dB gives half the amplitude");
        {
            set (p, 0, "level", -6.0f);
            const auto out = hit (p, 0);
            expectWithinAbsoluteError (peak (out, 0, out.getNumSamples()) / referencePeak, 0.501f, 0.01f);
            set (p, 0, "level", 0.0f);
        }

        beginTest ("Level at minimum is silence");
        {
            set (p, 0, "level", NzDrumProcessor::minLevelDb);
            const auto out = hit (p, 0);
            expectEquals (peak (out, 0, out.getNumSamples()), 0.0f);
            set (p, 0, "level", 0.0f);
        }

        beginTest ("Pan hard left silences the right channel");
        {
            set (p, 0, "pan", -1.0f);
            const auto out = hit (p, 0);
            expectWithinAbsoluteError (out.getMagnitude (0, 0, out.getNumSamples()), referencePeak, 1.0e-3f);
            expectEquals (out.getMagnitude (1, 0, out.getNumSamples()), 0.0f);
            set (p, 0, "pan", 0.0f);
        }

        beginTest ("Pitch +12 plays an octave up: it lasts half as long");
        {
            set (p, 0, "pitch", 12.0f);
            const int length = soundingLength (hit (p, 0));
            expectWithinAbsoluteError (length, referenceLength / 2, 4);
            set (p, 0, "pitch", 0.0f);
        }

        beginTest ("Start and end trim the sample, even when swapped");
        {
            set (p, 0, "start", 0.5f);
            set (p, 0, "end", 0.25f);
            const int length = soundingLength (hit (p, 0));
            expectWithinAbsoluteError (length, referenceLength / 4, 4);
            set (p, 0, "start", 0.0f);
            set (p, 0, "end", 1.0f);
        }

        beginTest ("Start and end at the very end of the sample don't read past the file");
        {
            set (p, 0, "start", 1.0f);
            set (p, 0, "end", 1.0f);
            const auto out = hit (p, 0);
            expectLessOrEqual (soundingLength (out), 2);
            set (p, 0, "start", 0.0f);
            set (p, 0, "end", 1.0f);
        }

        beginTest ("A short decay fades out the end of the sound");
        {
            set (p, 0, "decay", NzDrumProcessor::minDecaySeconds);
            const auto out = hit (p, 0);
            // 0.02 s of decay: at 2000 samples (42 ms) it is close to e^-2.
            expectLessThan (peak (out, 2000, 3000), 0.2f * peak (reference, 2000, 3000));
            set (p, 0, "decay", NzDrumProcessor::maxDecaySeconds);
        }

        beginTest ("A muted pad is silent, from MIDI or from a click");
        {
            set (p, 0, "mute", 1.0f);
            expect (! p.isPadAudible (0));
            const auto midiHit = hit (p, 0);
            expectEquals (peak (midiHit, 0, midiHit.getNumSamples()), 0.0f, "sounded from MIDI");

            p.triggerFromUi (0, 1.0f);
            const auto clickHit = render (p, {}, 4800);
            expectEquals (peak (clickHit, 0, clickHit.getNumSamples()), 0.0f, "sounded from a click");
            set (p, 0, "mute", 0.0f);
        }

        beginTest ("Muting while playing fades the sound out instead of cutting it");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 0);
            runBlock (p, midi);
            set (p, 0, "mute", 1.0f);

            const auto out = runBlock (p);
            const int fadeSamples = (int) (0.005 * testSampleRate);
            expectGreaterThan (peak (out, 0, 40), 0.05f, "cut abruptly");
            expectEquals (peak (out, fadeSamples + 1, blockSize), 0.0f, "did not finish fading");

            set (p, 0, "mute", 0.0f);
            render (p, {}, 4800);
        }

        beginTest ("With one pad soloed, the others are silent");
        {
            set (p, 1, "solo", 1.0f);
            expect (p.isPadAudible (1));
            expect (! p.isPadAudible (0));

            const auto other = hit (p, 0);
            expectEquals (peak (other, 0, other.getNumSamples()), 0.0f, "a pad outside the solo sounded");

            const auto soloed = hit (p, 1);
            expectWithinAbsoluteError (peak (soloed, 0, soloed.getNumSamples()), referencePeak, 1.0e-3f);
        }

        beginTest ("A muted pad is silent even when soloed");
        {
            set (p, 1, "mute", 1.0f);
            const auto out = hit (p, 1);
            expectEquals (peak (out, 0, out.getNumSamples()), 0.0f);
            set (p, 1, "mute", 0.0f);
            set (p, 1, "solo", 0.0f);
            expect (p.isPadAudible (0));
        }

        folder.deleteRecursively();
    }

private:
    static void set (NzDrumProcessor& p, int pad, const char* name, float value)
    {
        auto* param = p.getParameters().getParameter (NzDrumProcessor::paramId (pad, name));
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // Processes several blocks in a row and joins them into one buffer.
    static juce::AudioBuffer<float> render (NzDrumProcessor& p, juce::MidiBuffer firstBlockMidi, int numSamples)
    {
        juce::AudioBuffer<float> result (2, numSamples);
        result.clear();

        for (int start = 0; start < numSamples; start += blockSize)
        {
            auto block = runBlock (p, start == 0 ? firstBlockMidi : juce::MidiBuffer {});
            const int count = juce::jmin (blockSize, numSamples - start);

            for (int ch = 0; ch < 2; ++ch)
                result.copyFrom (ch, start, block, ch, 0, count);
        }

        return result;
    }

    // Triggers the pad by MIDI at the start and returns 100 ms of audio.
    static juce::AudioBuffer<float> hit (NzDrumProcessor& p, int pad)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + pad, 1.0f), 0);
        return render (p, midi, 4800);
    }

    // The last sample that has sound.
    static int soundingLength (const juce::AudioBuffer<float>& b)
    {
        for (int i = b.getNumSamples(); --i >= 0;)
            if (std::abs (b.getSample (0, i)) > 0.0f || std::abs (b.getSample (1, i)) > 0.0f)
                return i + 1;

        return 0;
    }
};

static PadSettingsTests padSettingsTests;

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("NzDrum");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    return failures == 0 ? 0 : 1;
}

// Pattern bank, per-pattern length and swing.
class PatternTests final : public juce::UnitTest
{
public:
    PatternTests() : juce::UnitTest ("Patterns and swing", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-patterns");
        folder.createDirectory();
        const auto blip = writeTestWav (folder, "blip.wav", 200);   // short: ends before the next step

        NzDrumProcessor p;
        FakePlayHead host;
        p.setPlayHead (&host);
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);

        // Pad 1 hard left and pad 2 hard right, to tell which one sounded.
        for (int pad : { 0, 1 })
        {
            p.loadSample (pad, blip);
            expect (waitUntilLoaded (p, pad));
        }

        setParameter (p, NzDrumProcessor::paramId (0, "pan"), -1.0f);
        setParameter (p, NzDrumProcessor::paramId (1, "pan"), 1.0f);

        // At 120 BPM and 48 kHz: a quarter note = 24000 samples, a sixteenth = 6000.
        const double samplesPerQuarter = testSampleRate * 60.0 / host.bpm;

        beginTest ("Steps belong to each pattern");
        {
            p.setStep (1, 0, 3, StepLevel::accent);
            expect (p.getStep (1, 0, 3) == StepLevel::accent);
            expect (p.getStep (0, 0, 3) == StepLevel::off);
            p.setStep (1, 0, 3, StepLevel::off);
        }

        beginTest ("Only lengths of 4, 8, 16 and 32 steps are accepted");
        {
            expectEquals (p.getPatternLength (0), 16);
            p.setPatternLength (0, 12);
            expectEquals (p.getPatternLength (0), 16);
            p.setPatternLength (0, 32);
            expectEquals (p.getPatternLength (0), 32);
            p.setPatternLength (0, 16);
        }

        beginTest ("A 4-step pattern repeats every quarter note");
        {
            p.setStep (0, 0, 0, StepLevel::normal);
            p.setPatternLength (0, 4);
            const auto onsets = play (p, host, 0.0, 2.0);
            expect (onsets.left == std::vector<int> { 0, 24000 }, describe (onsets.left));
            p.setPatternLength (0, 16);
            p.setStep (0, 0, 0, StepLevel::off);
        }

        beginTest ("In a 32-step pattern, step 17 plays on quarter note 4");
        {
            p.setPatternLength (0, 32);
            p.setStep (0, 0, 16, StepLevel::normal);
            const auto onsets = play (p, host, 0.0, 8.5);
            expect (onsets.left == std::vector<int> { 4 * 24000 }, describe (onsets.left));
            p.setStep (0, 0, 16, StepLevel::off);
            p.setPatternLength (0, 16);
        }

        beginTest ("Swing: delays the even sixteenths by the percentage");
        {
            p.setStep (0, 0, 0, StepLevel::normal);
            p.setStep (0, 0, 1, StepLevel::normal);

            for (const auto& [swing, expected] : { std::pair { 50.0f, 6000 }, std::pair { 60.0f, 7200 }, std::pair { 75.0f, 9000 } })
            {
                setParameter (p, "swing", swing);
                const auto onsets = play (p, host, 0.0, 0.5);
                expect (onsets.left == std::vector<int> { 0, expected },
                        "swing " + juce::String (swing) + ": " + describe (onsets.left));
            }

            setParameter (p, "swing", 50.0f);
            p.setStep (0, 0, 0, StepLevel::off);
            p.setStep (0, 0, 1, StepLevel::off);
        }

        beginTest ("With swing, a delayed step that falls in the next block still plays");
        {
            setParameter (p, "swing", 75.0f);
            p.setStep (0, 0, 1, StepLevel::normal);   // without swing it would fall on 6000; at 75 %, on 9000
            const auto onsets = play (p, host, 7000.0 / samplesPerQuarter, 0.25);
            expect (onsets.left == std::vector<int> { 2000 }, describe (onsets.left));
            setParameter (p, "swing", 50.0f);
            p.setStep (0, 0, 1, StepLevel::off);
        }

        beginTest ("Switching patterns while playing waits for the end of the pattern");
        {
            p.setStep (0, 0, 0, StepLevel::normal);   // pattern 1: pad 1 on step 1
            p.setStep (1, 1, 0, StepLevel::normal);   // pattern 2: pad 2 on step 1
            p.selectPattern (0);
            stop (p, host);

            Onsets onsets;
            playInto (onsets, p, host, 0.0, 1.0, 0);
            p.selectPattern (1);
            expectEquals (p.getPlayingPattern(), 0, "switched too early");
            playInto (onsets, p, host, 1.0, 4.5, (int) (1.0 * samplesPerQuarter));

            expect (onsets.left == std::vector<int> { 0 }, "pad 1: " + describe (onsets.left));
            expect (onsets.right == std::vector<int> { 4 * 24000 }, "pad 2: " + describe (onsets.right));
            expectEquals (p.getPlayingPattern(), 1);
        }

        beginTest ("Loading a sample between patterns doesn't erase the previous pattern");
        {
            stop (p, host);
            p.selectPattern (3);
            p.setStep (3, 0, 0, StepLevel::normal);
            p.setStep (3, 0, 4, StepLevel::accent);

            // Load a sample on a pad that was empty and then build another pattern.
            p.loadSample (5, blip);
            expect (waitUntilLoaded (p, 5));
            p.setStep (3, 5, 2, StepLevel::low);
            p.selectPattern (4);
            p.setStep (4, 0, 8, StepLevel::normal);
            for (int i = 0; i < 10; ++i) stop (p, host);

            expect (p.getStep (3, 0, 0) == StepLevel::normal);
            expect (p.getStep (3, 0, 4) == StepLevel::accent);
            expect (p.getStep (3, 5, 2) == StepLevel::low);

            p.selectPattern (3);
            stop (p, host);
            const auto onsets = play (p, host, 0.0, 1.0);
            expect (onsets.left == std::vector<int> { 0, 12000 }, describe (onsets.left));   // pad 1 on step 1, pad 6 on step 3

            for (auto [pattern, pad, step] : { std::tuple { 3, 0, 0 }, std::tuple { 3, 0, 4 }, std::tuple { 3, 5, 2 }, std::tuple { 4, 0, 8 } })
                p.setStep (pattern, pad, step, StepLevel::off);
            p.selectPattern (0);
            stop (p, host);
        }

        beginTest ("Copying and pasting a pattern copies steps and length, as a snapshot");
        {
            p.setStep (6, 2, 5, StepLevel::accent);
            p.setPatternLength (6, 8);
            p.copyPattern (6);
            p.setStep (6, 2, 5, StepLevel::off);   // changing the original afterwards doesn't affect the copy

            expect (p.hasCopiedPattern());
            p.pastePattern (7);
            expect (p.getStep (7, 2, 5) == StepLevel::accent);
            expectEquals (p.getPatternLength (7), 8);
            expect (p.getStep (6, 2, 5) == StepLevel::off);
        }

        beginTest ("Clearing a pattern turns off every step and resets it to 16");
        {
            p.setStep (7, 9, 31, StepLevel::low);
            p.clearPattern (7);

            bool anyStep = false;
            for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
                for (int step = 0; step < NzDrumProcessor::maxSteps; ++step)
                    anyStep = anyStep || p.getStep (7, pad, step) != StepLevel::off;

            expect (! anyStep);
            expectEquals (p.getPatternLength (7), 16);
            p.clearPattern (6);
        }

        beginTest ("Copying and pasting a pad copies audio and settings, not mute");
        {
            const auto value = [&p] (int pad, const char* parameterName)
            {
                return p.getParameters().getRawParameterValue (NzDrumProcessor::paramId (pad, parameterName))->load();
            };

            expect (! p.hasCopiedPad());
            setParameter (p, NzDrumProcessor::paramId (0, "pitch"), 7.0f);
            setParameter (p, NzDrumProcessor::paramId (0, "start"), 0.25f);
            setParameter (p, NzDrumProcessor::paramId (0, "mute"), 1.0f);
            p.copyPad (0);
            p.pastePad (9);
            expect (waitUntilLoaded (p, 9));

            expect (p.getPadFile (9) == p.getPadFile (0));
            expectWithinAbsoluteError (value (9, "pitch"), 7.0f, 0.01f);
            expectWithinAbsoluteError (value (9, "pan"), -1.0f, 0.01f);
            expectWithinAbsoluteError (value (9, "start"), 0.25f, 0.001f);
            expectEquals (value (9, "mute"), 0.0f);

            setParameter (p, NzDrumProcessor::paramId (0, "pitch"), 0.0f);
            setParameter (p, NzDrumProcessor::paramId (0, "start"), 0.0f);
            setParameter (p, NzDrumProcessor::paramId (0, "mute"), 0.0f);
        }

        beginTest ("The selected pattern is the host parameter 'pattern' (1 to 16)");
        {
            stop (p, host);
            setParameter (p, "pattern", 5.0f);
            expectEquals (p.getSelectedPattern(), 4);
            stop (p, host);
            expectEquals (p.getPlayingPattern(), 4);

            p.selectPattern (9);
            expectEquals (p.getParameters().getRawParameterValue ("pattern")->load(), 10.0f);
            p.selectPattern (0);
            stop (p, host);
        }

        beginTest ("Automating the parameter chains patterns at the end of each one");
        {
            // Pattern 1 has 4 steps with pad 1; pattern 2 has 4 steps with pad 2.
            p.setPatternLength (0, 4);
            p.setPatternLength (1, 4);
            p.setStep (0, 0, 0, StepLevel::normal);
            p.setStep (1, 1, 0, StepLevel::normal);
            setParameter (p, "pattern", 1.0f);
            stop (p, host);

            // As in a DAW: the automation changes halfway through the first
            // pattern and the change only takes effect on the next quarter note.
            Onsets onsets;
            playInto (onsets, p, host, 0.0, 0.5, 0);
            setParameter (p, "pattern", 2.0f);
            playInto (onsets, p, host, 0.5, 2.5, (int) (0.5 * samplesPerQuarter));

            expect (onsets.left == std::vector<int> { 0 }, "pad 1: " + describe (onsets.left));
            expect (onsets.right == std::vector<int> { 24000, 48000 }, "pad 2: " + describe (onsets.right));

            p.clearPattern (0);
            p.clearPattern (1);
            setParameter (p, "pattern", 1.0f);
            stop (p, host);
        }

        beginTest ("With the host stopped, a pattern switch is immediate");
        {
            stop (p, host);
            p.selectPattern (2);
            stop (p, host);
            expectEquals (p.getPlayingPattern(), 2);
            p.selectPattern (0);
            stop (p, host);
        }

        p.setPlayHead (nullptr);
        folder.deleteRecursively();
    }

private:
    struct Onsets
    {
        std::vector<int> left, right;
    };

    static void stop (NzDrumProcessor& p, FakePlayHead& host)
    {
        host.playing = false;
        runBlock (p);
        host.playing = true;
    }

    static juce::String describe (const std::vector<int>& values)
    {
        juce::StringArray parts;
        for (auto v : values)
            parts.add (juce::String (v));
        return "[" + parts.joinIntoString (", ") + "]";
    }

    // Plays consecutive blocks from `fromPpq` to `toPpq` and records the sample
    // (counted from `firstSample`) where each hit starts on each channel.
    // The test sample starts at sin(0) = 0, so the hit starts one sample
    // before the first non-zero value.
    void playInto (Onsets& onsets, NzDrumProcessor& p, FakePlayHead& host, double fromPpq, double toPpq, int firstSample)
    {
        const double samplesPerQuarter = testSampleRate * 60.0 / host.bpm;
        const int total = (int) std::llround ((toPpq - fromPpq) * samplesPerQuarter);
        std::array<int, 2> silentFor { 1000, 1000 };

        for (int done = 0; done < total; done += blockSize)
        {
            host.ppq = fromPpq + done / samplesPerQuarter;
            const auto block = runBlock (p);

            for (int i = 0; i < blockSize && done + i < total; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    // A threshold rather than exact zero: with pan fully to one side, the
                    // parameter rounding can leave the other channel at ~1e-8 (happens on Mac).
                    if (std::abs (block.getSample (ch, i)) < 1.0e-4f)
                    {
                        ++silentFor[(size_t) ch];
                        continue;
                    }

                    if (silentFor[(size_t) ch] > 100)
                        (ch == 0 ? onsets.left : onsets.right).push_back (firstSample + done + i - 1);

                    silentFor[(size_t) ch] = 0;
                }
            }
        }
    }

    Onsets play (NzDrumProcessor& p, FakePlayHead& host, double fromPpq, double quarters)
    {
        stop (p, host);
        Onsets onsets;
        playInto (onsets, p, host, fromPpq, fromPpq + quarters, 0);
        return onsets;
    }
};

static PatternTests patternTests;

// Saving and loading state, as the DAW does when saving and opening a session.
class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("Session state", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-state");
        folder.createDirectory();
        const auto kick = writeTestWav (folder, "kick.wav", 4000);
        const auto snare = writeTestWav (folder, "snare.wav", 4000);
        const auto other = writeTestWav (folder, "other.wav", 4000);

        juce::MemoryBlock saved;

        {
            NzDrumProcessor p;
            prepare (p);
            p.loadSample (0, kick);
            p.loadSample (3, snare);
            expect (waitUntilLoaded (p, 0) && waitUntilLoaded (p, 3));

            p.setStep (0, 0, 0, StepLevel::accent);
            p.setStep (5, 15, 31, StepLevel::low);
            p.setPatternLength (5, 32);
            p.setPatternLength (2, 4);
            setParameter (p, "swing", 62.0f);
            setParameter (p, "pattern", 3.0f);
            setParameter (p, NzDrumProcessor::paramId (0, "level"), -6.0f);
            setParameter (p, NzDrumProcessor::paramId (3, "mute"), 1.0f);

            p.getStateInformation (saved);
        }

        beginTest ("A new instance restores patterns, lengths, parameters and samples");
        {
            NzDrumProcessor p;
            prepare (p);
            p.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (waitUntilLoaded (p, 0) && waitUntilLoaded (p, 3));

            expect (p.getStep (0, 0, 0) == StepLevel::accent);
            expect (p.getStep (5, 15, 31) == StepLevel::low);
            expect (p.getStep (0, 0, 1) == StepLevel::off);
            expectEquals (p.getPatternLength (5), 32);
            expectEquals (p.getPatternLength (2), 4);
            expectEquals (p.getPatternLength (0), 16);
            expectWithinAbsoluteError (value (p, "swing"), 62.0f, 0.01f);
            expectEquals (p.getSelectedPattern(), 2);
            expectWithinAbsoluteError (value (p, NzDrumProcessor::paramId (0, "level")), -6.0f, 0.01f);
            expectEquals (value (p, NzDrumProcessor::paramId (3, "mute")), 1.0f);

            expect (p.getPadFile (0) == kick);
            expect (p.getPadFile (3) == snare);
            expect (p.getPadFile (1) == juce::File());
            expect (! p.isPadMissing (0));

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 0);
            expectGreaterThan (peak (runBlock (p, midi), 0, blockSize), 0.1f, "pad 1 is silent");
        }

        beginTest ("If a file is missing, the pad is silent and marked as missing");
        {
            snare.moveFileTo (folder.getChildFile ("moved.wav"));

            NzDrumProcessor p;
            prepare (p);
            p.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (waitUntilLoaded (p, 0) && waitUntilLoaded (p, 3));

            expect (p.isPadMissing (3));
            expectEquals (p.getPadFile (3).getFileName(), juce::String ("snare.wav"));
            expect (! p.isPadMissing (0));
            setParameter (p, NzDrumProcessor::paramId (3, "mute"), 0.0f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + 3, 1.0f), 0);
            runBlock (p);   // the audio thread picks up the empty pad
            expectEquals (peak (runBlock (p, midi), 0, blockSize), 0.0f);

            // "Next" from a missing pad still walks the folder. The folder
            // holds kick, moved and other: none comes after "snare" in
            // natural order, so it wraps around to "kick.wav".
            p.stepSample (3, +1);
            expect (waitUntilLoaded (p, 3));
            expect (! p.isPadMissing (3), "next did not recover the pad");
            expectEquals (p.getPadFile (3).getFileName(), juce::String ("kick.wav"));

            // Loading another file clears the mark.
            p.loadSample (3, other);
            expect (waitUntilLoaded (p, 3));
            expect (! p.isPadMissing (3));
        }

        beginTest ("Loading a state onto an instance with samples empties the pads it doesn't have");
        {
            NzDrumProcessor p;
            prepare (p);
            p.loadSample (7, other);
            p.setStep (9, 7, 2, StepLevel::normal);
            expect (waitUntilLoaded (p, 7));

            p.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (waitUntilLoaded (p, 7));
            expect (p.getPadFile (7) == juce::File());
            expect (! p.isPadMissing (7));
            expect (p.getStep (9, 7, 2) == StepLevel::off);
        }

        beginTest ("Saving while samples are loading saves the new paths");
        {
            NzDrumProcessor p;
            prepare (p);
            p.setStateInformation (saved.getData(), (int) saved.getSize());

            juce::MemoryBlock again;
            p.getStateInformation (again);   // without waiting for the loads to finish
            const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            expect (xml != nullptr && xml->toString().contains ("kick.wav"), "pad 1 path was lost");
            expect (waitUntilLoaded (p, 0));
        }

        beginTest ("A state wins over a load that was queued");
        {
            NzDrumProcessor p;
            prepare (p);
            p.loadSample (1, other);   // the state has nothing on pad 2
            p.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (waitUntilLoaded (p, 1));
            expect (p.getPadFile (1) == juce::File());
        }

        beginTest ("A load requested before the state doesn't overwrite the restored path");
        {
            // Several long loads queued before the state: with the fix, they are
            // dropped when they finish and pad 1 path is always kick.
            const auto big = writeTestWav (folder, "big.wav", 2000000);

            NzDrumProcessor p;
            prepare (p);

            for (int i = 0; i < 4; ++i)
                p.loadSample (0, big);

            p.setStateInformation (saved.getData(), (int) saved.getSize());

            bool alwaysRestored = true;
            const auto deadline = juce::Time::getMillisecondCounter() + 20000;

            while (p.isLoading (0) && juce::Time::getMillisecondCounter() < deadline)
            {
                alwaysRestored = alwaysRestored && p.getPadFile (0) == kick;
                juce::Thread::sleep (1);
            }

            expect (! p.isLoading (0));
            expect (alwaysRestored, "a stale load overwrote the restored path");
            expect (p.getPadFile (0) == kick);
        }

        beginTest ("Garbage data breaks nothing and doesn't change the state");
        {
            NzDrumProcessor p;
            prepare (p);
            p.setStep (1, 1, 1, StepLevel::normal);
            const char garbage[] = "this is not an NzDrum state";
            p.setStateInformation (garbage, (int) sizeof (garbage));
            p.setStateInformation (nullptr, 0);
            expect (p.getStep (1, 1, 1) == StepLevel::normal);
        }

        folder.deleteRecursively();
    }

private:
    static void prepare (NzDrumProcessor& p)
    {
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);
    }

    static float value (NzDrumProcessor& p, const juce::String& id)
    {
        return p.getParameters().getRawParameterValue (id)->load();
    }
};

static StateTests stateTests;

// ============================================================================
// How knob values are displayed and parsed
// ============================================================================

class DisplayTests final : public juce::UnitTest
{
public:
    DisplayTests() : juce::UnitTest ("Knob text", "NzDrum") {}

    void runTest() override
    {
        using P = NzDrumProcessor;

        beginTest ("Each value shows its unit and zero never shows as -0.00");
        {
            expectEquals (P::levelToText (-6.0f), juce::String ("-6.0 dB"));
            expectEquals (P::levelToText (-0.01f), juce::String ("0.0 dB"));
            expectEquals (P::levelToText (P::minLevelDb), juce::String ("-inf dB"));

            expectEquals (P::panToText (0.001f), juce::String ("C"));
            expectEquals (P::panToText (-0.25f), juce::String ("25 L"));

            expectEquals (P::pitchToText (-0.001f), juce::String ("0.00 st"));
            expectEquals (P::pitchToText (0.0f), juce::String ("0.00 st"));
            expectEquals (P::pitchToText (1.5f), juce::String ("+1.50 st"));
            expectEquals (P::pitchToText (-12.0f), juce::String ("-12.00 st"));

            expectEquals (P::decayToText (0.25f), juce::String ("250 ms"));
            expectEquals (P::decayToText (1.5f), juce::String ("1.50 s"));
            expectEquals (P::decayToText (P::maxDecaySeconds), juce::String ("off"));
        }

        beginTest ("Typed values are understood, with or without a unit");
        {
            expectWithinAbsoluteError (P::textToLevel ("-6 dB"), -6.0f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToLevel ("-inf"), P::minLevelDb, 1.0e-4f);
            expectWithinAbsoluteError (P::textToPan ("30 L"), -0.3f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToPan ("30R"), 0.3f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToPan ("C"), 0.0f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToPan ("-30"), -0.3f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToPan ("45"), 0.45f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToPitch ("+1,5 st"), 1.5f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToDecay ("250 ms"), 0.25f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToDecay ("1.2"), 1.2f, 1.0e-4f);
            expectWithinAbsoluteError (P::textToDecay ("off"), P::maxDecaySeconds, 1.0e-4f);
        }

        beginTest ("The host sees the same text as the knob");
        {
            NzDrumProcessor p;
            auto* pitch = p.getParameters().getParameter (P::paramId (0, "pitch"));
            expect (pitch != nullptr);
            expectEquals (pitch->getText (pitch->convertTo0to1 (-0.001f), 32), juce::String ("0.00 st"));

            auto* start = p.getParameters().getParameter (P::paramId (0, "start"));
            expectEquals (start->getText (0.125f, 32), juce::String ("12.5 %"));
            expectWithinAbsoluteError (start->getValueForText ("50 %"), 0.5f, 1.0e-4f);
        }
    }
};

static DisplayTests displayTests;

// ============================================================================
// Chromatic mode ("16 Levels")
// ============================================================================

class ChromaticTests final : public juce::UnitTest
{
public:
    ChromaticTests() : juce::UnitTest ("Chromatic mode", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-chromatic");
        folder.createDirectory();
        const auto tone = writeTestWav (folder, "tone.wav", 4000);

        beginTest ("Pad 9 plays like the source and the ones below play lower (longer)");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            const int normal = soundLength (p, 0);

            p.setChromaticSource (0);
            const int root = soundLength (p, NzDrumProcessor::chromaticRootPad);
            const int low = soundLength (p, 0);    // -8 semitonos
            const int high = soundLength (p, 15);  // +7 semitonos

            expectWithinAbsoluteError (root, normal, 4);
            expectWithinAbsoluteError ((double) low / root, std::exp2 (8.0 / 12.0), 0.01);
            expectWithinAbsoluteError ((double) high / root, std::exp2 (-7.0 / 12.0), 0.01);

            // The host doesn't cut the lowest note when exporting.
            expectGreaterOrEqual (p.getTailLengthSeconds() * testSampleRate, (double) low - 1.0);
        }

        beginTest ("It adds to the source pitch");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            const int root = soundLength (p, 0);
            setParameter (p, NzDrumProcessor::paramId (0, "pitch"), 12.0f);
            p.setChromaticSource (0);
            const int plusTwelvePlusOne = soundLength (p, NzDrumProcessor::chromaticRootPad + 1);
            expectWithinAbsoluteError ((double) plusTwelvePlusOne / root, std::exp2 (-13.0 / 12.0), 0.01);
        }

        beginTest ("With the source muted nothing plays; turning it off goes back to normal");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            p.setChromaticSource (0);
            setParameter (p, NzDrumProcessor::paramId (0, "mute"), 1.0f);
            expectEquals (soundLength (p, 5), 0);

            setParameter (p, NzDrumProcessor::paramId (0, "mute"), 0.0f);
            p.setChromaticSource (-1);
            expectEquals (soundLength (p, 5), 0, "pad 6 is empty and should be silent");
            expectGreaterThan (soundLength (p, 0), 0);
        }

        beginTest ("A chromatic note doesn't cut the source pad that was already playing");
        {
            NzDrumProcessor p;
            prepare (p, tone);

            juce::MidiBuffer first;
            first.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 0);
            const float alone = peak (runBlock (p, first), 0, blockSize);

            p.setChromaticSource (0);
            juce::MidiBuffer second;
            second.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + NzDrumProcessor::chromaticRootPad, 1.0f), 0);
            const float together = peak (runBlock (p, second), 0, blockSize);

            // Both voices read the same waveform, offset in time: if the first one
            // were cut, the peak would be that of a single voice.
            expectGreaterThan (together, alone * 1.05f);
        }

        folder.deleteRecursively();
    }

private:
    static void prepare (NzDrumProcessor& p, const juce::File& tone)
    {
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);
        p.loadSample (0, tone);
        waitUntilLoaded (p, 0);
        runBlock (p);   // the audio thread picks up the sample
    }

    // Plays the pad by MIDI and counts how many samples it sounds for.
    int soundLength (NzDrumProcessor& p, int pad)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + pad, 1.0f), 0);

        int last = -1;

        for (int block = 0; block < 100; ++block)
        {
            const auto buffer = runBlock (p, block == 0 ? midi : juce::MidiBuffer());

            for (int i = 0; i < blockSize; ++i)
                if (std::abs (buffer.getSample (0, i)) > 1.0e-6f || std::abs (buffer.getSample (1, i)) > 1.0e-6f)
                    last = block * blockSize + i;
        }

        return last + 1;
    }
};

static ChromaticTests chromaticTests;

// ============================================================================
// Per-step semitone
// ============================================================================

class StepPitchTests final : public juce::UnitTest
{
public:
    StepPitchTests() : juce::UnitTest ("Per-step semitone", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-steppitch");
        folder.createDirectory();
        const auto tone = writeTestWav (folder, "tone.wav", 4000);

        beginTest ("It is kept within the 16 Levels range and turning the step off resets it to 0");
        {
            NzDrumProcessor p;
            p.setStep (0, 2, 3, StepLevel::normal);
            p.setStepPitch (0, 2, 3, 5);
            expectEquals (p.getStepPitch (0, 2, 3), 5);
            p.setStepPitch (0, 2, 3, 20);
            expectEquals (p.getStepPitch (0, 2, 3), NzDrumProcessor::maxStepPitch);
            p.setStepPitch (0, 2, 3, -20);
            expectEquals (p.getStepPitch (0, 2, 3), NzDrumProcessor::minStepPitch);
            p.setStep (0, 2, 3, StepLevel::off);
            expectEquals (p.getStepPitch (0, 2, 3), 0);
        }

        beginTest ("A step at -8 sounds like pad 1 in 16 Levels");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            const int plain = stepSoundLength (p, 0);

            p.setStepPitch (0, 0, 0, -8);
            const int low = stepSoundLength (p, 0);
            expectWithinAbsoluteError ((double) low / plain, std::exp2 (8.0 / 12.0), 0.01);
        }

        beginTest ("Copy, paste, clear and save carry the semitone");
        {
            NzDrumProcessor p;
            p.setStep (1, 4, 7, StepLevel::accent);
            p.setStepPitch (1, 4, 7, -3);
            p.setStep (1, 4, 8, StepLevel::low);
            p.setStepPitch (1, 4, 8, 7);

            p.copyPattern (1);
            p.pastePattern (2);
            expectEquals (p.getStepPitch (2, 4, 7), -3);
            expectEquals (p.getStepPitch (2, 4, 8), 7);

            juce::MemoryBlock saved;
            p.getStateInformation (saved);

            p.clearPattern (2);
            expectEquals (p.getStepPitch (2, 4, 7), 0);

            NzDrumProcessor restored;
            restored.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (restored.getStepPitch (1, 4, 7), -3);
            expectEquals (restored.getStepPitch (2, 4, 8), 7);
            expectEquals (restored.getStepPitch (0, 0, 0), 0);
            expect (restored.getStep (1, 4, 7) == StepLevel::accent);
        }

        folder.deleteRecursively();
    }

private:
    static void prepare (NzDrumProcessor& p, const juce::File& tone)
    {
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);
        p.loadSample (0, tone);
        waitUntilLoaded (p, 0);
        runBlock (p);
    }

    // Turns on only step 1 of the pad, plays from quarter note 0 with the host
    // and counts how many samples it sounds for (the pattern doesn't restart
    // until sample 96000, well after the window).
    int stepSoundLength (NzDrumProcessor& p, int pad)
    {
        p.setStep (0, pad, 0, StepLevel::accent);

        FakePlayHead head;
        p.setPlayHead (&head);

        int last = -1;

        for (int block = 0; block < 20; ++block)
        {
            head.ppq = (double) (block * blockSize) / testSampleRate * (head.bpm / 60.0);
            const auto buffer = runBlock (p);

            for (int i = 0; i < blockSize; ++i)
                if (std::abs (buffer.getSample (0, i)) > 1.0e-6f || std::abs (buffer.getSample (1, i)) > 1.0e-6f)
                    last = block * blockSize + i;
        }

        head.playing = false;
        runBlock (p);
        p.setPlayHead (nullptr);
        return last + 1;
    }
};

static StepPitchTests stepPitchTests;

// ============================================================================
// Live recording
// ============================================================================

class RecordingTests final : public juce::UnitTest
{
public:
    RecordingTests() : juce::UnitTest ("Live recording", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-recording");
        folder.createDirectory();
        const auto tone = writeTestWav (folder, "tone.wav", 2000);

        // At 120 BPM and 48 kHz a sixteenth lasts 6000 samples: step 2
        // (index 1) falls on sample 6000.
        constexpr int stepSamples = 6000;

        beginTest ("Without Rec, playing writes nothing");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            const auto before = p.getTriggerCount (0);
            play (p, stepSamples + 3 * blockSize, { { stepSamples - 100, 0, 0.8f } });
            expect (p.getStep (0, 0, 1) == StepLevel::off);
            expectEquals ((int) (p.getTriggerCount (0) - before), 1, "the hit must still sound");
        }

        beginTest ("With Rec, the hit lands on the nearest step with the level of its velocity");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            p.setRecording (true);
            play (p, 30 * blockSize, { { stepSamples - 100, 0, 1.0f }, { 2 * stepSamples + 2500, 3, 0.4f } });
            expect (p.getStep (0, 0, 1) == StepLevel::accent);
            expect (p.getStep (0, 3, 2) == StepLevel::low);
            expect (p.getStep (0, 3, 3) == StepLevel::off);
        }

        beginTest ("In 16 Levels the source pad is recorded with the semitone");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            p.setChromaticSource (0);
            p.setRecording (true);
            play (p, 3 * blockSize, { { 10, NzDrumProcessor::chromaticRootPad + 3, 0.8f } });
            expect (p.getStep (0, 0, 0) == StepLevel::normal);
            expectEquals (p.getStepPitch (0, 0, 0), 3);
            expect (p.getStep (0, NzDrumProcessor::chromaticRootPad + 3, 0) == StepLevel::off);
        }

        beginTest ("A hit before its step is not repeated (same block and next block)");
        {
            for (const int hitAt : { stepSamples - 100, stepSamples - 1500 })
            {
                NzDrumProcessor p;
                prepare (p, tone);
                p.setPatternLength (0, 4);
                p.setRecording (true);
                const auto before = p.getTriggerCount (0);
                play (p, stepSamples + 3 * blockSize, { { hitAt, 0, 0.8f } });
                expectEquals ((int) (p.getTriggerCount (0) - before), 1, "sono dos veces: " + juce::String (hitAt));
                expect (p.getStep (0, 0, 1) == StepLevel::normal);
            }
        }

        beginTest ("Overdub: replaces the step and plays it once on the next pass");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            p.setPatternLength (0, 4);
            p.setStep (0, 0, 1, StepLevel::accent);
            p.setStepPitch (0, 0, 1, 5);
            p.setRecording (true);
            const auto before = p.getTriggerCount (0);

            // A whole pass (4 steps = 24000 samples) and step 2 of the
            // next one.
            play (p, 4 * stepSamples + stepSamples + 3 * blockSize, { { stepSamples - 100, 0, 0.4f } });

            expect (p.getStep (0, 0, 1) == StepLevel::low);
            expectEquals (p.getStepPitch (0, 0, 1), 0);
            expectEquals ((int) (p.getTriggerCount (0) - before), 2, "the recorded hit + the next pass");
        }

        beginTest ("Without Rec, a hit on the same sample as a step is the one that plays");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            p.setStep (0, 0, 0, StepLevel::accent);   // 1.0

            FakePlayHead head;
            p.setPlayHead (&head);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 0.4f), 0);
            const float level = peak (runBlock (p, midi), 0, blockSize);
            p.setPlayHead (nullptr);

            // The sample peaks at 0.5: at 0.4 it gives 0.2, with the accent 0.5.
            expectWithinAbsoluteError (level, 0.2f, 0.03f);
        }

        beginTest ("A hit recorded before a pattern switch doesn't skip steps of the new pattern");
        {
            NzDrumProcessor p;
            prepare (p, tone);
            p.setPatternLength (0, 4);
            p.setStep (1, 0, 0, StepLevel::normal);
            p.setRecording (true);

            FakePlayHead head;
            p.setPlayHead (&head);
            const auto before = p.getTriggerCount (0);
            const int hitAt = 4 * stepSamples - 1500;   // falls on step 1 of the next pass

            for (int start = 0; start < 4 * stepSamples + 3 * blockSize; start += blockSize)
            {
                head.ppq = (double) start / testSampleRate * (head.bpm / 60.0);
                juce::MidiBuffer midi;

                if (hitAt >= start && hitAt < start + blockSize)
                    midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 0.8f), hitAt - start);

                runBlock (p, midi);

                if (start == 0)
                    p.selectPattern (1);   // takes effect when pattern 1 ends
            }

            p.setPlayHead (nullptr);
            expectEquals (p.getPlayingPattern(), 1);
            expectEquals ((int) (p.getTriggerCount (0) - before), 2, "the live hit + step 1 of the new pattern");
        }

        folder.deleteRecursively();
    }

private:
    struct Hit { int sample; int pad; float velocity; };

    static void prepare (NzDrumProcessor& p, const juce::File& tone)
    {
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);
        p.loadSample (0, tone);
        waitUntilLoaded (p, 0);
        runBlock (p);
    }

    // Plays from quarter note 0 for `totalSamples`, with MIDI notes at the
    // given absolute samples.
    static void play (NzDrumProcessor& p, int totalSamples, std::initializer_list<Hit> hits)
    {
        FakePlayHead head;
        p.setPlayHead (&head);

        for (int start = 0; start < totalSamples; start += blockSize)
        {
            head.ppq = (double) start / testSampleRate * (head.bpm / 60.0);
            juce::MidiBuffer midi;

            for (const auto& hit : hits)
                if (hit.sample >= start && hit.sample < start + blockSize)
                    midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + hit.pad, hit.velocity),
                                   hit.sample - start);

            runBlock (p, midi);
        }

        head.playing = false;
        runBlock (p);
        p.setPlayHead (nullptr);
    }
};

static RecordingTests recordingTests;

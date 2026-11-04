// Pruebas del motor de audio sin DAW: cargan samples reales desde disco,
// tocan notas MIDI y revisan el audio que sale.
// Correr con: cmake --build build --target NzDrumTests && ./build/NzDrumTests_artefacts/Release/NzDrumTests

#include "../Source/PluginProcessor.h"
#include "../Source/StepSequencerComponent.h"

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int blockSize = 512;

    // Escribe un WAV mono de prueba: una senoidal corta.
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

    // Un host de mentira: dice tempo, play y en qué negra empieza el bloque.
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
    EngineTests() : juce::UnitTest ("Motor", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-tests");
        folder.createDirectory();
        const auto kick1 = writeTestWav (folder, "kick1.wav", 4000);
        const auto kick2 = writeTestWav (folder, "kick2.wav", 4000);
        const auto kick10 = writeTestWav (folder, "kick10.wav", 4000);
        folder.getChildFile ("notas.txt").replaceWithText ("no soy audio");

        NzDrumProcessor p;
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);

        beginTest ("Un pad vacio no suena");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 0);
            expectEquals (peak (runBlock (p, midi), 0, blockSize), 0.0f);
        }

        beginTest ("Carga en segundo plano");
        {
            p.loadSample (0, kick1);
            expect (waitUntilLoaded (p, 0), "la carga no termino");
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick1.wav"));
        }

        beginTest ("El tail informa la duracion del sample mas largo");
        {
            expectWithinAbsoluteError (p.getTailLengthSeconds(), 4000.0 / testSampleRate, 1.0e-6);
        }

        beginTest ("La nota MIDI suena en la muestra exacta");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 100);
            const auto out = runBlock (p, midi);
            expectEquals (peak (out, 0, 101), 0.0f, "sono antes de la nota");
            expectGreaterThan (peak (out, 101, blockSize), 0.1f, "no sono despues de la nota");
        }

        beginTest ("La voz termina cuando se acaba el sample");
        {
            for (int i = 0; i < 10; ++i)
                runBlock (p);
            expectEquals (peak (runBlock (p), 0, blockSize), 0.0f);
        }

        beginTest ("Notas fuera de rango se ignoran");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote - 1, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + 16, 1.0f), 0);
            expectEquals (peak (runBlock (p, midi), 0, blockSize), 0.0f);
        }

        beginTest ("Click en la UI dispara el pad");
        {
            const auto before = p.getTriggerCount (0);
            p.triggerFromUi (0, 0.8f);
            expectGreaterThan (peak (runBlock (p), 0, blockSize), 0.1f);
            expectEquals ((int) p.getTriggerCount (0), (int) before + 1);
        }

        beginTest ("Siguiente recorre la carpeta en orden natural e ignora lo que no es audio");
        {
            p.stepSample (0, +1);
            p.stepSample (0, +1);   // dos clicks seguidos: cada uno parte del anterior
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));

            p.stepSample (0, +1);   // da la vuelta
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick1.wav"));

            p.stepSample (0, -1);
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));
        }

        beginTest ("Un archivo invalido no cambia el pad");
        {
            p.loadSample (0, folder.getChildFile ("notas.txt"));
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));

            // Un WAV valido pero sin audio tampoco se publica.
            auto emptyFolder = folder.getChildFile ("vacio");
            emptyFolder.createDirectory();
            p.loadSample (0, writeTestWav (emptyFolder, "silencio.wav", 0));
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick10.wav"));
        }

        beginTest ("Cambiar de sample mientras suena no rompe nada");
        {
            p.triggerFromUi (0, 1.0f);
            runBlock (p);
            p.loadSample (0, kick2);
            expect (waitUntilLoaded (p, 0));
            runBlock (p);   // acá se toma el sample nuevo y se corta la voz vieja
            expectEquals (peak (runBlock (p), 0, blockSize), 0.0f);
        }

        runSequencerTests (kick1);
        runClickTests();

        folder.deleteRecursively();
    }

    void runSequencerTests (const juce::File& sampleFile)
    {
        // A 120 BPM y 48 kHz una negra dura 24000 muestras y un paso, 6000.
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

        beginTest ("Sin pasos activos el secuenciador no suena");
        {
            expectEquals (peak (blockStartingAt (0.0), 0, blockSize), 0.0f);
        }

        beginTest ("Un paso suena en la muestra exacta dentro del bloque");
        {
            p.setStep (0, 1, StepLevel::normal);   // el paso 2 cae en la negra 0.25
            const auto out = blockStartingAt (0.25 - 100.0 / samplesPerQuarter);
            expectEquals (peak (out, 0, 100), 0.0f, "sono antes del paso");
            expectGreaterThan (peak (out, 100, blockSize), 0.1f, "no sono en el paso");
        }

        beginTest ("Un paso justo en el borde suena una sola vez");
        {
            p.setStep (0, 1, StepLevel::off);
            p.setStep (0, 2, StepLevel::normal);   // negra 0.5
            const int before = (int) p.getTriggerCount (0);
            blockStartingAt (0.5 - blockSize / samplesPerQuarter);   // termina justo en el paso
            blockStartingAt (0.5);                                    // empieza justo en el paso
            expectEquals ((int) p.getTriggerCount (0) - before, 1);
            p.setStep (0, 2, StepLevel::off);
        }

        beginTest ("Con el host detenido no suena y no hay paso actual");
        {
            p.setStep (0, 0, StepLevel::normal);
            host.playing = false;
            for (int i = 0; i < 20; ++i) runBlock (p);
            expectEquals (peak (blockStartingAt (0.0), 0, blockSize), 0.0f);
            expectEquals (p.getPlayingStep(), -1);
            host.playing = true;
        }

        beginTest ("El patron da la vuelta despues del paso 16");
        {
            for (int i = 0; i < 20; ++i) runBlock (p);   // dejamos terminar la voz anterior
            host.playing = false; runBlock (p); host.playing = true;
            const int before = (int) p.getTriggerCount (0);
            blockStartingAt (4.0);   // negra 4 = paso 17 = paso 1 del patron
            expectEquals ((int) p.getTriggerCount (0) - before, 1);
            expectEquals (p.getPlayingStep(), 0);
        }

        beginTest ("El acento suena mas fuerte que el bajo, en la proporcion de sus niveles");
        {
            const auto peakFor = [&] (StepLevel level)
            {
                p.setStep (0, 0, level);
                for (int i = 0; i < 20; ++i) { host.playing = false; runBlock (p); }
                host.playing = true;
                return peak (blockStartingAt (0.0), 0, blockSize);
            };

            const float low = peakFor (StepLevel::low);
            const float accent = peakFor (StepLevel::accent);
            expectWithinAbsoluteError (accent / low,
                                       NzDrumProcessor::gainFor (StepLevel::accent) / NzDrumProcessor::gainFor (StepLevel::low),
                                       0.01f);
            p.setStep (0, 0, StepLevel::off);
        }

        p.setPlayHead (nullptr);
    }

    void runClickTests()
    {
        using Seq = StepSequencerComponent;
        const juce::ModifierKeys none, shift (juce::ModifierKeys::shiftModifier), alt (juce::ModifierKeys::altModifier);

        beginTest ("Click: normal, Shift: acento, Option: bajo; repetir apaga");
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

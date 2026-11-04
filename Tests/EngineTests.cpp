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
    EngineTests() : juce::UnitTest ("Motor", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-tests");
        folder.createDirectory();
        const auto kick1 = writeTestWav (folder, "kick1.wav", 4000);
        const auto kick2 = writeTestWav (folder, "kick2.wav", 4000);
        const auto kick10 = writeTestWav (folder, "kick10.wav", 4000);
        folder.getChildFile ("notas.txt").replaceWithText ("no soy audio");
        folder.getChildFile ("kick1b.wav").replaceWithText ("extension de audio, contenido roto");

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

        beginTest ("Siguiente salta un archivo roto en vez de quedarse trabado");
        {
            p.loadSample (0, kick1);
            expect (waitUntilLoaded (p, 0));
            p.stepSample (0, +1);   // el vecino es kick1b.wav, que no se puede leer
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick2.wav"));

            p.stepSample (0, -1);   // y para atras tambien lo salta
            expect (waitUntilLoaded (p, 0));
            expectEquals (p.getPadFile (0).getFileName(), juce::String ("kick1.wav"));
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
            p.setStep (0, 0, 1, StepLevel::normal);   // el paso 2 cae en la negra 0.25
            const auto out = blockStartingAt (0.25 - 100.0 / samplesPerQuarter);
            expectEquals (peak (out, 0, 100), 0.0f, "sono antes del paso");
            expectGreaterThan (peak (out, 100, blockSize), 0.1f, "no sono en el paso");
        }

        beginTest ("Un paso justo en el borde suena una sola vez");
        {
            p.setStep (0, 0, 1, StepLevel::off);
            p.setStep (0, 0, 2, StepLevel::normal);   // negra 0.5
            const int before = (int) p.getTriggerCount (0);
            blockStartingAt (0.5 - blockSize / samplesPerQuarter);   // termina justo en el paso
            blockStartingAt (0.5);                                    // empieza justo en el paso
            expectEquals ((int) p.getTriggerCount (0) - before, 1);
            p.setStep (0, 0, 2, StepLevel::off);
        }

        beginTest ("Con el host detenido no suena y no hay paso actual");
        {
            p.setStep (0, 0, 0, StepLevel::normal);
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

// Ajustes por pad: volumen, pan, pitch, inicio/fin, decay, mute y solo.
class PadSettingsTests final : public juce::UnitTest
{
public:
    PadSettingsTests() : juce::UnitTest ("Ajustes por pad", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-settings");
        folder.createDirectory();
        const auto tone = writeTestWav (folder, "tono.wav", 4000);

        NzDrumProcessor p;
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);

        for (int pad : { 0, 1 })
        {
            p.loadSample (pad, tone);
            expect (waitUntilLoaded (p, pad), "la carga no termino");
        }

        // Lo de base, sin tocar nada.
        const auto reference = hit (p, 0);
        const float referencePeak = peak (reference, 0, reference.getNumSamples());
        const int referenceLength = soundingLength (reference);

        beginTest ("Volumen: -6 dB da la mitad de amplitud");
        {
            set (p, 0, "level", -6.0f);
            const auto out = hit (p, 0);
            expectWithinAbsoluteError (peak (out, 0, out.getNumSamples()) / referencePeak, 0.501f, 0.01f);
            set (p, 0, "level", 0.0f);
        }

        beginTest ("Volumen al minimo es silencio");
        {
            set (p, 0, "level", NzDrumProcessor::minLevelDb);
            const auto out = hit (p, 0);
            expectEquals (peak (out, 0, out.getNumSamples()), 0.0f);
            set (p, 0, "level", 0.0f);
        }

        beginTest ("Pan a la izquierda apaga el canal derecho");
        {
            set (p, 0, "pan", -1.0f);
            const auto out = hit (p, 0);
            expectWithinAbsoluteError (out.getMagnitude (0, 0, out.getNumSamples()), referencePeak, 1.0e-3f);
            expectEquals (out.getMagnitude (1, 0, out.getNumSamples()), 0.0f);
            set (p, 0, "pan", 0.0f);
        }

        beginTest ("Pitch +12 suena una octava arriba: dura la mitad");
        {
            set (p, 0, "pitch", 12.0f);
            const int length = soundingLength (hit (p, 0));
            expectWithinAbsoluteError (length, referenceLength / 2, 4);
            set (p, 0, "pitch", 0.0f);
        }

        beginTest ("Inicio y fin recortan el sample, aunque esten invertidos");
        {
            set (p, 0, "start", 0.5f);
            set (p, 0, "end", 0.25f);
            const int length = soundingLength (hit (p, 0));
            expectWithinAbsoluteError (length, referenceLength / 4, 4);
            set (p, 0, "start", 0.0f);
            set (p, 0, "end", 1.0f);
        }

        beginTest ("Inicio y fin al final del sample no leen fuera del archivo");
        {
            set (p, 0, "start", 1.0f);
            set (p, 0, "end", 1.0f);
            const auto out = hit (p, 0);
            expectLessOrEqual (soundingLength (out), 2);
            set (p, 0, "start", 0.0f);
            set (p, 0, "end", 1.0f);
        }

        beginTest ("Un decay corto baja el final del sonido");
        {
            set (p, 0, "decay", NzDrumProcessor::minDecaySeconds);
            const auto out = hit (p, 0);
            // 0,02 s de decay: a las 2000 muestras (42 ms) queda cerca de e^-2.
            expectLessThan (peak (out, 2000, 3000), 0.2f * peak (reference, 2000, 3000));
            set (p, 0, "decay", NzDrumProcessor::maxDecaySeconds);
        }

        beginTest ("Un pad muteado no suena, ni por MIDI ni por click");
        {
            set (p, 0, "mute", 1.0f);
            expect (! p.isPadAudible (0));
            const auto midiHit = hit (p, 0);
            expectEquals (peak (midiHit, 0, midiHit.getNumSamples()), 0.0f, "sono por MIDI");

            p.triggerFromUi (0, 1.0f);
            const auto clickHit = render (p, {}, 4800);
            expectEquals (peak (clickHit, 0, clickHit.getNumSamples()), 0.0f, "sono por click");
            set (p, 0, "mute", 0.0f);
        }

        beginTest ("Mutear mientras suena baja el sonido de a poco, sin cortarlo en seco");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote, 1.0f), 0);
            runBlock (p, midi);
            set (p, 0, "mute", 1.0f);

            const auto out = runBlock (p);
            const int fadeSamples = (int) (0.005 * testSampleRate);
            expectGreaterThan (peak (out, 0, 40), 0.05f, "corto en seco");
            expectEquals (peak (out, fadeSamples + 1, blockSize), 0.0f, "no termino de bajar");

            set (p, 0, "mute", 0.0f);
            render (p, {}, 4800);
        }

        beginTest ("Con un pad en solo, los demas no suenan");
        {
            set (p, 1, "solo", 1.0f);
            expect (p.isPadAudible (1));
            expect (! p.isPadAudible (0));

            const auto other = hit (p, 0);
            expectEquals (peak (other, 0, other.getNumSamples()), 0.0f, "sono un pad fuera del solo");

            const auto soloed = hit (p, 1);
            expectWithinAbsoluteError (peak (soloed, 0, soloed.getNumSamples()), referencePeak, 1.0e-3f);
        }

        beginTest ("Un pad muteado no suena aunque este en solo");
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

    // Procesa varios bloques seguidos y los junta en un solo buffer.
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

    // Dispara el pad por MIDI al principio y devuelve 100 ms de audio.
    static juce::AudioBuffer<float> hit (NzDrumProcessor& p, int pad)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, NzDrumProcessor::firstMidiNote + pad, 1.0f), 0);
        return render (p, midi, 4800);
    }

    // Hasta qué muestra hay sonido.
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

// Banco de patrones, largo por patrón y swing.
class PatternTests final : public juce::UnitTest
{
public:
    PatternTests() : juce::UnitTest ("Patrones y swing", "NzDrum") {}

    void runTest() override
    {
        auto folder = juce::File::createTempFile ("nzdrum-patterns");
        folder.createDirectory();
        const auto blip = writeTestWav (folder, "blip.wav", 200);   // corto: termina antes del paso siguiente

        NzDrumProcessor p;
        FakePlayHead host;
        p.setPlayHead (&host);
        p.setPlayConfigDetails (0, 2, testSampleRate, blockSize);
        p.prepareToPlay (testSampleRate, blockSize);

        // Pad 1 solo a la izquierda y pad 2 solo a la derecha, para saber cuál sonó.
        for (int pad : { 0, 1 })
        {
            p.loadSample (pad, blip);
            expect (waitUntilLoaded (p, pad));
        }

        setParameter (p, NzDrumProcessor::paramId (0, "pan"), -1.0f);
        setParameter (p, NzDrumProcessor::paramId (1, "pan"), 1.0f);

        // A 120 BPM y 48 kHz: una negra = 24000 muestras, una semicorchea = 6000.
        const double samplesPerQuarter = testSampleRate * 60.0 / host.bpm;

        beginTest ("Los pasos son de cada patron");
        {
            p.setStep (1, 0, 3, StepLevel::accent);
            expect (p.getStep (1, 0, 3) == StepLevel::accent);
            expect (p.getStep (0, 0, 3) == StepLevel::off);
            p.setStep (1, 0, 3, StepLevel::off);
        }

        beginTest ("Solo se aceptan largos de 4, 8, 16 y 32 pasos");
        {
            expectEquals (p.getPatternLength (0), 16);
            p.setPatternLength (0, 12);
            expectEquals (p.getPatternLength (0), 16);
            p.setPatternLength (0, 32);
            expectEquals (p.getPatternLength (0), 32);
            p.setPatternLength (0, 16);
        }

        beginTest ("Un patron de 4 pasos se repite cada negra");
        {
            p.setStep (0, 0, 0, StepLevel::normal);
            p.setPatternLength (0, 4);
            const auto onsets = play (p, host, 0.0, 2.0);
            expect (onsets.left == std::vector<int> { 0, 24000 }, describe (onsets.left));
            p.setPatternLength (0, 16);
            p.setStep (0, 0, 0, StepLevel::off);
        }

        beginTest ("En un patron de 32 pasos, el paso 17 suena en la negra 4");
        {
            p.setPatternLength (0, 32);
            p.setStep (0, 0, 16, StepLevel::normal);
            const auto onsets = play (p, host, 0.0, 8.5);
            expect (onsets.left == std::vector<int> { 4 * 24000 }, describe (onsets.left));
            p.setStep (0, 0, 16, StepLevel::off);
            p.setPatternLength (0, 16);
        }

        beginTest ("Swing: corre las semicorcheas pares segun el porcentaje");
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

        beginTest ("Con swing, un paso corrido que cae en el bloque siguiente suena igual");
        {
            setParameter (p, "swing", 75.0f);
            p.setStep (0, 0, 1, StepLevel::normal);   // sin swing caeria en 6000; con 75 %, en 9000
            const auto onsets = play (p, host, 7000.0 / samplesPerQuarter, 0.25);
            expect (onsets.left == std::vector<int> { 2000 }, describe (onsets.left));
            setParameter (p, "swing", 50.0f);
            p.setStep (0, 0, 1, StepLevel::off);
        }

        beginTest ("Al cambiar de patron sonando, el cambio espera al final del patron");
        {
            p.setStep (0, 0, 0, StepLevel::normal);   // patron 1: pad 1 en el paso 1
            p.setStep (1, 1, 0, StepLevel::normal);   // patron 2: pad 2 en el paso 1
            p.selectPattern (0);
            stop (p, host);

            Onsets onsets;
            playInto (onsets, p, host, 0.0, 1.0, 0);
            p.selectPattern (1);
            expectEquals (p.getPlayingPattern(), 0, "cambio antes de tiempo");
            playInto (onsets, p, host, 1.0, 4.5, (int) (1.0 * samplesPerQuarter));

            expect (onsets.left == std::vector<int> { 0 }, "pad 1: " + describe (onsets.left));
            expect (onsets.right == std::vector<int> { 4 * 24000 }, "pad 2: " + describe (onsets.right));
            expectEquals (p.getPlayingPattern(), 1);
        }

        beginTest ("Con el host detenido, el cambio de patron es inmediato");
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

    // Toca bloques seguidos desde `fromPpq` hasta `toPpq` y anota en qué
    // muestra (contada desde `firstSample`) arranca cada golpe en cada canal.
    // El sample de prueba empieza en sin(0) = 0, así que el golpe arranca una
    // muestra antes del primer valor distinto de cero.
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
                    if (std::abs (block.getSample (ch, i)) < 1.0e-9f)
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

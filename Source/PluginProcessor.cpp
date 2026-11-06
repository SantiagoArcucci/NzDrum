#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

namespace
{
    // Tope de duración por sample, para que un archivo largo cargado por
    // error no se coma la memoria. Para baterías sobra.
    constexpr double maxSampleSeconds = 30.0;
}

juce::String NzDrumProcessor::paramId (int pad, const char* name)
{
    return "pad" + juce::String (pad + 1) + "_" + name;
}

bool NzDrumProcessor::isValidPatternLength (int length) noexcept
{
    return length == 4 || length == 8 || length == 16 || length == 32;
}

namespace
{
    // El número que haya en un texto escrito a mano ("-6 dB", "250ms",
    // "1,5 st"): se ignoran las unidades y se acepta coma decimal.
    float numberIn (const juce::String& text)
    {
        return text.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getFloatValue();
    }

    // Evita el "-0.00": cualquier valor que se redondee a cero se muestra 0.
    juce::String fixed (float value, int decimals)
    {
        const auto scale = std::pow (10.0f, (float) decimals);
        return juce::String (std::abs (value) * scale < 0.5f ? 0.0f : value, decimals);
    }
}

juce::String NzDrumProcessor::levelToText (float db)
{
    return db <= minLevelDb ? juce::String ("-inf dB") : fixed (db, 1) + " dB";
}

float NzDrumProcessor::textToLevel (const juce::String& text)
{
    return text.containsIgnoreCase ("inf") ? minLevelDb : numberIn (text);
}

juce::String NzDrumProcessor::panToText (float pan)
{
    const int percent = juce::roundToInt (std::abs (pan) * 100.0f);
    return percent == 0 ? juce::String ("C") : juce::String (percent) + (pan < 0.0f ? " L" : " R");
}

float NzDrumProcessor::textToPan (const juce::String& text)
{
    // "30 L" / "30 R", o un número solo: negativo es izquierda ("-30").
    const float number = numberIn (text) / 100.0f;
    const float amount = juce::jlimit (0.0f, 1.0f, std::abs (number));

    if (text.containsIgnoreCase ("L"))
        return -amount;

    if (text.containsIgnoreCase ("R"))
        return amount;

    return number < 0.0f ? -amount : amount;
}

juce::String NzDrumProcessor::pitchToText (float semitones)
{
    const auto text = fixed (semitones, 2);
    return (text.getFloatValue() > 0.0f ? "+" : "") + text + " st";
}

float NzDrumProcessor::textToPitch (const juce::String& text)
{
    return numberIn (text);
}

juce::String NzDrumProcessor::decayToText (float seconds)
{
    if (seconds >= maxDecaySeconds)
        return "off";

    // Debajo de un segundo, en milisegundos: "250 ms" se lee mejor que "0.25 s".
    if (seconds < 0.9995f)
        return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";

    return juce::String (seconds, 2) + " s";
}

float NzDrumProcessor::textToDecay (const juce::String& text)
{
    if (text.containsIgnoreCase ("off"))
        return maxDecaySeconds;

    const float number = numberIn (text);
    return text.containsIgnoreCase ("ms") ? number / 1000.0f : number;
}

juce::AudioProcessorValueTreeState::ParameterLayout NzDrumProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "swing", 1 }, "Swing",
                                                             juce::NormalisableRange<float> (minSwing, maxSwing, 0.1f), minSwing,
                                                             juce::AudioParameterFloatAttributes().withLabel ("%")
                                                                 .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })));

    // El patrón que suena, de 1 a 16. Automatizándolo en el DAW se encadenan
    // patrones; el cambio espera a que termine el patrón actual.
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "pattern", 1 },
                                                           juce::String::fromUTF8 ("Patrón"), 1, numPatterns, 1));

    // El 1 de ParameterID es la versión: si algún día cambia un parámetro,
    // los hosts lo usan para no confundirlo con el anterior.
    const auto id = [] (int pad, const char* name) { return juce::ParameterID { paramId (pad, name), 1 }; };
    const auto label = [] (int pad, const char* name) { return juce::String::fromUTF8 ("Pad ") + juce::String (pad + 1) + " " + juce::String::fromUTF8 (name); };

    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;

    for (int pad = 0; pad < numPads; ++pad)
    {
        layout.add (std::make_unique<Float> (id (pad, "level"), label (pad, "Volumen"),
                                             juce::NormalisableRange<float> (minLevelDb, maxLevelDb, 0.1f, 2.0f), 0.0f,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return levelToText (v); })
                                                         .withValueFromStringFunction (textToLevel)));

        layout.add (std::make_unique<Float> (id (pad, "pan"), label (pad, "Pan"),
                                             juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return panToText (v); })
                                                         .withValueFromStringFunction (textToPan)));

        layout.add (std::make_unique<Float> (id (pad, "pitch"), label (pad, "Pitch"),
                                             juce::NormalisableRange<float> (-maxPitchSemitones, maxPitchSemitones, 0.01f), 0.0f,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return pitchToText (v); })
                                                         .withValueFromStringFunction (textToPitch)));

        // Skew < 1: más resolución en los decays cortos, que es donde se nota.
        layout.add (std::make_unique<Float> (id (pad, "decay"), label (pad, "Decay"),
                                             juce::NormalisableRange<float> (minDecaySeconds, maxDecaySeconds, 0.001f, 0.35f), maxDecaySeconds,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return decayToText (v); })
                                                         .withValueFromStringFunction (textToDecay)));

        layout.add (std::make_unique<Float> (id (pad, "start"), label (pad, "Inicio"),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return fixed (v * 100.0f, 1) + " %"; })
                                                         .withValueFromStringFunction ([] (const juce::String& t) { return juce::jlimit (0.0f, 1.0f, numberIn (t) / 100.0f); })));

        layout.add (std::make_unique<Float> (id (pad, "end"), label (pad, "Fin"),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 1.0f,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return fixed (v * 100.0f, 1) + " %"; })
                                                         .withValueFromStringFunction ([] (const juce::String& t) { return juce::jlimit (0.0f, 1.0f, numberIn (t) / 100.0f); })));

        layout.add (std::make_unique<juce::AudioParameterBool> (id (pad, "mute"), label (pad, "Mute"), false));
        layout.add (std::make_unique<juce::AudioParameterBool> (id (pad, "solo"), label (pad, "Solo"), false));
    }

    return layout;
}

NzDrumProcessor::NzDrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "NzDrum", createParameterLayout())
{
    for (int pad = 0; pad < numPads; ++pad)
    {
        auto& p = padParameters[(size_t) pad];
        p.level = parameters.getRawParameterValue (paramId (pad, "level"));
        p.pan = parameters.getRawParameterValue (paramId (pad, "pan"));
        p.pitch = parameters.getRawParameterValue (paramId (pad, "pitch"));
        p.decay = parameters.getRawParameterValue (paramId (pad, "decay"));
        p.start = parameters.getRawParameterValue (paramId (pad, "start"));
        p.end = parameters.getRawParameterValue (paramId (pad, "end"));
        p.mute = parameters.getRawParameterValue (paramId (pad, "mute"));
        p.solo = parameters.getRawParameterValue (paramId (pad, "solo"));
    }

    swingParameter = parameters.getRawParameterValue ("swing");
    patternParameter = parameters.getRawParameterValue ("pattern");
    patternParameterObject = parameters.getParameter ("pattern");

    for (auto& length : patternLengths)
        length.store (defaultPatternLength, std::memory_order_relaxed);

    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg y, en Mac, MP3/M4A vía CoreAudio
    startTimerHz (10);                // recolector de samples descartados
}

NzDrumProcessor::~NzDrumProcessor()
{
    stopTimer();
    // Esperamos sin límite a que termine la carga en curso: cada carga está
    // acotada (máximo 30 s de audio), y cortarla antes dejaría al hilo de
    // carga escribiendo en memoria ya liberada.
    loaderPool.removeAllJobs (true, -1);
}

void NzDrumProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (int v = 0; v < numVoices; ++v)
    {
        voices[(size_t) v] = {};
        voices[(size_t) v].sourcePad = v % numPads;
    }
}

bool NzDrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

// ============================================================================
// Hilo de audio
// ============================================================================

void NzDrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Reglas del hilo de audio: sin new/malloc, sin locks, sin disco, sin logs.
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    numEvents = 0;

    juce::Optional<juce::AudioPlayHead::PositionInfo> position;

    if (auto* hostPlayHead = getPlayHead())
        position = hostPlayHead->getPosition();

    if (position.hasValue())
    {
        hostBpm.store (position->getBpm().orFallback (0.0), std::memory_order_relaxed);
        hostPpq.store (position->getPpqPosition().orFallback (0.0), std::memory_order_relaxed);
        hostPlaying.store (position->getIsPlaying(), std::memory_order_relaxed);
    }

    // 1. Tomamos los samples nuevos que haya dejado el hilo de carga. Si un pad
    //    cambió de sample, cortamos su voz: seguía leyendo el buffer viejo.
    for (int pad = 0; pad < numPads; ++pad)
    {
        bool changed = false;
        blockSamples[(size_t) pad] = slots[(size_t) pad].acquire (changed);

        if (changed)
            for (auto& voice : voices)
                if (voice.sourcePad == pad)
                    voice.playing = false;
    }

    // Se lee una vez por bloque, igual que mute y solo.
    const int chromatic = chromaticSource.load (std::memory_order_relaxed);
    const bool chromaticOn = juce::isPositiveAndBelow (chromatic, numPads);

    // 2. Juntamos todos los disparos del bloque en una sola lista:
    //    clicks de la UI (al inicio del bloque), pasos del secuenciador y
    //    notas MIDI, cada uno con su muestra exacta.
    for (int pad = 0; pad < numPads; ++pad)
    {
        const float velocity = uiTriggers[(size_t) pad].exchange (0.0f, std::memory_order_acq_rel);

        if (velocity > 0.0f)
            addEvent (0, pad, velocity, chromaticOn);
    }

    if (position.hasValue() && position->getIsPlaying())
    {
        collectSequencerEvents (*position, numSamples);
    }
    else
    {
        // Detenido: el patrón pedido pasa a ser el actual de inmediato.
        sequencerRunning = false;
        currentPattern = getSelectedPattern();
        playingPattern.store (currentPattern, std::memory_order_relaxed);
        playingStep.store (-1, std::memory_order_relaxed);
    }

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const int pad = message.getNoteNumber() - firstMidiNote;

            if (pad >= 0 && pad < numPads)
                addEvent (juce::jlimit (0, numSamples, metadata.samplePosition), pad, message.getFloatVelocity(), chromaticOn);
        }
    }

    // Ordenamos por muestra con insertion sort: std::sort y std::stable_sort
    // pueden reservar memoria, y la lista es corta. Es estable, así que dos
    // disparos en la misma muestra mantienen el orden de llegada.
    for (int i = 1; i < numEvents; ++i)
    {
        const auto event = events[(size_t) i];
        int j = i - 1;

        for (; j >= 0 && events[(size_t) j].offset > event.offset; --j)
            events[(size_t) (j + 1)] = events[(size_t) j];

        events[(size_t) (j + 1)] = event;
    }

    // Mute y solo se leen una vez por bloque; el cambio se suaviza en renderVoices.
    for (int pad = 0; pad < numPads; ++pad)
        blockAudible[(size_t) pad] = isPadAudible (pad);

    // 3. Renderizamos hasta cada disparo, disparamos y seguimos.
    int cursor = 0;

    for (int i = 0; i < numEvents; ++i)
    {
        const auto& event = events[(size_t) i];
        renderVoices (buffer, cursor, event.offset);
        cursor = event.offset;
        // En cromático, el pad tocado elige el semitono y el que suena es la
        // fuente, con una voz propia para cada pad tocado.
        if (event.chromatic)
            startVoice (numPads + event.pad, chromatic, event.velocity, (float) chromaticOffsetFor (event.pad));
        else
            startVoice (event.pad, event.pad, event.velocity, (float) event.semitones);

        // El pad tocado destella (en cromático también, aunque suene otro).
        triggerCounts[(size_t) event.pad].fetch_add (1, std::memory_order_relaxed);
    }

    renderVoices (buffer, cursor, numSamples);
}

void NzDrumProcessor::addEvent (int offset, int pad, float velocity, bool chromatic, int semitones) noexcept
{
    if (numEvents < maxEventsPerBlock)
        events[(size_t) numEvents++] = { offset, pad, velocity, chromatic, semitones };
}

void NzDrumProcessor::collectSequencerEvents (const juce::AudioPlayHead::PositionInfo& position, int numSamples) noexcept
{
    const auto bpm = position.getBpm();
    const auto ppq = position.getPpqPosition();

    if (! bpm.hasValue() || ! ppq.hasValue() || *bpm <= 0.0)
    {
        sequencerRunning = false;
        playingStep.store (-1, std::memory_order_relaxed);
        return;
    }

    // El host nos dice en qué negra empieza el bloque (PPQ). Con el tempo
    // sabemos cuántas muestras dura una negra, y de ahí dónde cae cada paso.
    const double samplesPerQuarter = currentSampleRate * 60.0 / *bpm;
    const double blockStart = *ppq;
    const double blockEnd = blockStart + numSamples / samplesPerQuarter;

    // Si el transporte recién arranca o saltó (loop, click en la regla), nos
    // realineamos a la grilla del host: el paso 1 del patrón cae en los
    // compases. Un pedido de patrón pendiente se aplica en ese momento.
    constexpr double jumpTolerance = 0.01;   // negras

    if (! sequencerRunning || std::abs (blockStart - expectedBlockStart) > jumpTolerance)
    {
        currentPattern = getSelectedPattern();
        patternAnchor = 0;
    }

    sequencerRunning = true;
    expectedBlockStart = blockEnd;

    // Swing: la semicorchea impar (0-based) de cada par se corre. A 50% cae
    // en la mitad del par; a 75%, a tres cuartos.
    const float swing = juce::jlimit (minSwing, maxSwing, swingParameter->load (std::memory_order_relaxed));
    const double swingOffset = (swing / 100.0 - 0.5) * 2.0 * stepLengthPpq;

    const auto stepTime = [swingOffset] (juce::int64 index)
    {
        return (double) index * stepLengthPpq + ((index & 1) != 0 ? swingOffset : 0.0);
    };

    const auto positionInPattern = [this] (juce::int64 index)
    {
        const auto length = (juce::int64) patternLengths[(size_t) currentPattern].load (std::memory_order_relaxed);
        return (int) ((((index - patternAnchor) % length) + length) % length);
    };

    // Recorremos las semicorcheas que caen dentro del bloque, en orden. El
    // margen chico evita perder un paso que cae justo en el inicio por
    // redondeo; el siguiente bloque no lo repite porque el fin es excluyente.
    constexpr double epsilon = 1.0e-9;
    auto index = (juce::int64) std::floor (blockStart / stepLengthPpq) - 1;

    for (; (double) index * stepLengthPpq < blockEnd; ++index)
    {
        const double time = stepTime (index);

        if (time < blockStart - epsilon || time >= blockEnd)
            continue;

        // Al terminar el patrón (vuelve al paso 1) entra el pedido pendiente.
        if (positionInPattern (index) == 0)
        {
            const int requested = getSelectedPattern();

            if (requested != currentPattern)
            {
                currentPattern = requested;
                patternAnchor = index;
            }
        }

        const int step = positionInPattern (index);
        const int offset = juce::jlimit (0, numSamples - 1, (int) std::llround ((time - blockStart) * samplesPerQuarter));

        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto level = getStep (currentPattern, pad, step);

            if (level != StepLevel::off)
                addEvent (offset, pad, gainFor (level), false, getStepPitch (currentPattern, pad, step));
        }
    }

    // Para la UI: el último paso que empezó dentro del bloque (sin swing:
    // alcanza para iluminarlo).
    playingStep.store (positionInPattern ((juce::int64) std::floor (blockEnd / stepLengthPpq - epsilon)), std::memory_order_relaxed);
    playingPattern.store (currentPattern, std::memory_order_relaxed);
}

void NzDrumProcessor::startVoice (int voiceIndex, int pad, float velocity, float extraSemitones) noexcept
{
    const auto* sample = blockSamples[(size_t) pad];

    if (sample == nullptr || sample->audio.getNumSamples() < 2)
        return;

    // Los parámetros se leen una vez por disparo: mover una perilla afecta
    // al próximo golpe, no al que ya está sonando.
    const auto& params = padParameters[(size_t) pad];
    const auto lastIndex = (double) (sample->audio.getNumSamples() - 1);

    float start = params.start->load (std::memory_order_relaxed);
    float end = params.end->load (std::memory_order_relaxed);

    if (end < start)
        std::swap (start, end);

    auto& voice = voices[(size_t) voiceIndex];

    // Si no venía sonando nada, la voz arranca directo en el estado de mute
    // de su pad: no hay nada que suavizar y así el ataque queda intacto.
    if (! voice.playing)
        audibleGains[(size_t) voiceIndex] = blockAudible[(size_t) pad] ? 1.0f : 0.0f;

    voice.sourcePad = pad;

    // La interpolación lee la muestra siguiente, así que la voz tiene que
    // terminar a más tardar en la última muestra: el inicio queda al menos una
    // muestra antes del final, aunque el host ponga inicio = 1.
    voice.position = juce::jmin (start * lastIndex, lastIndex - 1.0);
    voice.endPosition = juce::jlimit (voice.position + 1.0, lastIndex, end * lastIndex);

    const float pitch = params.pitch->load (std::memory_order_relaxed) + extraSemitones;
    voice.increment = (sample->sampleRate / currentSampleRate) * std::exp2 (pitch / 12.0);

    const float levelDb = params.level->load (std::memory_order_relaxed);
    const float gain = velocity * (levelDb <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (levelDb));

    // Pan tipo "balance": al centro los dos canales quedan como estaban, y
    // hacia un lado se va bajando el otro.
    const float pan = params.pan->load (std::memory_order_relaxed);
    voice.gainLeft = gain * juce::jmin (1.0f, 1.0f - pan);
    voice.gainRight = gain * juce::jmin (1.0f, 1.0f + pan);

    const float decay = params.decay->load (std::memory_order_relaxed);
    voice.envelope = 1.0f;
    voice.envelopeCoefficient = decay >= maxDecaySeconds
                                    ? 1.0f
                                    : (float) std::exp (-1.0 / (decay * currentSampleRate));

    voice.playing = true;
}

void NzDrumProcessor::renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept
{
    if (startSample >= endSample)
        return;

    const int numOutputChannels = buffer.getNumChannels();

    // Un fundido de 2 ms al llegar al fin evita el "click" de cortar la onda
    // en un punto que no es cero, algo común al recortar con el marcador.
    const double fadeOutputSamples = 0.002 * currentSampleRate;
    constexpr float silence = 1.0e-4f;

    // Mute y solo bajan o suben el volumen del pad en 5 ms, como el fader de
    // una consola: cortar en seco haría un click.
    const float muteStep = (float) (1.0 / (0.005 * currentSampleRate));

    for (int v = 0; v < numVoices; ++v)
    {
        auto& voice = voices[(size_t) v];
        const auto* sample = blockSamples[(size_t) voice.sourcePad];

        auto& audibleGain = audibleGains[(size_t) v];
        const float audibleTarget = blockAudible[(size_t) voice.sourcePad] ? 1.0f : 0.0f;

        if (! voice.playing || sample == nullptr)
        {
            audibleGain = audibleTarget;   // sin voz no hay nada que suavizar
            continue;
        }

        const auto& source = sample->audio;
        const int sourceChannels = source.getNumChannels();

        for (int i = startSample; i < endSample; ++i)
        {
            const int index = (int) voice.position;

            if (voice.position >= voice.endPosition || voice.envelope < silence)
            {
                voice.playing = false;
                break;
            }

            // Interpolación lineal entre dos muestras: corrige diferencias de
            // sample rate y permite el pitch.
            const float fraction = (float) (voice.position - index);
            const double remaining = (voice.endPosition - voice.position) / voice.increment;
            const float fade = (float) juce::jmin (1.0, remaining / fadeOutputSamples);
            if (audibleGain < audibleTarget)
                audibleGain = juce::jmin (audibleTarget, audibleGain + muteStep);
            else if (audibleGain > audibleTarget)
                audibleGain = juce::jmax (audibleTarget, audibleGain - muteStep);

            const float amplitude = voice.envelope * fade * audibleGain;

            for (int channel = 0; channel < numOutputChannels; ++channel)
            {
                // Un archivo mono suena igual en los dos canales.
                const int sourceChannel = juce::jmin (channel, sourceChannels - 1);
                const float a = source.getSample (sourceChannel, index);
                const float b = source.getSample (sourceChannel, juce::jmin (index + 1, source.getNumSamples() - 1));

                const float channelGain = numOutputChannels == 1 ? 0.5f * (voice.gainLeft + voice.gainRight)
                                                                 : (channel == 0 ? voice.gainLeft : voice.gainRight);

                buffer.addSample (channel, i, (a + fraction * (b - a)) * channelGain * amplitude);
            }

            voice.position += voice.increment;
            voice.envelope *= voice.envelopeCoefficient;
        }
    }
}

// ============================================================================
// Carga de samples (hilo de UI + hilo de carga)
// ============================================================================

void NzDrumProcessor::loadSample (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= numPads)
        return;

    pendingLoads[(size_t) pad].fetch_add (1);
    const auto generation = restoreGeneration[(size_t) pad].load();

    loaderPool.addJob ([this, pad, file, generation]
    {
        if (isSuperseded (pad, generation))
        {
            pendingLoads[(size_t) pad].fetch_sub (1);
            return;
        }

        finishLoad (pad, file, readFile (file), generation);
    });
}

void NzDrumProcessor::stepSample (int pad, int direction)
{
    if (pad < 0 || pad >= numPads)
        return;

    pendingLoads[(size_t) pad].fetch_add (1);
    const auto generation = restoreGeneration[(size_t) pad].load();

    loaderPool.addJob ([this, pad, direction, generation]
    {
        // Si mientras esperaba se restauró un estado, este pedido ya no vale.
        if (isSuperseded (pad, generation))
        {
            pendingLoads[(size_t) pad].fetch_sub (1);
            return;
        }

        // Leemos el archivo actual recién acá, en el hilo de carga: si se hace
        // click en "siguiente" varias veces seguidas, cada pedido parte del
        // resultado del anterior y no del mismo archivo.
        const auto current = getPadFile (pad);
        const auto files = listAudioFilesNextTo (current);
        const int count = files.size();
        const int index = files.indexOf (current);
        int start = index;

        // Si el archivo actual ya no está (un pad "Falta"), arrancamos desde
        // donde estaría en el orden de la carpeta: "siguiente" da el primero
        // que viene después de su nombre y "anterior" el último antes.
        if (index < 0)
        {
            int position = 0;

            while (position < count && files[position].getFileName().compareNatural (current.getFileName()) < 0)
                ++position;

            start = direction > 0 ? position - 1 : position;
        }

        // Si el vecino no se puede leer (archivo roto o vacío), seguimos en la
        // misma dirección hasta encontrar uno que sí, sin pasar por todos más
        // de una vez. Si no hay ninguno, el pad se queda como estaba.
        const int lastOffset = index < 0 ? count : count - 1;

        for (int offset = 1; offset <= lastOffset; ++offset)
        {
            const auto& candidate = files.getReference (((start + offset * direction) % count + count) % count);

            if (auto sample = readFile (candidate))
            {
                finishLoad (pad, candidate, std::move (sample), generation);
                return;
            }
        }

        finishLoad (pad, {}, nullptr, generation);
    });
}

bool NzDrumProcessor::isSuperseded (int pad, uint32_t generation) const noexcept
{
    return restoreGeneration[(size_t) pad].load() != generation;
}

void NzDrumProcessor::finishLoad (int pad, const juce::File& file, std::unique_ptr<LoadedSample> sample, uint32_t generation)
{
    if (sample != nullptr)
    {
        {
            const juce::ScopedLock lock (padFilesLock);

            // Una carga pedida antes de restaurar un estado no pisa la ruta
            // restaurada: se descarta (el chequeo va bajo el lock para que no
            // se cuele entre la restauración y la escritura).
            if (isSuperseded (pad, generation))
            {
                pendingLoads[(size_t) pad].fetch_sub (1);
                return;
            }

            padFiles[(size_t) pad] = file;
            padMissing[(size_t) pad] = false;
        }

        sampleSeconds[(size_t) pad].store (sample->audio.getNumSamples() / sample->sampleRate);

        slots[(size_t) pad].publish (std::move (sample));
    }

    pendingLoads[(size_t) pad].fetch_sub (1);
}

std::unique_ptr<LoadedSample> NzDrumProcessor::readFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return nullptr;

    const auto maxLength = (juce::int64) (maxSampleSeconds * reader->sampleRate);
    const int length = (int) std::min (reader->lengthInSamples, maxLength);
    const int channels = (int) juce::jmin (2u, reader->numChannels);

    if (length <= 0)
        return nullptr;

    auto sample = std::make_unique<LoadedSample>();
    sample->audio.setSize (channels, length);
    sample->sampleRate = reader->sampleRate;
    sample->file = file;

    // Si la decodificación falla, el pad se queda con el sample que tenía.
    if (! reader->read (&sample->audio, 0, length, 0, true, channels > 1))
        return nullptr;

    return sample;
}

juce::Array<juce::File> NzDrumProcessor::listAudioFilesNextTo (const juce::File& current) const
{
    // Alcanza con que exista la carpeta: un pad "Falta" también puede
    // recorrerla para encontrar el archivo movido o uno parecido.
    if (current == juce::File() || ! current.getParentDirectory().isDirectory())
        return {};

    auto files = current.getParentDirectory().findChildFiles (juce::File::findFiles, false,
                                                              getSupportedWildcard());

    // macOS deja archivos "._algo" en discos externos; no son audio.
    files.removeIf ([] (const juce::File& f) { return f.isHidden() || f.getFileName().startsWith ("._"); });

    // Orden "natural", como Finder: kick2 va antes que kick10.
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });

    return files;
}

bool NzDrumProcessor::isPadAudible (int pad) const noexcept
{
    if (! juce::isPositiveAndBelow (pad, numPads))
        return false;

    bool anySolo = false;

    for (const auto& p : padParameters)
        anySolo = anySolo || p.solo->load (std::memory_order_relaxed) >= 0.5f;

    const auto& params = padParameters[(size_t) pad];

    // Un pad muteado no suena aunque esté en solo.
    if (params.mute->load (std::memory_order_relaxed) >= 0.5f)
        return false;

    return ! anySolo || params.solo->load (std::memory_order_relaxed) >= 0.5f;
}

void NzDrumProcessor::setStep (int pattern, int pad, int step, StepLevel level) noexcept
{
    if (juce::isPositiveAndBelow (pattern, numPatterns) && juce::isPositiveAndBelow (pad, numPads)
        && juce::isPositiveAndBelow (step, maxSteps))
    {
        steps[stepIndex (pattern, pad, step)].store ((uint8_t) level, std::memory_order_relaxed);

        // Un paso apagado no guarda semitono: si se vuelve a prender, arranca
        // en la afinación del pad.
        if (level == StepLevel::off)
            stepPitches[stepIndex (pattern, pad, step)].store (0, std::memory_order_relaxed);
    }
}

void NzDrumProcessor::setStepPitch (int pattern, int pad, int step, int semitones) noexcept
{
    if (juce::isPositiveAndBelow (pattern, numPatterns) && juce::isPositiveAndBelow (pad, numPads)
        && juce::isPositiveAndBelow (step, maxSteps))
        stepPitches[stepIndex (pattern, pad, step)].store ((int8_t) juce::jlimit (minStepPitch, maxStepPitch, semitones),
                                                           std::memory_order_relaxed);
}

int NzDrumProcessor::getStepPitch (int pattern, int pad, int step) const noexcept
{
    if (! juce::isPositiveAndBelow (pattern, numPatterns) || ! juce::isPositiveAndBelow (pad, numPads)
        || ! juce::isPositiveAndBelow (step, maxSteps))
        return 0;

    return stepPitches[stepIndex (pattern, pad, step)].load (std::memory_order_relaxed);
}

StepLevel NzDrumProcessor::getStep (int pattern, int pad, int step) const noexcept
{
    if (! juce::isPositiveAndBelow (pattern, numPatterns) || ! juce::isPositiveAndBelow (pad, numPads)
        || ! juce::isPositiveAndBelow (step, maxSteps))
        return StepLevel::off;

    return (StepLevel) steps[stepIndex (pattern, pad, step)].load (std::memory_order_relaxed);
}

void NzDrumProcessor::setPatternLength (int pattern, int length) noexcept
{
    if (juce::isPositiveAndBelow (pattern, numPatterns) && isValidPatternLength (length))
        patternLengths[(size_t) pattern].store (length, std::memory_order_relaxed);
}

int NzDrumProcessor::getPatternLength (int pattern) const noexcept
{
    return juce::isPositiveAndBelow (pattern, numPatterns)
               ? patternLengths[(size_t) pattern].load (std::memory_order_relaxed)
               : defaultPatternLength;
}

void NzDrumProcessor::selectPattern (int pattern)
{
    if (juce::isPositiveAndBelow (pattern, numPatterns))
    {
        // Con gesto, para que el DAW lo grabe como un solo cambio cuando la
        // automatización está en modo escritura.
        patternParameterObject->beginChangeGesture();
        patternParameterObject->setValueNotifyingHost (patternParameterObject->convertTo0to1 ((float) (pattern + 1)));
        patternParameterObject->endChangeGesture();
    }
}

void NzDrumProcessor::clearPattern (int pattern) noexcept
{
    if (! juce::isPositiveAndBelow (pattern, numPatterns))
        return;

    for (int pad = 0; pad < numPads; ++pad)
        for (int step = 0; step < maxSteps; ++step)
            setStep (pattern, pad, step, StepLevel::off);

    setPatternLength (pattern, defaultPatternLength);
}

void NzDrumProcessor::copyPattern (int pattern)
{
    if (! juce::isPositiveAndBelow (pattern, numPatterns))
        return;

    PatternClipboard clip;
    clip.length = getPatternLength (pattern);

    for (int pad = 0; pad < numPads; ++pad)
        for (int step = 0; step < maxSteps; ++step)
        {
            clip.steps[(size_t) (pad * maxSteps + step)] = (uint8_t) getStep (pattern, pad, step);
            clip.pitches[(size_t) (pad * maxSteps + step)] = (int8_t) getStepPitch (pattern, pad, step);
        }

    patternClipboard = clip;
}

void NzDrumProcessor::pastePattern (int pattern) noexcept
{
    if (! patternClipboard.has_value() || ! juce::isPositiveAndBelow (pattern, numPatterns))
        return;

    // Paso por paso: si el patrón está sonando, el audio puede ver un bloque
    // con el pegado a medias, que no se nota.
    for (int pad = 0; pad < numPads; ++pad)
        for (int step = 0; step < maxSteps; ++step)
        {
            setStep (pattern, pad, step, (StepLevel) patternClipboard->steps[(size_t) (pad * maxSteps + step)]);
            setStepPitch (pattern, pad, step, patternClipboard->pitches[(size_t) (pad * maxSteps + step)]);
        }

    setPatternLength (pattern, patternClipboard->length);
}

void NzDrumProcessor::copyPad (int pad)
{
    if (! juce::isPositiveAndBelow (pad, numPads))
        return;

    PadClipboard clip;
    clip.file = getPadFile (pad);

    for (size_t i = 0; i < copiedPadParameters.size(); ++i)
        clip.values[i] = parameters.getRawParameterValue (paramId (pad, copiedPadParameters[i]))->load();

    padClipboard = clip;
}

void NzDrumProcessor::pastePad (int pad)
{
    if (! padClipboard.has_value() || ! juce::isPositiveAndBelow (pad, numPads))
        return;

    if (padClipboard->file != juce::File() && padClipboard->file != getPadFile (pad))
        loadSample (pad, padClipboard->file);

    // Con gesto, para que el DAW lo registre como un solo cambio por ajuste.
    for (size_t i = 0; i < copiedPadParameters.size(); ++i)
    {
        if (auto* param = parameters.getParameter (paramId (pad, copiedPadParameters[i])))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (padClipboard->values[i]));
            param->endChangeGesture();
        }
    }
}

int NzDrumProcessor::getSelectedPattern() const noexcept
{
    // El parámetro va de 1 a 16; acá contamos desde 0.
    return juce::jlimit (0, numPatterns - 1, juce::roundToInt (patternParameter->load (std::memory_order_relaxed)) - 1);
}

int NzDrumProcessor::getPlayingPattern() const noexcept
{
    return playingPattern.load (std::memory_order_relaxed);
}

int NzDrumProcessor::getPlayingStep() const noexcept
{
    return playingStep.load (std::memory_order_relaxed);
}

float NzDrumProcessor::gainFor (StepLevel level) noexcept
{
    switch (level)
    {
        case StepLevel::low:    return 0.45f;
        case StepLevel::normal: return 0.75f;
        case StepLevel::accent: return 1.0f;
        case StepLevel::off:    break;
    }

    return 0.0f;
}

void NzDrumProcessor::setChromaticSource (int pad) noexcept
{
    chromaticSource.store (juce::isPositiveAndBelow (pad, numPads) ? pad : -1, std::memory_order_relaxed);
}

void NzDrumProcessor::triggerFromUi (int pad, float velocity) noexcept
{
    if (pad >= 0 && pad < numPads)
        uiTriggers[(size_t) pad].store (velocity, std::memory_order_release);
}

juce::File NzDrumProcessor::getPadFile (int pad) const
{
    const juce::ScopedLock lock (padFilesLock);
    return padFiles[(size_t) pad];
}

bool NzDrumProcessor::isPadMissing (int pad) const
{
    const juce::ScopedLock lock (padFilesLock);
    return juce::isPositiveAndBelow (pad, numPads) && padMissing[(size_t) pad];
}

void NzDrumProcessor::restorePad (int pad, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (pad, numPads))
        return;

    pendingLoads[(size_t) pad].fetch_add (1);

    // Cada restauración invalida las cargas pedidas antes (y una restauración
    // anterior que todavía no terminó).
    uint32_t generation = 0;

    // La ruta se anota ya, no cuando termine la carga: si el host guarda la
    // sesión mientras los samples todavía se están cargando, tiene que
    // guardar estas rutas y no las viejas (o ninguna).
    {
        const juce::ScopedLock lock (padFilesLock);
        generation = ++restoreGeneration[(size_t) pad];
        padFiles[(size_t) pad] = file;
        padMissing[(size_t) pad] = false;
    }

    // Por el mismo hilo de carga que el resto, así queda en orden con
    // cualquier carga pedida antes.
    loaderPool.addJob ([this, pad, file, generation]
    {
        auto sample = file == juce::File() ? nullptr : readFile (file);

        if (sample != nullptr)
        {
            finishLoad (pad, file, std::move (sample), generation);
            return;
        }

        // Vacío o faltante: un sample sin audio deja el pad mudo (la voz no
        // arranca con menos de 2 muestras) sin tocar el mecanismo del slot.
        {
            const juce::ScopedLock lock (padFilesLock);

            if (isSuperseded (pad, generation))
            {
                pendingLoads[(size_t) pad].fetch_sub (1);
                return;
            }

            padFiles[(size_t) pad] = file;
            padMissing[(size_t) pad] = file != juce::File();
        }

        sampleSeconds[(size_t) pad].store (0.0);
        slots[(size_t) pad].publish (std::make_unique<LoadedSample>());
        pendingLoads[(size_t) pad].fetch_sub (1);
    });
}

// ============================================================================
// Estado de la sesión
// ============================================================================

namespace
{
    // Versión del formato, por si algún día cambia.
    constexpr int stateVersion = 1;
    const juce::Identifier dataTag { "NzDrumData" };
}

void NzDrumProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();

    juce::ValueTree data (dataTag);
    data.setProperty ("version", stateVersion, nullptr);

    // Cada patrón: su largo y los pasos de los 16 pads como 512 dígitos
    // (0 apagado, 1 bajo, 2 normal, 3 acento). Compacto y fácil de leer.
    for (int pattern = 0; pattern < numPatterns; ++pattern)
    {
        juce::String digits, pitches;
        digits.preallocateBytes ((size_t) (numPads * maxSteps + 1));
        pitches.preallocateBytes ((size_t) (numPads * maxSteps + 1));
        bool anyPitch = false;

        for (int pad = 0; pad < numPads; ++pad)
        {
            for (int step = 0; step < maxSteps; ++step)
            {
                digits << juce::String::charToString ((juce::juce_wchar) ('0' + (int) getStep (pattern, pad, step)));

                // El semitono como un dígito hexadecimal: -8 es "0", 0 es "8"
                // y +7 es "f".
                const int pitch = getStepPitch (pattern, pad, step);
                anyPitch = anyPitch || pitch != 0;
                pitches << juce::String::toHexString (pitch - minStepPitch);
            }
        }

        juce::ValueTree node ("Pattern");
        node.setProperty ("index", pattern, nullptr);
        node.setProperty ("length", getPatternLength (pattern), nullptr);
        node.setProperty ("steps", digits, nullptr);

        // Solo si hay alguno: las sesiones sin semitonos quedan como antes.
        if (anyPitch)
            node.setProperty ("pitches", pitches, nullptr);
        data.appendChild (node, nullptr);
    }

    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto file = getPadFile (pad);

        if (file != juce::File())
        {
            juce::ValueTree node ("Pad");
            node.setProperty ("index", pad, nullptr);
            node.setProperty ("path", file.getFullPathName(), nullptr);
            data.appendChild (node, nullptr);
        }
    }

    state.appendChild (data, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void NzDrumProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto extra = tree.getChildWithName (dataTag);

    // Lo nuestro no va en el árbol de parámetros: se regenera al guardar.
    if (extra.isValid())
        tree.removeChild (extra, nullptr);

    parameters.replaceState (tree);

    if (! extra.isValid())
        return;

    for (int pattern = 0; pattern < numPatterns; ++pattern)
        clearPattern (pattern);

    std::array<juce::File, numPads> files;

    for (const auto& node : extra)
    {
        const int index = node.getProperty ("index", -1);

        if (node.hasType ("Pattern") && juce::isPositiveAndBelow (index, numPatterns))
        {
            setPatternLength (index, node.getProperty ("length", defaultPatternLength));

            const auto digits = node.getProperty ("steps").toString();

            for (int i = 0; i < juce::jmin (digits.length(), numPads * maxSteps); ++i)
            {
                const int level = (int) digits[i] - '0';

                if (level >= (int) StepLevel::off && level <= (int) StepLevel::accent)
                    setStep (index, i / maxSteps, i % maxSteps, (StepLevel) level);
            }

            // Sesiones anteriores no tienen "pitches": quedan todos en 0.
            const auto pitches = node.getProperty ("pitches").toString();

            for (int i = 0; i < juce::jmin (pitches.length(), numPads * maxSteps); ++i)
            {
                const int value = juce::CharacterFunctions::getHexDigitValue (pitches[i]);

                if (value >= 0 && getStep (index, i / maxSteps, i % maxSteps) != StepLevel::off)
                    setStepPitch (index, i / maxSteps, i % maxSteps, value + minStepPitch);
            }
        }
        else if (node.hasType ("Pad") && juce::isPositiveAndBelow (index, numPads))
        {
            const auto path = node.getProperty ("path").toString();

            if (juce::File::isAbsolutePath (path))
                files[(size_t) index] = juce::File (path);
        }
    }

    // Todos los pads, también los que no estaban en la sesión (si el host
    // carga un estado sobre una instancia con samples, esos pads se vacían),
    // y aunque la ruta parezca la misma: puede haber una carga pendiente en
    // la cola que la cambiaría después.
    for (int pad = 0; pad < numPads; ++pad)
        restorePad (pad, files[(size_t) pad]);
}

bool NzDrumProcessor::isLoading (int pad) const noexcept
{
    return pendingLoads[(size_t) pad].load() > 0;
}

uint32_t NzDrumProcessor::getTriggerCount (int pad) const noexcept
{
    return triggerCounts[(size_t) pad].load (std::memory_order_relaxed);
}

bool NzDrumProcessor::isSupportedFile (const juce::File& file) const
{
    return file.existsAsFile() && formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

juce::String NzDrumProcessor::getSupportedWildcard() const
{
    return formats.getWildcardForAllFormats();
}

double NzDrumProcessor::getTailLengthSeconds() const
{
    // Una nota puede seguir sonando hasta que termina su sample. El host usa
    // este valor para no cortar el final al exportar o congelar una pista.
    // Con el pitch hacia abajo el sample dura más, así que lo tenemos en cuenta.
    //
    // El modo cromático puede tocar cualquier pad hasta 8 semitonos más grave.
    // Lo contamos siempre, no solo con el modo prendido: el host no vuelve a
    // preguntar al prender o apagar el modo, y una nota grave puede seguir
    // sonando después de apagarlo. Pasarse un poco solo alarga el export.
    const double lowestChromatic = (double) juce::jmin (0, chromaticOffsetFor (0));
    double longest = 0.0;

    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto& params = padParameters[(size_t) pad];
        const double region = std::abs (params.end->load() - params.start->load());
        const double speed = std::exp2 ((params.pitch->load() + lowestChromatic) / 12.0);
        longest = juce::jmax (longest, sampleSeconds[(size_t) pad].load() * region / speed);
    }

    return longest;
}

void NzDrumProcessor::timerCallback()
{
    for (auto& slot : slots)
        slot.collectGarbage();
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NzDrumProcessor();
}

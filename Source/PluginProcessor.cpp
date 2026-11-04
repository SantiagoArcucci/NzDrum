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

juce::AudioProcessorValueTreeState::ParameterLayout NzDrumProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

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
                                             Attributes().withLabel ("dB")
                                                         .withStringFromValueFunction ([] (float v, int)
                                                         {
                                                             return v <= minLevelDb ? juce::String ("-inf") : juce::String (v, 1);
                                                         })));

        layout.add (std::make_unique<Float> (id (pad, "pan"), label (pad, "Pan"),
                                             juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                                             Attributes().withStringFromValueFunction ([] (float v, int)
                                             {
                                                 if (std::abs (v) < 0.005f) return juce::String ("C");
                                                 return juce::String (juce::roundToInt (std::abs (v) * 100.0f)) + (v < 0.0f ? " L" : " R");
                                             })));

        layout.add (std::make_unique<Float> (id (pad, "pitch"), label (pad, "Pitch"),
                                             juce::NormalisableRange<float> (-maxPitchSemitones, maxPitchSemitones, 0.01f), 0.0f,
                                             Attributes().withLabel ("st")));

        // Skew < 1: más resolución en los decays cortos, que es donde se nota.
        layout.add (std::make_unique<Float> (id (pad, "decay"), label (pad, "Decay"),
                                             juce::NormalisableRange<float> (minDecaySeconds, maxDecaySeconds, 0.001f, 0.35f), maxDecaySeconds,
                                             Attributes().withLabel ("s")
                                                         .withStringFromValueFunction ([] (float v, int)
                                                         {
                                                             return v >= maxDecaySeconds ? juce::String ("off") : juce::String (v, 2);
                                                         })));

        layout.add (std::make_unique<Float> (id (pad, "start"), label (pad, "Inicio"),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

        layout.add (std::make_unique<Float> (id (pad, "end"), label (pad, "Fin"),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 1.0f));

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

    for (auto& voice : voices)
        voice = {};
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
            voices[(size_t) pad].playing = false;
    }

    // 2. Juntamos todos los disparos del bloque en una sola lista:
    //    clicks de la UI (al inicio del bloque), pasos del secuenciador y
    //    notas MIDI, cada uno con su muestra exacta.
    for (int pad = 0; pad < numPads; ++pad)
    {
        const float velocity = uiTriggers[(size_t) pad].exchange (0.0f, std::memory_order_acq_rel);

        if (velocity > 0.0f)
            addEvent (0, pad, velocity);
    }

    if (position.hasValue() && position->getIsPlaying())
        collectSequencerEvents (*position, numSamples);
    else
        playingStep.store (-1, std::memory_order_relaxed);

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const int pad = message.getNoteNumber() - firstMidiNote;

            if (pad >= 0 && pad < numPads)
                addEvent (juce::jlimit (0, numSamples, metadata.samplePosition), pad, message.getFloatVelocity());
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
        startVoice (event.pad, blockSamples[(size_t) event.pad], event.velocity);
    }

    renderVoices (buffer, cursor, numSamples);
}

void NzDrumProcessor::addEvent (int offset, int pad, float velocity) noexcept
{
    if (numEvents < maxEventsPerBlock)
        events[(size_t) numEvents++] = { offset, pad, velocity };
}

void NzDrumProcessor::collectSequencerEvents (const juce::AudioPlayHead::PositionInfo& position, int numSamples) noexcept
{
    const auto bpm = position.getBpm();
    const auto ppq = position.getPpqPosition();

    if (! bpm.hasValue() || ! ppq.hasValue() || *bpm <= 0.0)
    {
        playingStep.store (-1, std::memory_order_relaxed);
        return;
    }

    // El host nos dice en qué negra empieza el bloque (PPQ). Con el tempo
    // sabemos cuántas muestras dura una negra, y de ahí dónde cae cada paso.
    const double samplesPerQuarter = currentSampleRate * 60.0 / *bpm;
    const double blockStart = *ppq;
    const double blockEnd = blockStart + numSamples / samplesPerQuarter;

    const auto wrap = [] (juce::int64 index) { return (int) (((index % numSteps) + numSteps) % numSteps); };

    playingStep.store (wrap ((juce::int64) std::floor (blockStart / stepLengthPpq)), std::memory_order_relaxed);

    // Primer paso que empieza dentro del bloque. El margen chico evita perder
    // un paso que cae justo en el inicio por error de redondeo; el siguiente
    // bloque no lo repite porque el fin del bloque es excluyente.
    constexpr double epsilon = 1.0e-9;
    auto stepIndex = (juce::int64) std::ceil (blockStart / stepLengthPpq - epsilon);

    for (; stepIndex * stepLengthPpq < blockEnd; ++stepIndex)
    {
        const double stepPpq = (double) stepIndex * stepLengthPpq;
        const int offset = juce::jlimit (0, numSamples - 1,
                                         (int) std::llround ((stepPpq - blockStart) * samplesPerQuarter));
        const int step = wrap (stepIndex);

        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto level = getStep (pad, step);

            if (level != StepLevel::off)
                addEvent (offset, pad, gainFor (level));
        }
    }
}

void NzDrumProcessor::startVoice (int pad, const LoadedSample* sample, float velocity) noexcept
{
    triggerCounts[(size_t) pad].fetch_add (1, std::memory_order_relaxed);

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

    auto& voice = voices[(size_t) pad];

    // Si no venía sonando nada, el pad arranca directo en su estado de mute:
    // no hay nada que suavizar y así el ataque queda intacto.
    if (! voice.playing)
        audibleGains[(size_t) pad] = blockAudible[(size_t) pad] ? 1.0f : 0.0f;

    // La interpolación lee la muestra siguiente, así que la voz tiene que
    // terminar a más tardar en la última muestra: el inicio queda al menos una
    // muestra antes del final, aunque el host ponga inicio = 1.
    voice.position = juce::jmin (start * lastIndex, lastIndex - 1.0);
    voice.endPosition = juce::jlimit (voice.position + 1.0, lastIndex, end * lastIndex);

    const float pitch = params.pitch->load (std::memory_order_relaxed);
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

    for (int pad = 0; pad < numPads; ++pad)
    {
        auto& voice = voices[(size_t) pad];
        const auto* sample = blockSamples[(size_t) pad];

        auto& audibleGain = audibleGains[(size_t) pad];
        const float audibleTarget = blockAudible[(size_t) pad] ? 1.0f : 0.0f;

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

    loaderPool.addJob ([this, pad, file]
    {
        finishLoad (pad, file, readFile (file));
    });
}

void NzDrumProcessor::stepSample (int pad, int direction)
{
    if (pad < 0 || pad >= numPads)
        return;

    pendingLoads[(size_t) pad].fetch_add (1);

    loaderPool.addJob ([this, pad, direction]
    {
        // Leemos el archivo actual recién acá, en el hilo de carga: si se hace
        // click en "siguiente" varias veces seguidas, cada pedido parte del
        // resultado del anterior y no del mismo archivo.
        const auto current = getPadFile (pad);
        const auto files = listAudioFilesNextTo (current);
        const int count = files.size();
        const int start = juce::jmax (0, files.indexOf (current));

        // Si el vecino no se puede leer (archivo roto o vacío), seguimos en la
        // misma dirección hasta encontrar uno que sí, sin pasar por todos más
        // de una vez. Si no hay ninguno, el pad se queda como estaba.
        for (int offset = 1; offset < count; ++offset)
        {
            const auto& candidate = files.getReference (((start + offset * direction) % count + count) % count);

            if (auto sample = readFile (candidate))
            {
                finishLoad (pad, candidate, std::move (sample));
                return;
            }
        }

        finishLoad (pad, {}, nullptr);
    });
}

void NzDrumProcessor::finishLoad (int pad, const juce::File& file, std::unique_ptr<LoadedSample> sample)
{
    if (sample != nullptr)
    {
        {
            const juce::ScopedLock lock (padFilesLock);
            padFiles[(size_t) pad] = file;
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
    if (! current.existsAsFile())
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

void NzDrumProcessor::setStep (int pad, int step, StepLevel level) noexcept
{
    if (pad >= 0 && pad < numPads && step >= 0 && step < numSteps)
        steps[(size_t) (pad * numSteps + step)].store ((uint8_t) level, std::memory_order_relaxed);
}

StepLevel NzDrumProcessor::getStep (int pad, int step) const noexcept
{
    return (StepLevel) steps[(size_t) (pad * numSteps + step)].load (std::memory_order_relaxed);
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
    double longest = 0.0;

    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto& params = padParameters[(size_t) pad];
        const double region = std::abs (params.end->load() - params.start->load());
        const double speed = std::exp2 (params.pitch->load() / 12.0);
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

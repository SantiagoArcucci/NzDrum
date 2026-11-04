#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

namespace
{
    // Tope de duración por sample, para que un archivo largo cargado por
    // error no se coma la memoria. Para baterías sobra.
    constexpr double maxSampleSeconds = 30.0;
}

NzDrumProcessor::NzDrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
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

    if (sample == nullptr || sample->audio.getNumSamples() == 0)
        return;

    auto& voice = voices[(size_t) pad];
    voice.position = 0.0;
    voice.increment = sample->sampleRate / currentSampleRate;
    voice.gain = velocity;
    voice.playing = true;
}

void NzDrumProcessor::renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept
{
    if (startSample >= endSample)
        return;

    const int numOutputChannels = buffer.getNumChannels();

    for (int pad = 0; pad < numPads; ++pad)
    {
        auto& voice = voices[(size_t) pad];
        const auto* sample = blockSamples[(size_t) pad];

        if (! voice.playing || sample == nullptr)
            continue;

        const auto& source = sample->audio;
        const int sourceLength = source.getNumSamples();
        const int sourceChannels = source.getNumChannels();

        for (int i = startSample; i < endSample; ++i)
        {
            const int index = (int) voice.position;

            if (index + 1 >= sourceLength)
            {
                voice.playing = false;
                break;
            }

            // Interpolación lineal entre dos muestras: alcanza para corregir
            // diferencias de sample rate (44,1 vs 48 kHz) en esta etapa.
            const float fraction = (float) (voice.position - index);

            for (int channel = 0; channel < numOutputChannels; ++channel)
            {
                // Un archivo mono suena igual en los dos canales.
                const int sourceChannel = juce::jmin (channel, sourceChannels - 1);
                const float a = source.getSample (sourceChannel, index);
                const float b = source.getSample (sourceChannel, index + 1);
                buffer.addSample (channel, i, (a + fraction * (b - a)) * voice.gain);
            }

            voice.position += voice.increment;
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
    double longest = 0.0;

    for (const auto& seconds : sampleSeconds)
        longest = juce::jmax (longest, seconds.load());

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

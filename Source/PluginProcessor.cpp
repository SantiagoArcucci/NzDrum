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

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            hostBpm.store (position->getBpm().orFallback (0.0), std::memory_order_relaxed);
            hostPpq.store (position->getPpqPosition().orFallback (0.0), std::memory_order_relaxed);
            hostPlaying.store (position->getIsPlaying(), std::memory_order_relaxed);
        }
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

    // 2. Clicks de la UI: suenan al inicio del bloque.
    for (int pad = 0; pad < numPads; ++pad)
    {
        const float velocity = uiTriggers[(size_t) pad].exchange (0.0f, std::memory_order_acq_rel);

        if (velocity > 0.0f)
            startVoice (pad, blockSamples[(size_t) pad], velocity);
    }

    // 3. MIDI con precisión de muestra: renderizamos hasta cada evento,
    //    disparamos y seguimos.
    const int numSamples = buffer.getNumSamples();
    int cursor = 0;

    for (const auto metadata : midi)
    {
        const int eventPosition = juce::jlimit (0, numSamples, metadata.samplePosition);
        renderVoices (buffer, cursor, eventPosition);
        cursor = eventPosition;

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const int pad = message.getNoteNumber() - firstMidiNote;

            if (pad >= 0 && pad < numPads)
                startVoice (pad, blockSamples[(size_t) pad], message.getFloatVelocity());
        }
    }

    renderVoices (buffer, cursor, numSamples);
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
        const auto target = findNeighbour (getPadFile (pad), direction);
        finishLoad (pad, target, target.existsAsFile() ? readFile (target) : nullptr);
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

juce::File NzDrumProcessor::findNeighbour (const juce::File& current, int direction) const
{
    if (! current.existsAsFile())
        return {};

    auto files = current.getParentDirectory().findChildFiles (juce::File::findFiles, false,
                                                              getSupportedWildcard());

    // macOS deja archivos "._algo" en discos externos; no son audio.
    files.removeIf ([] (const juce::File& f) { return f.isHidden() || f.getFileName().startsWith ("._"); });

    if (files.isEmpty())
        return {};

    // Orden "natural", como Finder: kick2 va antes que kick10.
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });

    const int count = files.size();
    const int index = juce::jmax (0, files.indexOf (current));
    return files[((index + direction) % count + count) % count];
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

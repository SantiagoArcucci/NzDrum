#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

namespace
{
    // Length cap per sample, so a long file loaded by mistake doesn't eat
    // the memory. Plenty for drums.
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
    // The number in a typed text ("-6 dB", "250ms", "1,5 st"): units are
    // ignored and a decimal comma is accepted.
    float numberIn (const juce::String& text)
    {
        return text.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getFloatValue();
    }

    // Avoids "-0.00": any value that rounds to zero is shown as 0.
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
    // "30 L" / "30 R", or a bare number: negative is left ("-30").
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

    // Under a second, in milliseconds: "250 ms" reads better than "0.25 s".
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

    // The playing pattern, 1 to 16. Automating it in the DAW chains patterns;
    // the switch waits for the current pattern to finish.
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "pattern", 1 },
                                                           juce::String ("Pattern"), 1, numPatterns, 1));

    // The 1 in ParameterID is the version: if a parameter ever changes, hosts
    // use it to tell it apart from the old one. Display names can change
    // freely; sessions find parameters by ID.
    const auto id = [] (int pad, const char* name) { return juce::ParameterID { paramId (pad, name), 1 }; };
    const auto label = [] (int pad, const char* name) { return "Pad " + juce::String (pad + 1) + " " + name; };

    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;

    for (int pad = 0; pad < numPads; ++pad)
    {
        layout.add (std::make_unique<Float> (id (pad, "level"), label (pad, "Level"),
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

        // Skew < 1: more resolution for short decays, where it matters.
        layout.add (std::make_unique<Float> (id (pad, "decay"), label (pad, "Decay"),
                                             juce::NormalisableRange<float> (minDecaySeconds, maxDecaySeconds, 0.001f, 0.35f), maxDecaySeconds,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return decayToText (v); })
                                                         .withValueFromStringFunction (textToDecay)));

        layout.add (std::make_unique<Float> (id (pad, "start"), label (pad, "Start"),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f,
                                             Attributes().withStringFromValueFunction ([] (float v, int) { return fixed (v * 100.0f, 1) + " %"; })
                                                         .withValueFromStringFunction ([] (const juce::String& t) { return juce::jlimit (0.0f, 1.0f, numberIn (t) / 100.0f); })));

        layout.add (std::make_unique<Float> (id (pad, "end"), label (pad, "End"),
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

    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg and, on Mac, MP3/M4A through CoreAudio
    startTimerHz (10);                // collects retired samples
}

NzDrumProcessor::~NzDrumProcessor()
{
    stopTimer();
    // Wait without a timeout for the load in progress: each load is bounded
    // (30 s of audio at most), and cutting it short would leave the loader
    // thread writing to freed memory.
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

    skipStepIndex.fill (-1);
}

bool NzDrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

// ============================================================================
// Audio thread
// ============================================================================

void NzDrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Audio thread rules: no new/malloc, no locks, no disk, no logging.
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

    // 1. Pick up any new samples left by the loader thread. If a pad changed
    //    sample, stop its voices: they were still reading the old buffer.
    for (int pad = 0; pad < numPads; ++pad)
    {
        bool changed = false;
        blockSamples[(size_t) pad] = slots[(size_t) pad].acquire (changed);

        if (changed)
            for (auto& voice : voices)
                if (voice.sourcePad == pad)
                    voice.playing = false;
    }

    // Read once per block, like mute and solo.
    const int chromatic = chromaticSource.load (std::memory_order_relaxed);
    const bool chromaticOn = juce::isPositiveAndBelow (chromatic, numPads);

    // 2. Gather every trigger of the block into one list: UI clicks (at the
    //    start of the block), sequencer steps and MIDI notes, each one at its
    //    exact sample.
    for (int pad = 0; pad < numPads; ++pad)
    {
        const float velocity = uiTriggers[(size_t) pad].exchange (0.0f, std::memory_order_acq_rel);

        if (velocity > 0.0f)
        {
            TriggerEvent event;
            event.pad = pad;
            event.velocity = velocity;
            event.chromatic = chromaticOn;
            event.live = true;
            addEvent (event);
        }
    }

    if (position.hasValue() && position->getIsPlaying())
    {
        collectSequencerEvents (*position, numSamples);
    }
    else
    {
        // Stopped: the requested pattern becomes the current one right away.
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
            {
                TriggerEvent event;
                event.offset = juce::jlimit (0, numSamples - 1, metadata.samplePosition);
                event.pad = pad;
                event.velocity = message.getFloatVelocity();
                event.chromatic = chromaticOn;
                event.live = true;
                addEvent (event);
            }
        }
    }

    // Recording only happens while the sequencer runs (DAW playing).
    const bool recordingNow = recording.load (std::memory_order_relaxed) && sequencerRunning;

    // Sort by sample offset with an insertion sort: std::sort and
    // std::stable_sort may allocate, and the list is short. It is stable, so two
    // triggers at the same sample keep their arrival order. While recording,
    // live hits go before the sequencer so they can replace the step that plays
    // at that same sample; otherwise, the usual order (the live hit arrives last
    // and is the one left playing).
    const auto goesAfter = [recordingNow] (const TriggerEvent& a, const TriggerEvent& b)
    {
        return a.offset > b.offset || (recordingNow && a.offset == b.offset && ! a.live && b.live);
    };

    for (int i = 1; i < numEvents; ++i)
    {
        const auto event = events[(size_t) i];
        int j = i - 1;

        for (; j >= 0 && goesAfter (events[(size_t) j], event); --j)
            events[(size_t) (j + 1)] = events[(size_t) j];

        events[(size_t) (j + 1)] = event;
    }

    // Mute and solo are read once per block; renderVoices smooths the change.
    for (int pad = 0; pad < numPads; ++pad)
        blockAudible[(size_t) pad] = isPadAudible (pad);

    // 3. Render up to each trigger, fire it, and carry on.
    int cursor = 0;

    for (int i = 0; i < numEvents; ++i)
    {
        const auto event = events[(size_t) i];

        if (event.cancelled)
            continue;

        if (event.live && recordingNow)
            recordLiveHit (event, i, chromatic);

        renderVoices (buffer, cursor, event.offset);
        cursor = event.offset;
        // In chromatic mode the pad that was hit picks the semitone and the source
        // pad is the one that sounds, with its own voice for each pad hit.
        if (event.chromatic)
            startVoice (numPads + event.pad, chromatic, event.velocity, (float) chromaticOffsetFor (event.pad));
        else
            startVoice (event.pad, event.pad, event.velocity, (float) event.semitones);

        // The pad that was hit flashes (in chromatic mode too, even if another sounds).
        triggerCounts[(size_t) event.pad].fetch_add (1, std::memory_order_relaxed);
    }

    renderVoices (buffer, cursor, numSamples);
}

void NzDrumProcessor::addEvent (const TriggerEvent& event) noexcept
{
    if (numEvents < maxEventsPerBlock)
        events[(size_t) numEvents++] = event;
}

double NzDrumProcessor::stepTimeFor (juce::int64 index) const noexcept
{
    return (double) index * stepLengthPpq + ((index & 1) != 0 ? swingOffsetNow : 0.0);
}

int NzDrumProcessor::patternStepFor (juce::int64 index) const noexcept
{
    const auto length = (juce::int64) patternLengths[(size_t) currentPattern].load (std::memory_order_relaxed);
    return (int) ((((index - patternAnchor) % length) + length) % length);
}

StepLevel NzDrumProcessor::levelForVelocity (float velocity) noexcept
{
    // Halfway between the levels (soft 0.45, normal 0.75, accent 1).
    if (velocity < 0.6f)
        return StepLevel::low;

    return velocity < 0.875f ? StepLevel::normal : StepLevel::accent;
}

void NzDrumProcessor::recordLiveHit (const TriggerEvent& event, int eventIndex, int chromatic) noexcept
{
    // In 16 Levels, record on the source pad with the semitone of the pad hit.
    const int lane = event.chromatic ? chromatic : event.pad;
    const int semitones = event.chromatic ? chromaticOffsetFor (event.pad) : 0;

    if (! juce::isPositiveAndBelow (lane, numPads) || samplesPerQuarterNow <= 0.0)
        return;

    // The nearest step, with swing: a hit slightly before or slightly after the
    // step lands on that step.
    const double time = blockStartPpq + event.offset / samplesPerQuarterNow;
    const auto base = (juce::int64) std::floor (time / stepLengthPpq);
    juce::int64 nearest = base;

    for (auto candidate = base - 1; candidate <= base + 1; ++candidate)
        if (std::abs (stepTimeFor (candidate) - time) < std::abs (stepTimeFor (nearest) - time))
            nearest = candidate;

    const int step = patternStepFor (nearest);
    setStep (currentPattern, lane, step, levelForVelocity (event.velocity));
    setStepPitch (currentPattern, lane, step, semitones);

    // If the step has not played yet, the sequencer must not repeat the hit that
    // was already heard live. If it falls in this block, the old step is already
    // in the list; if it falls later, it is skipped when reached.
    for (int i = eventIndex + 1; i < numEvents; ++i)
    {
        auto& later = events[(size_t) i];

        if (! later.live && later.pad == lane && later.stepIndex == nearest)
            later.cancelled = true;
    }

    if (stepTimeFor (nearest) >= blockEndPpq)
        skipStepIndex[(size_t) lane] = nearest;
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

    // The host tells us which quarter note the block starts on (PPQ). With the
    // tempo we know how many samples a quarter note lasts, and from that where
    // each step falls.
    const double samplesPerQuarter = currentSampleRate * 60.0 / *bpm;
    const double blockStart = *ppq;
    const double blockEnd = blockStart + numSamples / samplesPerQuarter;

    // If the transport just started or jumped (loop, click on the ruler), we
    // realign to the host grid: step 1 of the pattern lands on the bar lines.
    // A pending pattern request is applied at that moment.
    constexpr double jumpTolerance = 0.01;   // quarter notes

    if (! sequencerRunning || std::abs (blockStart - expectedBlockStart) > jumpTolerance)
    {
        currentPattern = getSelectedPattern();
        patternAnchor = 0;
        skipStepIndex.fill (-1);   // what was recorded before the jump no longer applies
    }

    sequencerRunning = true;
    expectedBlockStart = blockEnd;

    // Swing: the odd sixteenth (0-based) of each pair is delayed. At 50 % it
    // falls halfway through the pair; at 75 %, three quarters of the way.
    const float swing = juce::jlimit (minSwing, maxSwing, swingParameter->load (std::memory_order_relaxed));
    const double swingOffset = (swing / 100.0 - 0.5) * 2.0 * stepLengthPpq;

    blockStartPpq = blockStart;
    blockEndPpq = blockEnd;
    samplesPerQuarterNow = samplesPerQuarter;
    swingOffsetNow = swingOffset;

    const auto stepTime = [this] (juce::int64 index) { return stepTimeFor (index); };
    const auto positionInPattern = [this] (juce::int64 index) { return patternStepFor (index); };

    // Walk the sixteenths that fall inside the block, in order. The small
    // margin avoids losing a step that falls right at the start because of
    // rounding; the next block doesn't repeat it because the end is exclusive.
    constexpr double epsilon = 1.0e-9;
    auto index = (juce::int64) std::floor (blockStart / stepLengthPpq) - 1;

    for (; (double) index * stepLengthPpq < blockEnd; ++index)
    {
        const double time = stepTime (index);

        if (time < blockStart - epsilon || time >= blockEnd)
            continue;

        // When the pattern ends (back to step 1) the pending request takes over.
        if (positionInPattern (index) == 0)
        {
            const int requested = getSelectedPattern();

            if (requested != currentPattern)
            {
                currentPattern = requested;
                patternAnchor = index;

                // What was recorded in the previous pattern doesn't skip steps of the new one.
                skipStepIndex.fill (-1);
            }
        }

        const int step = positionInPattern (index);
        const int offset = juce::jlimit (0, numSamples - 1, (int) std::llround ((time - blockStart) * samplesPerQuarter));

        for (int pad = 0; pad < numPads; ++pad)
        {
            // A hit recorded before its step arrives has already been heard.
            if (skipStepIndex[(size_t) pad] >= 0 && index >= skipStepIndex[(size_t) pad])
            {
                const bool skip = index == skipStepIndex[(size_t) pad];
                skipStepIndex[(size_t) pad] = -1;

                if (skip)
                    continue;
            }

            const auto level = getStep (currentPattern, pad, step);

            if (level != StepLevel::off)
            {
                TriggerEvent event;
                event.offset = offset;
                event.pad = pad;
                event.velocity = gainFor (level);
                event.semitones = getStepPitch (currentPattern, pad, step);
                event.stepIndex = index;
                addEvent (event);
            }
        }
    }

    // For the UI: the last step that started inside the block (without swing:
    // good enough to light it up).
    playingStep.store (positionInPattern ((juce::int64) std::floor (blockEnd / stepLengthPpq - epsilon)), std::memory_order_relaxed);
    playingPattern.store (currentPattern, std::memory_order_relaxed);
}

void NzDrumProcessor::startVoice (int voiceIndex, int pad, float velocity, float extraSemitones) noexcept
{
    const auto* sample = blockSamples[(size_t) pad];

    if (sample == nullptr || sample->audio.getNumSamples() < 2)
        return;

    // Parameters are read once per trigger: turning a knob affects the next hit,
    // not the one already playing.
    const auto& params = padParameters[(size_t) pad];
    const auto lastIndex = (double) (sample->audio.getNumSamples() - 1);

    float start = params.start->load (std::memory_order_relaxed);
    float end = params.end->load (std::memory_order_relaxed);

    if (end < start)
        std::swap (start, end);

    auto& voice = voices[(size_t) voiceIndex];

    // If nothing was playing, the voice starts directly in its pad's mute state:
    // there is nothing to smooth, and this keeps the attack intact.
    if (! voice.playing)
        audibleGains[(size_t) voiceIndex] = blockAudible[(size_t) pad] ? 1.0f : 0.0f;

    voice.sourcePad = pad;

    // Interpolation reads the next sample, so the voice has to end at the last
    // sample at the latest: the start stays at least one sample before the end,
    // even if the host sets start = 1.
    voice.position = juce::jmin (start * lastIndex, lastIndex - 1.0);
    voice.endPosition = juce::jlimit (voice.position + 1.0, lastIndex, end * lastIndex);

    const float pitch = params.pitch->load (std::memory_order_relaxed) + extraSemitones;
    voice.increment = (sample->sampleRate / currentSampleRate) * std::exp2 (pitch / 12.0);

    const float levelDb = params.level->load (std::memory_order_relaxed);
    const float gain = velocity * (levelDb <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (levelDb));

    // "Balance" style pan: at the centre both channels stay as they were, and
    // towards one side the other one is turned down.
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

    // A 2 ms fade on reaching the end avoids the click of cutting the waveform
    // at a non-zero point, which is common when trimming with the marker.
    const double fadeOutputSamples = 0.002 * currentSampleRate;
    constexpr float silence = 1.0e-4f;

    // Mute and solo ramp the pad's volume down or up over 5 ms, like a mixing
    // desk fader: cutting it abruptly would click.
    const float muteStep = (float) (1.0 / (0.005 * currentSampleRate));

    for (int v = 0; v < numVoices; ++v)
    {
        auto& voice = voices[(size_t) v];
        const auto* sample = blockSamples[(size_t) voice.sourcePad];

        auto& audibleGain = audibleGains[(size_t) v];
        const float audibleTarget = blockAudible[(size_t) voice.sourcePad] ? 1.0f : 0.0f;

        if (! voice.playing || sample == nullptr)
        {
            audibleGain = audibleTarget;   // no voice, nothing to smooth
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

            // Linear interpolation between two samples: corrects sample rate
            // differences and allows pitching.
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
                // A mono file sounds the same on both channels.
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
// Sample loading (UI thread + loader thread)
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
        // If a state was restored while this was waiting, the request is stale.
        if (isSuperseded (pad, generation))
        {
            pendingLoads[(size_t) pad].fetch_sub (1);
            return;
        }

        // Read the current file only here, on the loader thread: if "next" is
        // clicked several times in a row, each request starts from the result of
        // the previous one and not from the same file.
        const auto current = getPadFile (pad);
        const auto files = listAudioFilesNextTo (current);
        const int count = files.size();
        const int index = files.indexOf (current);
        int start = index;

        // If the current file is gone (a "Missing" pad), start from where it would
        // be in the folder order: "next" gives the first one after its name and
        // "previous" the last one before it.
        if (index < 0)
        {
            int position = 0;

            while (position < count && files[position].getFileName().compareNatural (current.getFileName()) < 0)
                ++position;

            start = direction > 0 ? position - 1 : position;
        }

        // If the neighbour can't be read (broken or empty file), keep going in the
        // same direction until one can, without visiting them all more than once.
        // If none can, the pad stays as it was.
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

            // A load requested before a state was restored must not overwrite the
            // restored path: it is dropped (the check happens under the lock so it can't
            // slip in between the restore and the write).
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

    // If decoding fails, the pad keeps the sample it had.
    if (! reader->read (&sample->audio, 0, length, 0, true, channels > 1))
        return nullptr;

    return sample;
}

juce::Array<juce::File> NzDrumProcessor::listAudioFilesNextTo (const juce::File& current) const
{
    // It's enough that the folder exists: a "Missing" pad can also browse it to
    // find the moved file or a similar one.
    if (current == juce::File() || ! current.getParentDirectory().isDirectory())
        return {};

    auto files = current.getParentDirectory().findChildFiles (juce::File::findFiles, false,
                                                              getSupportedWildcard());

    // macOS leaves "._something" files on external drives; they aren't audio.
    files.removeIf ([] (const juce::File& f) { return f.isHidden() || f.getFileName().startsWith ("._"); });

    // "Natural" order, like Finder: kick2 comes before kick10.
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

    // A muted pad stays silent even when soloed.
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

        // A step that is off doesn't keep a semitone: when turned back on, it starts
        // at the pad's tuning.
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
        // With a gesture, so the DAW records it as a single change when automation
        // is in write mode.
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

    // Step by step: if the pattern is playing, the audio thread may see one block
    // with a half-done paste, which isn't noticeable.
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

    // With a gesture, so the DAW records a single change per adjustment.
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
    // The parameter goes from 1 to 16; here we count from 0.
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

    // Each restore invalidates the loads requested before it (and an earlier
    // restore that hasn't finished yet).
    uint32_t generation = 0;

    // The path is recorded now, not when the load finishes: if the host saves
    // the session while samples are still loading, it has to save these paths
    // and not the old ones (or none).
    {
        const juce::ScopedLock lock (padFilesLock);
        generation = ++restoreGeneration[(size_t) pad];
        padFiles[(size_t) pad] = file;
        padMissing[(size_t) pad] = false;
    }

    // Through the same loader thread as everything else, so it stays in order
    // with any load requested earlier.
    loaderPool.addJob ([this, pad, file, generation]
    {
        auto sample = file == juce::File() ? nullptr : readFile (file);

        if (sample != nullptr)
        {
            finishLoad (pad, file, std::move (sample), generation);
            return;
        }

        // Empty or missing: a sample with no audio leaves the pad silent (a voice
        // doesn't start with fewer than 2 samples) without touching the slot logic.
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
// Session state
// ============================================================================

namespace
{
    // Format version, in case it ever changes.
    constexpr int stateVersion = 1;
    const juce::Identifier dataTag { "NzDrumData" };
}

void NzDrumProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();

    juce::ValueTree data (dataTag);
    data.setProperty ("version", stateVersion, nullptr);

    // Each pattern: its length and the steps of the 16 pads as 512 digits
    // (0 off, 1 soft, 2 normal, 3 accent). Compact and easy to read.
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

                // The semitone as one hex digit: -8 is "0", 0 is "8" and +7 is "f".
                const int pitch = getStepPitch (pattern, pad, step);
                anyPitch = anyPitch || pitch != 0;
                pitches << juce::String::toHexString (pitch - minStepPitch);
            }
        }

        juce::ValueTree node ("Pattern");
        node.setProperty ("index", pattern, nullptr);
        node.setProperty ("length", getPatternLength (pattern), nullptr);
        node.setProperty ("steps", digits, nullptr);

        // Only if there is any: sessions without semitones stay as before.
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

    // Our own data doesn't live in the parameter tree: it's rebuilt on save.
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

            // Older sessions have no "pitches": they all stay at 0.
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

    // Every pad, including the ones that weren't in the session (if the host
    // loads a state onto an instance with samples, those pads are emptied), and
    // even if the path looks the same: there may be a pending load in the queue
    // that would change it later.
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
    // A note can keep sounding until its sample ends. The host uses this value
    // so it doesn't cut the end when exporting or freezing a track. Pitching
    // down makes the sample last longer, so we take that into account.
    //
    // Chromatic mode can play any pad up to 8 semitones lower. We always count
    // it, not only with the mode on: the host doesn't ask again when the mode is
    // toggled, and a low note can keep sounding after it is turned off.
    // Overshooting a little only makes the export a bit longer.
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

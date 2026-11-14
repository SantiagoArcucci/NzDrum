#include "StepSequencerComponent.h"

namespace
{
    // Positions inside the panel, from the design: 36 x 58 keys, 4 px apart,
    // with 14 px between beats.
    constexpr float firstStepX = 17.0f, stepPitch = 40.0f, beatGap = 10.0f;
    constexpr float stepWidth = 36.0f;
    constexpr float stepTop = 22.0f;
    constexpr float ledWidth = 24.0f, ledHeight = 5.0f;
    constexpr float keyOffset = 12.0f, keyHeight = 46.0f;   // one row (up to 16 steps)
    constexpr float compactRowPitch = 40.0f, compactKeyOffset = 8.0f, compactKeyHeight = 30.0f;   // 32 steps
    constexpr float numbersTop = 82.0f;
    constexpr int columns = 16;
}

StepSequencerComponent::StepSequencerComponent (NzDrumProcessor& processor)
    : nzProcessor (processor)
{
    setTooltip (juce::String::fromUTF8 ("Click: normal · Shift: accent · Option: soft · "
                                        "Wheel over a step: changes its semitone"));
}

void StepSequencerComponent::setPad (int newPad)
{
    pad = newPad;

    // Painting reads the cached steps: load the new pad's before drawing.
    refresh();
    repaint();
}

void StepSequencerComponent::refresh()
{
    const int newPattern = nzProcessor.getSelectedPattern();
    const int newLength = nzProcessor.getPatternLength (newPattern);

    // The playhead only shows if the pattern on screen is the one playing
    // (while a pattern switch waits, the previous one keeps playing).
    const int current = nzProcessor.getPlayingPattern() == newPattern ? nzProcessor.getPlayingStep() : -1;

    // Steps can also change without a click here (pasting or clearing a
    // pattern, recording), so we compare what is shown with what is stored.
    bool stepsChanged = false;

    for (int step = 0; step < NzDrumProcessor::maxSteps; ++step)
    {
        const auto level = nzProcessor.getStep (newPattern, pad, step);
        const int pitch = nzProcessor.getStepPitch (newPattern, pad, step);

        if (level != shownSteps[(size_t) step] || pitch != shownPitches[(size_t) step])
        {
            shownSteps[(size_t) step] = level;
            shownPitches[(size_t) step] = pitch;
            stepsChanged = true;
        }
    }

    const auto newName = nzProcessor.getPadFile (pad).getFileNameWithoutExtension().toUpperCase();

    if (stepsChanged || newPattern != pattern || newLength != length || current != playingStep || newName != padName)
    {
        padName = newName;
        pattern = newPattern;
        length = newLength;
        playingStep = current;
        repaint();
    }
}

StepLevel StepSequencerComponent::levelAfterClick (StepLevel current, const juce::ModifierKeys& modifiers) noexcept
{
    StepLevel target = StepLevel::normal;

    if (modifiers.isShiftDown())
        target = StepLevel::accent;
    else if (modifiers.isAltDown())
        target = StepLevel::low;

    // Repeating the same click on the same level turns the step off.
    return current == target ? StepLevel::off : target;
}

juce::Rectangle<float> StepSequencerComponent::stepBounds (int step) const
{
    // Always 16 columns, so a step stays in its place at any length. With 32
    // steps the keys get shorter to fit two rows.
    const int row = step / columns;
    const int column = step % columns;
    const float x = firstStepX + (float) column * stepPitch + (float) (column / 4) * beatGap;

    if (length > columns)
        return { x, stepTop - 2.0f + (float) row * compactRowPitch, stepWidth, compactKeyOffset + compactKeyHeight };

    return { x, stepTop, stepWidth, keyOffset + keyHeight };
}

juce::Rectangle<float> StepSequencerComponent::keyBounds (int step) const
{
    const auto bounds = stepBounds (step);
    const bool compact = length > columns;
    return bounds.withTrimmedTop (compact ? compactKeyOffset : keyOffset);
}

juce::Rectangle<float> StepSequencerComponent::ledBounds (int step) const
{
    const auto bounds = stepBounds (step);
    const bool compact = length > columns;
    return { bounds.getCentreX() - ledWidth * 0.5f, bounds.getY() + 1.0f, ledWidth, compact ? 4.0f : ledHeight };
}

int StepSequencerComponent::stepAt (juce::Point<float> position) const
{
    for (int step = 0; step < length; ++step)
        if (stepBounds (step).contains (position))
            return step;

    return -1;
}

void StepSequencerComponent::paintStep (juce::Graphics& g, int step, bool inPattern)
{
    using namespace theme;

    const auto level = inPattern ? shownSteps[(size_t) step] : StepLevel::off;
    const auto key = keyBounds (step);
    const auto led = ledBounds (step);
    constexpr float keyRadius = 4.0f, ledRadius = 2.5f;

    // LED: off, soft (lit), normal (lit with a glow) or accent (bright red).
    switch (level)
    {
        case StepLevel::accent:
            drawGlow (g, led, ledRadius, colour::accent.withAlpha (0.9f), 7);
            g.setColour (colour::accent);
            g.fillRoundedRectangle (led, ledRadius);
            break;

        case StepLevel::normal:
            drawGlow (g, led, ledRadius, colour::active.withAlpha (0.6f), 5);
            g.setColour (colour::active);
            g.fillRoundedRectangle (led, ledRadius);
            break;

        case StepLevel::low:
            g.setColour (colour::active.withAlpha (0.55f));
            g.fillRoundedRectangle (led, ledRadius);
            drawInnerShadow (g, led, ledRadius, juce::Colours::black.withAlpha (0.4f), 1, { 0, 1 });
            break;

        case StepLevel::off:
        default:
            g.setColour (colour::inactive);
            g.fillRoundedRectangle (led, ledRadius);
            drawInnerShadow (g, led, ledRadius, juce::Colours::black.withAlpha (0.4f), 1, { 0, 1 });
            break;
    }

    // The key, coloured by beat.
    const auto& beat = colour::beats[(size_t) ((step % columns) / 4)];
    drawDropShadow (g, key, keyRadius, juce::Colours::black.withAlpha (0.4f), 2, { 0, 2 });
    g.setGradientFill (juce::ColourGradient::vertical (beat.light, key.getY(), beat.dark, key.getBottom()));
    g.fillRoundedRectangle (key, keyRadius);

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path inside;
        inside.addRoundedRectangle (key.reduced (1.0f), keyRadius - 1.0f);
        g.reduceClipRegion (inside);
        g.setColour (juce::Colours::white.withAlpha (0.88f));
        g.fillRect (key.reduced (1.0f).removeFromTop (1.0f));
    }

    const bool isPlaying = inPattern && step == playingStep;
    g.setColour (colour::primary);
    g.drawRoundedRectangle (key.reduced (isPlaying ? 1.0f : 0.5f), keyRadius, isPlaying ? 2.0f : 1.0f);

    // The semitone, if any: "+3", "-5".
    const int pitch = shownPitches[(size_t) step];

    if (inPattern && level != StepLevel::off && pitch != 0)
        drawText (g, (pitch > 0 ? "+" : "") + juce::String (pitch), font::bold (12.0f), colour::primary,
                  key, juce::Justification::centred);
}

void StepSequencerComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    drawText (g, "STEP SEQUENCER", font::label(), colour::secondary, { 14.0f, 6.0f, 110.0f, 12.0f }, juce::Justification::centredLeft);

    juce::String padLabel = "PAD " + juce::String (pad + 1).paddedLeft ('0', 2);

    if (padName.isNotEmpty())
        padLabel << juce::String::fromUTF8 (" \xc2\xb7 ") << padName;

    drawText (g, padLabel, font::label(), colour::active, { 118.0f, 6.0f, 400.0f, 12.0f }, juce::Justification::centredLeft);

    // With 4 or 8 steps, the rest of the row is faded out.
    const int shown = juce::jmax (length, columns);

    for (int step = 0; step < shown; ++step)
    {
        const bool inPattern = step < length;

        if (inPattern)
        {
            paintStep (g, step, true);
        }
        else
        {
            g.beginTransparencyLayer (0.3f);
            paintStep (g, step, false);
            g.endTransparencyLayer();
        }
    }

    // Step numbers under a single row; beat starts stand out.
    if (length <= columns)
    {
        for (int step = 0; step < columns; ++step)
        {
            const auto bounds = stepBounds (step);
            const bool beatStart = step % 4 == 0;
            drawText (g, juce::String (step + 1), beatStart ? font::bold (10.0f) : font::semiBold (10.0f),
                      beatStart ? colour::primary : colour::secondary,
                      { bounds.getX(), numbersTop, bounds.getWidth(), 12.0f }, juce::Justification::centred);
        }
    }
}

void StepSequencerComponent::mouseDown (const juce::MouseEvent& event)
{
    const int step = stepAt (event.position);

    if (step < 0)
        return;

    nzProcessor.setStep (pattern, pad, step, levelAfterClick (nzProcessor.getStep (pattern, pad, step), event.mods));
    refresh();
}

void StepSequencerComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const int step = stepAt (event.position);

    if (step < 0 || nzProcessor.getStep (pattern, pad, step) == StepLevel::off)
    {
        Component::mouseWheelMove (event, wheel);
        return;
    }

    if (step != wheelStep)
    {
        wheelStep = step;
        wheelAccumulator = 0.0f;
    }

    // Same as the waveform: the axis that moved most (Shift + wheel arrives
    // as horizontal on macOS), flipped with natural scrolling.
    float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (wheel.isReversed)
        delta = -delta;

    // A mouse wheel notch is about 0.1: one semitone per notch.
    constexpr float perSemitone = 0.08f;
    wheelAccumulator += delta;

    int change = 0;

    while (wheelAccumulator >= perSemitone)  { ++change; wheelAccumulator -= perSemitone; }
    while (wheelAccumulator <= -perSemitone) { --change; wheelAccumulator += perSemitone; }

    if (change != 0)
    {
        nzProcessor.setStepPitch (pattern, pad, step, nzProcessor.getStepPitch (pattern, pad, step) + change);
        refresh();
    }
}

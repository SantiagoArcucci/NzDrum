#include "StepSequencerComponent.h"

namespace
{
    const juce::Colour offColour { 0xff2a2d35 };
    const juce::Colour stepColour { 0xffff8a3d };
    const juce::Colour textColour { 0xffe8e8ea };
    constexpr int labelWidth = 70;
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

    if (stepsChanged || newPattern != pattern || newLength != length || current != playingStep)
    {
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
    // Always 16 columns, so a step has the same size at any length.
    constexpr int columns = 16;
    const int rows = length > columns ? 2 : 1;
    const int row = step / columns;
    const int column = step % columns;

    auto area = getLocalBounds().withTrimmedLeft (labelWidth).reduced (0, 6).toFloat();
    const float groupGap = 8.0f;   // gap every 4 steps, to read the beats
    const float rowGap = 4.0f;
    const float stepWidth = (area.getWidth() - groupGap * 3.0f) / (float) columns;
    const float rowHeight = (area.getHeight() - rowGap * (float) (rows - 1)) / (float) rows;
    const float x = area.getX() + (float) column * stepWidth + (float) (column / 4) * groupGap;
    const float y = area.getY() + (float) row * (rowHeight + rowGap);

    return { x + 2.0f, y, stepWidth - 4.0f, rowHeight };
}

int StepSequencerComponent::stepAt (juce::Point<float> position) const
{
    for (int step = 0; step < length; ++step)
        if (stepBounds (step).contains (position))
            return step;

    return -1;
}

void StepSequencerComponent::paint (juce::Graphics& g)
{
    g.setColour (textColour.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Pad " + juce::String (pad + 1),
                getLocalBounds().removeFromLeft (labelWidth).reduced (14, 0),
                juce::Justification::centredLeft);

    // With 4 or 8 steps, the rest of the row is only outlined.
    for (int step = length; step < juce::jmin (16, NzDrumProcessor::maxSteps); ++step)
    {
        g.setColour (offColour.withAlpha (0.3f));
        g.drawRoundedRectangle (stepBounds (step), 4.0f, 1.0f);
    }

    for (int step = 0; step < length; ++step)
    {
        const auto bounds = stepBounds (step);
        const auto level = nzProcessor.getStep (pattern, pad, step);

        g.setColour (offColour);
        g.fillRoundedRectangle (bounds, 4.0f);

        // Height and colour intensity show the level.
        if (level != StepLevel::off)
        {
            const float amount = NzDrumProcessor::gainFor (level);
            const auto filled = bounds.withTrimmedTop (bounds.getHeight() * (1.0f - amount));
            g.setColour (stepColour.withAlpha (0.35f + 0.65f * amount));
            g.fillRoundedRectangle (filled, 4.0f);

            // The semitone, if any: "+3", "-5".
            const int pitch = nzProcessor.getStepPitch (pattern, pad, step);

            if (pitch != 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.85f));
                g.setFont (juce::FontOptions (juce::jmin (13.0f, bounds.getHeight() * 0.45f), juce::Font::bold));
                g.drawText ((pitch > 0 ? "+" : "") + juce::String (pitch), bounds, juce::Justification::centred);
            }
        }

        if (step == playingStep)
        {
            g.setColour (textColour);
            g.drawRoundedRectangle (bounds, 4.0f, 2.0f);
        }
    }
}

void StepSequencerComponent::mouseDown (const juce::MouseEvent& event)
{
    const int step = stepAt (event.position);

    if (step < 0)
        return;

    nzProcessor.setStep (pattern, pad, step, levelAfterClick (nzProcessor.getStep (pattern, pad, step), event.mods));
    repaint();
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
        repaint();
    }
}

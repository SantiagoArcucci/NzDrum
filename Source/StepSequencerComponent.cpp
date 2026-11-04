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
}

void StepSequencerComponent::setPad (int newPad)
{
    pad = newPad;
    repaint();
}

void StepSequencerComponent::refresh()
{
    const int current = nzProcessor.getPlayingStep();

    if (current != playingStep)
    {
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

    // Repetir el mismo click sobre el mismo nivel apaga el paso.
    return current == target ? StepLevel::off : target;
}

juce::Rectangle<float> StepSequencerComponent::stepBounds (int step) const
{
    auto area = getLocalBounds().withTrimmedLeft (labelWidth).reduced (0, 8).toFloat();
    const float groupGap = 8.0f;   // separación cada 4 pasos, para leer los tiempos
    const float stepWidth = (area.getWidth() - groupGap * 3.0f) / (float) NzDrumProcessor::numSteps;
    const float x = area.getX() + (float) step * stepWidth + (float) (step / 4) * groupGap;

    return { x + 2.0f, area.getY(), stepWidth - 4.0f, area.getHeight() };
}

int StepSequencerComponent::stepAt (juce::Point<float> position) const
{
    for (int step = 0; step < NzDrumProcessor::numSteps; ++step)
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

    for (int step = 0; step < NzDrumProcessor::numSteps; ++step)
    {
        const auto bounds = stepBounds (step);
        const auto level = nzProcessor.getStep (pad, step);

        g.setColour (offColour);
        g.fillRoundedRectangle (bounds, 4.0f);

        // La altura y la intensidad del color muestran el nivel.
        if (level != StepLevel::off)
        {
            const float amount = NzDrumProcessor::gainFor (level);
            const auto filled = bounds.withTrimmedTop (bounds.getHeight() * (1.0f - amount));
            g.setColour (stepColour.withAlpha (0.35f + 0.65f * amount));
            g.fillRoundedRectangle (filled, 4.0f);
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

    nzProcessor.setStep (pad, step, levelAfterClick (nzProcessor.getStep (pad, step), event.mods));
    repaint();
}

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
    setTooltip (juce::String::fromUTF8 ("Click: normal · Shift: acento · Option: bajo · "
                                        "Rueda sobre un paso: cambia su semitono"));
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

    // El indicador solo se muestra si suena el patrón que estamos viendo
    // (mientras espera un cambio de patrón, suena el anterior).
    const int current = nzProcessor.getPlayingPattern() == newPattern ? nzProcessor.getPlayingStep() : -1;

    // Los pasos también pueden cambiar sin un click acá (pegar o borrar un
    // patrón), así que comparamos lo que se ve con lo que hay.
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

    // Repetir el mismo click sobre el mismo nivel apaga el paso.
    return current == target ? StepLevel::off : target;
}

juce::Rectangle<float> StepSequencerComponent::stepBounds (int step) const
{
    // Siempre 16 columnas, así un paso mide lo mismo con cualquier largo.
    constexpr int columns = 16;
    const int rows = length > columns ? 2 : 1;
    const int row = step / columns;
    const int column = step % columns;

    auto area = getLocalBounds().withTrimmedLeft (labelWidth).reduced (0, 6).toFloat();
    const float groupGap = 8.0f;   // separación cada 4 pasos, para leer los tiempos
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

    // Con 4 u 8 pasos, el resto de la fila queda apenas marcado.
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

        // La altura y la intensidad del color muestran el nivel.
        if (level != StepLevel::off)
        {
            const float amount = NzDrumProcessor::gainFor (level);
            const auto filled = bounds.withTrimmedTop (bounds.getHeight() * (1.0f - amount));
            g.setColour (stepColour.withAlpha (0.35f + 0.65f * amount));
            g.fillRoundedRectangle (filled, 4.0f);

            // El semitono, si tiene: "+3", "-5".
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

    // Igual que en la forma de onda: el eje que más se movió (Shift + rueda
    // llega como horizontal en macOS) y con scroll natural se invierte.
    float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (wheel.isReversed)
        delta = -delta;

    // Una muesca de rueda de mouse es cerca de 0.1: un semitono por muesca.
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

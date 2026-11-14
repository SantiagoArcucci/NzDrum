#include "PadEditorComponent.h"

namespace
{
    constexpr float markerGrabDistance = 8.0f;

    // Positions inside the panel, from the design.
    const juce::Rectangle<int> waveDisplay { 14, 22, 360, 116 };
    constexpr int knobColumnsX = 390, knobColumnWidth = 74;
    constexpr int knobLabelY = 30, knobTop = 44, knobSize = 52, knobLcdWidth = 60, knobLcdHeight = 22;
}

// ============================================================================
// WaveformView
// ============================================================================

WaveformView::WaveformView (NzDrumProcessor& processor)
    : nzProcessor (processor),
      thumbnail (256, processor.getFormatManager(), thumbnailCache)
{
    thumbnail.addChangeListener (this);
}

WaveformView::~WaveformView()
{
    thumbnail.removeChangeListener (this);
}

void WaveformView::setPad (int newPad)
{
    pad = newPad;

    auto& params = nzProcessor.getParameters();

    startAttachment = std::make_unique<juce::ParameterAttachment> (
        *params.getParameter (NzDrumProcessor::paramId (pad, "start")),
        [this] (float value) { start = value; repaint(); });

    endAttachment = std::make_unique<juce::ParameterAttachment> (
        *params.getParameter (NzDrumProcessor::paramId (pad, "end")),
        [this] (float value) { end = value; repaint(); });

    startAttachment->sendInitialUpdate();
    endAttachment->sendInitialUpdate();

    shownFile = juce::File();
    refresh();
}

void WaveformView::refresh()
{
    const auto file = nzProcessor.getPadFile (pad);

    if (file != shownFile)
    {
        shownFile = file;

        // AudioThumbnail reads the file on its own thread and notifies us
        // (changeListenerCallback) as it gets data to draw.
        if (file.existsAsFile())
            thumbnail.setSource (new juce::FileInputSource (file));
        else
            thumbnail.clear();

        repaint();
    }
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return { 8.0f, 6.0f, (float) getWidth() - 16.0f, 88.0f };
}

float WaveformView::xFor (float proportion) const
{
    const auto area = waveArea();
    return area.getX() + area.getWidth() * proportion;
}

float WaveformView::proportionAt (float x) const
{
    const auto area = waveArea();
    return juce::jlimit (0.0f, 1.0f, (x - area.getX()) / juce::jmax (1.0f, area.getWidth()));
}

WaveformView::Marker WaveformView::markerNear (float x) const
{
    const float toStart = std::abs (x - xFor (start));
    const float toEnd = std::abs (x - xFor (end));

    if (juce::jmin (toStart, toEnd) > markerGrabDistance)
        return Marker::none;

    return toStart <= toEnd ? Marker::start : Marker::end;
}

juce::ParameterAttachment& WaveformView::attachmentFor (Marker marker)
{
    return marker == Marker::start ? *startAttachment : *endAttachment;
}

void WaveformView::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto bounds = getLocalBounds().toFloat();
    drawDisplay (g, bounds, 6.0f);

    const auto area = waveArea();

    if (thumbnail.getTotalLength() <= 0.0)
    {
        drawText (g, "LOAD A SAMPLE", font::mono (13.0f), colour::displayInk.withAlpha (0.6f),
                  area, juce::Justification::centred);
        return;
    }

    // The part that plays sits on a lighter "paper".
    const auto playing = area.withLeft (xFor (start)).withRight (xFor (end));
    g.setColour (colour::displayPaper);
    g.fillRect (playing);

    g.setColour (colour::displayInk.withAlpha (0.35f));
    g.fillRect (area.withHeight (1.0f).withCentre (area.getCentre()));

    // The wave: faded outside start and end, dark inside.
    g.setColour (colour::displayInk.withAlpha (0.35f));
    thumbnail.drawChannels (g, area.toNearestInt(), 0.0, thumbnail.getTotalLength(), 1.0f);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (playing.toNearestInt());
        g.setColour (juce::Colour (0xff2f3a24));
        thumbnail.drawChannels (g, area.toNearestInt(), 0.0, thumbnail.getTotalLength(), 1.0f);
    }

    // Markers: a red line with a flag on top that shows where to grab it.
    for (const float proportion : { start, end })
    {
        const float x = xFor (proportion);
        g.setColour (colour::logoRed);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY(), 2.0f, area.getHeight()));

        juce::Path flag;
        flag.addTriangle ({ x - 6.0f, area.getY() }, { x + 6.0f, area.getY() }, { x, area.getY() + 8.0f });
        g.fillPath (flag);
    }

    const auto percent = [] (float value) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const juce::Rectangle<float> readouts { area.getX(), 96.0f, area.getWidth(), 16.0f };
    drawText (g, "START " + percent (start), font::mono (12.0f), colour::displayInk, readouts, juce::Justification::centredLeft);
    drawText (g, "END " + percent (end), font::mono (12.0f), colour::displayInk, readouts, juce::Justification::centredRight);
}

void WaveformView::mouseMove (const juce::MouseEvent& event)
{
    setMouseCursor (markerNear (event.position.x) == Marker::none ? juce::MouseCursor::NormalCursor
                                                                   : juce::MouseCursor::LeftRightResizeCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& event)
{
    if (thumbnail.getTotalLength() <= 0.0)
        return;

    dragging = event.position.y <= waveArea().getBottom() ? markerNear (event.position.x) : Marker::none;

    if (dragging != Marker::none)
        attachmentFor (dragging).beginGesture();
}

void WaveformView::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging == Marker::none)
        return;

    moveMarker (dragging, proportionAt (event.position.x));
}

void WaveformView::moveMarker (Marker marker, float value)
{
    // Start can't pass end, and vice versa.
    constexpr float minimumGap = 0.001f;

    if (marker == Marker::start)
        attachmentFor (marker).setValueAsPartOfGesture (juce::jlimit (0.0f, end - minimumGap, value));
    else
        attachmentFor (marker).setValueAsPartOfGesture (juce::jlimit (start + minimumGap, 1.0f, value));
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (thumbnail.getTotalLength() <= 0.0 || dragging != Marker::none)
    {
        Component::mouseWheelMove (event, wheel);
        return;
    }

    // The wheel alone moves the start; with Shift, the end. On macOS,
    // Shift + wheel arrives as horizontal scroll (deltaX), so we take the axis
    // that moved most. With "natural" scrolling the direction is flipped, as
    // JUCE sliders do: scrolling up always moves to the right.
    float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (wheel.isReversed)
        delta = -delta;

    if (std::abs (delta) < 1.0e-6f)
        return;

    // One wheel notch (~0.1) moves about 1 % of the sample; with Alt, ten
    // times finer, for trimming the attack of a hit.
    const float speed = event.mods.isAltDown() ? 0.01f : 0.1f;
    const auto marker = event.mods.isShiftDown() ? Marker::end : Marker::start;
    const float current = marker == Marker::start ? start : end;

    auto& attachment = attachmentFor (marker);
    attachment.beginGesture();
    moveMarker (marker, current + delta * speed);
    attachment.endGesture();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragging != Marker::none)
        attachmentFor (dragging).endGesture();

    dragging = Marker::none;
}

// ============================================================================
// PadEditorComponent
// ============================================================================

PadEditorComponent::PadEditorComponent (NzDrumProcessor& processor)
    : nzProcessor (processor), waveform (processor)
{
    addAndMakeVisible (waveform);

    struct Spec { const char* parameter; const char* label; bool bipolar; };
    const std::array<Spec, 4> specs { {
        { "level", "VOLUME", false }, { "pan", "PAN", true }, { "pitch", "PITCH", true }, { "decay", "DECAY", false }
    } };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.parameterName = specs[i].parameter;
        knob.label = specs[i].label;
        knob.slider.getProperties().set (NzLookAndFeel::bipolarProperty, specs[i].bipolar);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobLcdWidth, knobLcdHeight);
        addAndMakeVisible (knob.slider);
    }

    chromaticButton.setClickingTogglesState (true);
    chromaticButton.setBorderAlpha (0.45f);
    chromaticButton.setTooltip (juce::String ("Spreads the selected pad across the 16 pads, one semitone apart "
                                                    "(pad 9 is the original pitch). For live playing and recording: "
                                                    "existing steps play as before."));
    chromaticButton.onClick = [this]
    {
        nzProcessor.setChromaticSource (chromaticButton.getToggleState() ? currentPad : -1);
    };
    addAndMakeVisible (chromaticButton);
}

void PadEditorComponent::refresh()
{
    waveform.refresh();

    // The mode lives in the processor: if the window is closed and reopened,
    // the button shows what is really going on.
    chromaticButton.setToggleState (nzProcessor.getChromaticSource() >= 0, juce::dontSendNotification);

    const auto name = nzProcessor.getPadFile (currentPad).getFileName().toUpperCase();

    if (name != sampleName)
    {
        sampleName = name;
        repaint();
    }
}

void PadEditorComponent::setPad (int pad)
{
    currentPad = pad;
    waveform.setPad (pad);

    // Each attachment connects a knob to the selected pad's parameter. They
    // are replaced when the pad changes; the old one disconnects itself.
    for (auto& knob : knobs)
    {
        knob.attachment.reset();
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            nzProcessor.getParameters(), NzDrumProcessor::paramId (pad, knob.parameterName), knob.slider);
    }

    sampleName = nzProcessor.getPadFile (pad).getFileName().toUpperCase();
    repaint();
}

void PadEditorComponent::setDragSensitivity (int pixels)
{
    for (auto& knob : knobs)
        knob.slider.setMouseDragSensitivity (pixels);
}

void PadEditorComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto sample = juce::String::fromUTF8 ("SAMPLE \xc2\xb7 ") + (sampleName.isEmpty() ? juce::String ("EMPTY") : sampleName);
    drawText (g, sample, font::label(), colour::secondary, { 14.0f, 6.0f, 360.0f, 12.0f }, juce::Justification::centredLeft);

    const auto controls = "PAD " + juce::String (currentPad + 1).paddedLeft ('0', 2) + " CONTROLS";
    drawText (g, controls, font::label(), colour::secondary, { (float) knobColumnsX, 6.0f, 180.0f, 12.0f }, juce::Justification::centredLeft);

    for (size_t i = 0; i < knobs.size(); ++i)
        drawText (g, knobs[i].label, font::label(), colour::secondary,
                  { (float) (knobColumnsX + (int) i * knobColumnWidth), (float) knobLabelY, (float) knobColumnWidth, 12.0f },
                  juce::Justification::centred);
}

void PadEditorComponent::resized()
{
    waveform.setBounds (waveDisplay);
    chromaticButton.setBounds (594, 2, 92, 20);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int columnX = knobColumnsX + (int) i * knobColumnWidth;
        knobs[i].slider.setBounds (columnX + (knobColumnWidth - knobLcdWidth) / 2, knobTop,
                                   knobLcdWidth, knobSize + 6 + knobLcdHeight);
    }
}

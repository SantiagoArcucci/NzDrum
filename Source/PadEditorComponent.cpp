#include "PadEditorComponent.h"

namespace
{
    const juce::Colour backgroundColour { 0xff23262d };
    const juce::Colour waveColour { 0xffb9bcc4 };
    const juce::Colour accentColour { 0xffff8a3d };
    const juce::Colour textColour { 0xffe8e8ea };
    constexpr float markerGrabDistance = 8.0f;
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

float WaveformView::xFor (float proportion) const
{
    return (float) getWidth() * proportion;
}

float WaveformView::proportionAt (float x) const
{
    return juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth()));
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
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (backgroundColour);
    g.fillRoundedRectangle (bounds, 6.0f);

    if (thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour (textColour.withAlpha (0.4f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String ("Load a sample to see its waveform"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour (waveColour);
    thumbnail.drawChannels (g, getLocalBounds().reduced (0, 6), 0.0, thumbnail.getTotalLength(), 1.0f);

    // Whatever lies outside start and end is darkened: it won't play.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (bounds.withRight (xFor (start)));
    g.fillRect (bounds.withLeft (xFor (end)));

    g.setColour (accentColour);

    for (const float proportion : { start, end })
    {
        const float x = xFor (proportion);
        g.drawLine (x, 0.0f, x, bounds.getHeight(), 2.0f);
    }

    // A small "flag" on each marker shows where to grab it.
    g.fillRect (xFor (start), 0.0f, 8.0f, 8.0f);
    g.fillRect (xFor (end) - 8.0f, bounds.getHeight() - 8.0f, 8.0f, 8.0f);
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

    dragging = markerNear (event.position.x);

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

    const std::array<std::pair<const char*, const char*>, 4> names { {
        { "level", "Level" }, { "pan", "Pan" }, { "pitch", "Pitch" }, { "decay", "Decay" }
    } };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.parameterName = names[i].first;
        knob.label.setText (names[i].second, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setColour (juce::Label::textColourId, textColour.withAlpha (0.7f));
        knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 18);
        addAndMakeVisible (knob.slider);
        addAndMakeVisible (knob.label);
    }

    chromaticButton.setClickingTogglesState (true);
    chromaticButton.setColour (juce::TextButton::buttonOnColourId, accentColour);
    chromaticButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
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
}

void PadEditorComponent::resized()
{
    auto area = getLocalBounds();
    auto buttonArea = area.removeFromRight (92);
    chromaticButton.setBounds (buttonArea.withSizeKeepingCentre (84, 32));

    auto knobArea = area.removeFromRight (4 * 76);
    waveform.setBounds (area.reduced (0, 4).withTrimmedRight (12));

    for (auto& knob : knobs)
    {
        auto column = knobArea.removeFromLeft (76);
        knob.label.setBounds (column.removeFromTop (18));
        knob.slider.setBounds (column.reduced (4, 0));
    }
}

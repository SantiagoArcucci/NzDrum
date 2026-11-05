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

        // AudioThumbnail lee el archivo en su propio hilo y nos avisa
        // (changeListenerCallback) a medida que tiene datos para dibujar.
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
        g.drawText (juce::String::fromUTF8 ("Cargá un sample para ver su forma de onda"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour (waveColour);
    thumbnail.drawChannels (g, getLocalBounds().reduced (0, 6), 0.0, thumbnail.getTotalLength(), 1.0f);

    // Lo que queda fuera de inicio y fin se oscurece: no va a sonar.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (bounds.withRight (xFor (start)));
    g.fillRect (bounds.withLeft (xFor (end)));

    g.setColour (accentColour);

    for (const float proportion : { start, end })
    {
        const float x = xFor (proportion);
        g.drawLine (x, 0.0f, x, bounds.getHeight(), 2.0f);
    }

    // Una "banderita" en cada marcador para que se vea dónde agarrarlo.
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
    // Inicio no puede pasar a fin ni al revés.
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

    // Rueda sola mueve el inicio; con Shift, el fin. En macOS, Shift + rueda
    // llega como scroll horizontal (deltaX), así que tomamos el eje que más
    // se movió. Con scroll "natural" el sentido se invierte, como en las
    // perillas de JUCE: girar hacia arriba siempre mueve a la derecha.
    float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (wheel.isReversed)
        delta = -delta;

    if (std::abs (delta) < 1.0e-6f)
        return;

    // Una muesca de rueda (~0.1) mueve cerca de un 1 % del sample; con Alt,
    // diez veces más fino para ajustar el ataque de un golpe.
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
        { "level", "Volumen" }, { "pan", "Pan" }, { "pitch", "Pitch" }, { "decay", "Decay" }
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
}

void PadEditorComponent::setPad (int pad)
{
    waveform.setPad (pad);

    // Cada attachment conecta una perilla con el parámetro del pad elegido.
    // Al cambiar de pad se reemplazan; el anterior se desconecta solo.
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
    auto knobArea = area.removeFromRight (4 * 76);
    waveform.setBounds (area.reduced (0, 4).withTrimmedRight (12));

    for (auto& knob : knobs)
    {
        auto column = knobArea.removeFromLeft (76);
        knob.label.setBounds (column.removeFromTop (18));
        knob.slider.setBounds (column.reduced (4, 0));
    }
}

#include "Theme.h"
#include "BinaryData.h"

// ============================================================================
// Fonts
// ============================================================================

namespace
{
    juce::Typeface::Ptr loadTypeface (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    // Loaded once, on first use (always on the UI thread).
    juce::Typeface::Ptr semiBoldFace()
    {
        static auto face = loadTypeface (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize);
        return face;
    }

    juce::Typeface::Ptr boldFace()
    {
        static auto face = loadTypeface (BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);
        return face;
    }

    juce::Typeface::Ptr boldItalicFace()
    {
        static auto face = loadTypeface (BinaryData::BarlowCondensedBoldItalic_ttf, BinaryData::BarlowCondensedBoldItalic_ttfSize);
        return face;
    }

    juce::Typeface::Ptr monoFace()
    {
        static auto face = loadTypeface (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
        return face;
    }

    // Sizes in the design are CSS pixels (the em size), which JUCE calls the
    // point height.
    juce::Font makeFont (juce::Typeface::Ptr face, float size)
    {
        return juce::Font (juce::FontOptions (face).withPointHeight (size));
    }
}

namespace theme::font
{
    juce::Font label (float size, float trackingPoints)
    {
        return makeFont (semiBoldFace(), size).withExtraKerningFactor (trackingPoints / size);
    }

    juce::Font semiBold (float size) { return makeFont (semiBoldFace(), size); }
    juce::Font bold (float size)     { return makeFont (boldFace(), size); }
    juce::Font logo (float size)     { return makeFont (boldItalicFace(), size); }
    juce::Font mono (float size)     { return makeFont (monoFace(), size); }
}

// ============================================================================
// Drawing helpers
// ============================================================================

namespace theme
{
    // Blurring a shadow happens on the CPU and is slow: drawn on every repaint,
    // the shadows made the DAW sluggish while the transport ran. So each one is
    // rendered once, at the screen's pixel scale, and kept in JUCE's
    // ImageCache, which drops it a few seconds after nothing uses it.
    //
    // A shadow is one colour, so only its alpha is stored (a byte per pixel)
    // and the colour is applied when drawing. The scale is rounded up to a
    // half step, so dragging the window corner reuses a handful of sizes
    // instead of storing new copies on every step of the drag.
    enum class ShadowKind { drop, inner };

    void drawCachedShadow (juce::Graphics& g, ShadowKind kind, juce::Rectangle<float> bounds, float cornerRadius,
                           juce::Colour colour, int radius, juce::Point<int> offset)
    {
        if (bounds.isEmpty())
            return;

        // Physical pixels per design pixel (window size times Retina).
        const float physicalScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const float scale = juce::jlimit (0.5f, 8.0f, std::ceil (physicalScale * 2.0f - 0.01f) * 0.5f);

        // A drop shadow reaches beyond the shape; an inner one stays inside it.
        const float margin = kind == ShadowKind::drop
                           ? (float) (radius + std::abs (offset.x) + std::abs (offset.y) + 1)
                           : 0.0f;
        const auto area = bounds.expanded (margin);

        const auto key = juce::String ((int) kind) + "|" + juce::String (bounds.getWidth(), 2) + "x"
                       + juce::String (bounds.getHeight(), 2) + "|" + juce::String (cornerRadius, 2) + "|"
                       + juce::String (colour.getAlpha()) + "|" + juce::String (radius) + "|" + offset.toString() + "|"
                       + juce::String (scale, 3);
        const auto hash = key.hashCode64();

        auto image = juce::ImageCache::getFromHashCode (hash);

        if (! image.isValid())
        {
            image = juce::Image (juce::Image::SingleChannel,
                                 juce::jmax (1, juce::roundToInt (area.getWidth() * scale)),
                                 juce::jmax (1, juce::roundToInt (area.getHeight() * scale)),
                                 true);

            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (scale));

            const auto local = juce::Rectangle<float> (margin, margin, bounds.getWidth(), bounds.getHeight());
            juce::Path shape;
            shape.addRoundedRectangle (local, cornerRadius);

            const auto mask = juce::Colours::black.withAlpha (colour.getAlpha());

            if (kind == ShadowKind::drop)
            {
                juce::DropShadow (mask, radius, offset).drawForPath (ig, shape);
            }
            else
            {
                ig.reduceClipRegion (shape);

                // A frame around the shape casts its shadow inwards.
                const float frameMargin = (float) (radius * 2 + std::abs (offset.x) + std::abs (offset.y) + 2);
                juce::Path frame;
                frame.addRectangle (local.expanded (frameMargin));
                frame.addRoundedRectangle (local, cornerRadius);
                frame.setUsingNonZeroWinding (false);

                juce::DropShadow (mask, juce::jmax (1, radius), offset).drawForPath (ig, frame);
            }

            juce::ImageCache::addImageToCache (image, hash);
        }

        g.setColour (colour.withAlpha (1.0f));
        g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale).translated (area.getX(), area.getY()), true);
    }

    void drawDropShadow (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                         juce::Colour colour, int radius, juce::Point<int> offset)
    {
        drawCachedShadow (g, ShadowKind::drop, bounds, cornerRadius, colour, radius, offset);
    }

    void drawGlow (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                   juce::Colour colour, int radius)
    {
        drawDropShadow (g, bounds, cornerRadius, colour, radius, {});
    }

    void drawInnerShadow (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                          juce::Colour colour, int radius, juce::Point<int> offset)
    {
        drawCachedShadow (g, ShadowKind::inner, bounds, cornerRadius, colour, radius, offset);
    }

    void drawText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                   juce::Rectangle<float> area, juce::Justification justification)
    {
        g.setFont (font);
        g.setColour (colour);
        g.drawText (text, area, justification, false);
    }

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        constexpr float radius = 6.0f;

        drawDropShadow (g, bounds, radius, juce::Colours::black.withAlpha (0.35f), 2, { 0, 1 });

        g.setColour (colour::panel);
        g.fillRoundedRectangle (bounds, radius);

        drawInnerShadow (g, bounds, radius, juce::Colours::black.withAlpha (0.25f), 1, { 0, -1 });

        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    void drawDisplay (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
    {
        // A light edge below makes it look set into the panel.
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), cornerRadius);

        g.setColour (colour::display);
        g.fillRoundedRectangle (bounds, cornerRadius);

        drawInnerShadow (g, bounds, cornerRadius, juce::Colours::black.withAlpha (0.6f), 4, { 0, 2 });

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);
    }

    juce::Colour drawUtilityKey (juce::Graphics& g, juce::Rectangle<float> bounds, bool active,
                                 float cornerRadius, float borderAlpha)
    {
        if (active)
        {
            drawGlow (g, bounds, cornerRadius, colour::active.withAlpha (0.6f), 6);
            g.setColour (colour::active);
            g.fillRoundedRectangle (bounds, cornerRadius);
        }
        else
        {
            drawDropShadow (g, bounds, cornerRadius, juce::Colours::black.withAlpha (0.35f), 2, { 0, 1 });
            g.setGradientFill (juce::ColourGradient::vertical (colour::ivoryLight, bounds.getY(),
                                                               colour::ivoryDark, bounds.getBottom()));
            g.fillRoundedRectangle (bounds, cornerRadius);

            // Light catching the top edge.
            juce::Graphics::ScopedSaveState state (g);
            juce::Path inside;
            inside.addRoundedRectangle (bounds.reduced (1.0f), juce::jmax (0.0f, cornerRadius - 1.0f));
            g.reduceClipRegion (inside);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillRect (bounds.reduced (1.0f).removeFromTop (1.0f));
        }

        g.setColour (juce::Colours::black.withAlpha (borderAlpha));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);

        return active ? juce::Colours::black : colour::control;
    }

    void drawLed (juce::Graphics& g, juce::Rectangle<float> bounds, bool on)
    {
        if (on)
        {
            drawGlow (g, bounds, bounds.getWidth() * 0.5f, colour::accent.withAlpha (0.8f), 5);
            g.setColour (colour::accent);
        }
        else
        {
            g.setColour (colour::active.withAlpha (0.3f));
        }

        g.fillEllipse (bounds);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (bounds.reduced (0.25f), 0.5f);
    }
}

// ============================================================================
// ThemeKey
// ============================================================================

ThemeKey::ThemeKey (const juce::String& buttonText, float size, bool led)
    : juce::Button (buttonText), fontSize (size), hasLed (led)
{
}

void ThemeKey::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat();

    // Keys with an LED don't turn red: the LED shows the state.
    const bool active = getToggleState() && ! hasLed;
    auto textColour = theme::drawUtilityKey (g, bounds, active, 3.0f, borderAlpha);

    if (isButtonDown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.08f));
        g.fillRoundedRectangle (bounds, 3.0f);
    }
    else if (isMouseOver)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (bounds, 3.0f);
    }

    if (! isEnabled())
        textColour = textColour.withAlpha (0.35f);

    if (hasLed)
    {
        const float ledSize = 6.0f;
        theme::drawLed (g, { bounds.getX() + 7.0f, bounds.getCentreY() - ledSize * 0.5f, ledSize, ledSize }, getToggleState());
        bounds.removeFromLeft (12.0f);
    }

    theme::drawText (g, getButtonText(), theme::font::bold (fontSize), textColour, bounds, juce::Justification::centred);
}

// ============================================================================
// NzLookAndFeel
// ============================================================================

NzLookAndFeel::NzLookAndFeel()
{
    using namespace theme;

    setColour (juce::PopupMenu::backgroundColourId, colour::ivoryLight);
    setColour (juce::PopupMenu::textColourId, colour::primary);
    setColour (juce::PopupMenu::headerTextColourId, colour::secondary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::active);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::lightInk);

    setColour (juce::TooltipWindow::backgroundColourId, colour::ivoryLight);
    setColour (juce::TooltipWindow::textColourId, colour::primary);
    setColour (juce::TooltipWindow::outlineColourId, colour::secondary);

    // Typing a value into a knob's LCD.
    setColour (juce::TextEditor::backgroundColourId, colour::display);
    setColour (juce::TextEditor::textColourId, colour::displayInk);
    setColour (juce::TextEditor::highlightColourId, colour::displayInk.withAlpha (0.25f));
    setColour (juce::TextEditor::highlightedTextColourId, colour::displayInk);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, colour::active);
    setColour (juce::CaretComponent::caretColourId, colour::displayInk);
    setColour (juce::Label::textWhenEditingColourId, colour::displayInk);
}

void NzLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace theme;

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Scale marks around the knob.
    constexpr int ticks = 11;
    const float tickInner = size * 0.39f, tickOuter = size * 0.45f;
    g.setColour (colour::secondary.withAlpha (0.55f));

    for (int i = 0; i < ticks; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (ticks - 1);
        const auto direction = juce::Point<float> (std::sin (a), -std::cos (a));
        g.drawLine ({ centre + direction * tickInner, centre + direction * tickOuter }, 1.0f);
    }

    // The red arc shows the value: from the minimum, or from the centre for
    // bipolar controls (pan, pitch).
    const bool bipolar = (bool) slider.getProperties().getWithDefault (NzLookAndFeel::bipolarProperty, false);
    const float arcFrom = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - arcFrom) > 0.01f)
    {
        const float arcRadius = size * 0.47f;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                           juce::jmin (arcFrom, angle), juce::jmax (arcFrom, angle), true);
        g.setColour (colour::active);
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // The body: a dark cap with a lighter rim.
    const float bodyRadius = size * 0.31f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 3, { 0, 2 }).drawForPath (g, bodyPath);

    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff6a625a), body.getY(),
                                                       juce::Colour (0xff1c1916), body.getBottom()));
    g.fillEllipse (body);

    const auto cap = body.reduced (size * 0.035f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3833), cap.getX() + cap.getWidth() * 0.3f, cap.getY() + cap.getHeight() * 0.25f,
                                             juce::Colour (0xff141210), cap.getRight(), cap.getBottom(), true));
    g.fillEllipse (cap);

    // Pointer.
    const auto direction = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (juce::Colour (0xfff6f1e7));
    g.drawLine ({ centre + direction * (bodyRadius * 0.25f), centre + direction * (bodyRadius * 0.85f) }, size * 0.05f);

    if (! slider.isEnabled())
    {
        g.setColour (colour::panel.withAlpha (0.5f));
        g.fillEllipse (body);
    }
}

juce::Slider::SliderLayout NzLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    juce::Slider::SliderLayout layout;
    auto area = slider.getLocalBounds();

    if (slider.getTextBoxPosition() == juce::Slider::TextBoxRight)
    {
        // Swing: the knob on the left and its LCD on the right.
        const int knob = area.getHeight();
        layout.sliderBounds = area.removeFromLeft (knob);
        area.removeFromLeft (6);
        layout.textBoxBounds = area.withSizeKeepingCentre (area.getWidth(), slider.getTextBoxHeight()).translated (0, -1);
    }
    else if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        // Pad knobs: the knob on top and its LCD below.
        layout.textBoxBounds = area.removeFromBottom (slider.getTextBoxHeight())
                                   .withSizeKeepingCentre (slider.getTextBoxWidth(), slider.getTextBoxHeight());
        area.removeFromBottom (6);
        const int knob = juce::jmin (area.getWidth(), area.getHeight());
        layout.sliderBounds = area.withSizeKeepingCentre (knob, knob);
    }
    else
    {
        layout.sliderBounds = area;
    }

    return layout;
}

juce::Label* NzLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->getProperties().set ("lcd", true);
    label->setJustificationType (juce::Justification::centred);
    label->setFont (theme::font::mono (slider.getTextBoxPosition() == juce::Slider::TextBoxRight ? 14.0f : 13.0f));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    return label;
}

void NzLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (! (bool) label.getProperties().getWithDefault ("lcd", false))
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    const auto bounds = label.getLocalBounds().toFloat();
    theme::drawDisplay (g, bounds, 4.0f);

    if (! label.isBeingEdited())
        theme::drawText (g, label.getText(), label.getFont(), theme::colour::displayInk,
                         bounds.reduced (3.0f, 0.0f), juce::Justification::centred);
}

juce::Font NzLookAndFeel::getPopupMenuFont()
{
    return theme::font::semiBold (15.0f);
}

juce::TextLayout NzLookAndFeel::layoutTooltipText (const juce::String& text, juce::Colour colour) noexcept
{
    constexpr float maxWidth = 360.0f;

    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, theme::font::semiBold (13.0f), colour);

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, maxWidth);
    return layout;
}

juce::Rectangle<int> NzLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                                      juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltipText (text, juce::Colours::black);
    const int w = (int) (layout.getWidth() + 16.0f);
    const int h = (int) (layout.getHeight() + 10.0f);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
        .constrainedWithin (parentArea);
}

void NzLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();

    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId).withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    layoutTooltipText (text, findColour (juce::TooltipWindow::textColourId))
        .draw (g, bounds.reduced (8.0f, 5.0f));
}

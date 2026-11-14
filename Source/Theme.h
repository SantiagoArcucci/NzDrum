#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The "Crema 80s" look: colours, fonts and the drawing helpers shared by every
// component. The values come from the Figma file (design tokens), so the names
// follow its roles: surface, text, signal.
namespace theme
{
    namespace colour
    {
        // Surfaces
        inline const juce::Colour chassisTop { 0xffece5d6 };
        inline const juce::Colour chassisBottom { 0xffd6cdb9 };
        inline const juce::Colour panel { 0xffe2d9c7 };
        inline const juce::Colour display { 0xffb4c09a };     // LCD
        inline const juce::Colour displayPaper { 0xffd9d1bd }; // waveform area that plays

        // Ink
        inline const juce::Colour displayInk { 0xff26301c };
        inline const juce::Colour primary { 0xff2b2620 };
        inline const juce::Colour secondary { 0xff5a5044 };
        inline const juce::Colour control { 0xff3a332b };
        inline const juce::Colour lightInk { 0xfffff7ea };

        // Signals
        inline const juce::Colour active { 0xffe2482a };
        inline const juce::Colour accent { 0xffff1a00 };
        inline const juce::Colour inactive { 0xff6d5f50 };
        inline const juce::Colour missing { 0xffd42a1a };
        inline const juce::Colour logoRed { 0xffd8432c };

        // Keys
        inline const juce::Colour ivoryLight { 0xfff6f1e7 };
        inline const juce::Colour ivoryDark { 0xffdcd4c4 };

        // Step keys, one colour per beat: steps 1-4, 5-8, 9-12, 13-16.
        struct Pair { juce::Colour light, dark; };
        inline const std::array<Pair, 4> beats { {
            { juce::Colour (0xffe0573d), juce::Colour (0xffc4402a) },   // red
            { juce::Colour (0xfff0943d), juce::Colour (0xffd77b26) },   // orange
            { juce::Colour (0xfff3cf5a), juce::Colour (0xffdbb43e) },   // yellow
            { juce::Colour (0xfff6f1e7), juce::Colour (0xffddd5c6) },   // ivory
        } };
    }

    // Embedded fonts (Google Fonts, SIL Open Font License).
    namespace font
    {
        // Panel labels: "SWING", "PATTERN BANK". Tracking is in points.
        juce::Font label (float size = 10.0f, float trackingPoints = 1.2f);
        juce::Font semiBold (float size);
        juce::Font bold (float size);
        juce::Font logo (float size);
        juce::Font mono (float size);   // LCD readouts
    }

    // A cream panel with molded edges (header, pads, pattern bank...).
    void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds);

    // A recessed sage LCD with a dark border.
    void drawDisplay (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius = 4.0f);

    // A small ivory key, or red when active. Returns the colour for its text.
    juce::Colour drawUtilityKey (juce::Graphics&, juce::Rectangle<float> bounds, bool active,
                                 float cornerRadius = 3.0f, float borderAlpha = 1.0f);

    // A round LED, lit or off.
    void drawLed (juce::Graphics&, juce::Rectangle<float> bounds, bool on);

    // A soft coloured halo around a rounded rectangle.
    void drawGlow (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius,
                   juce::Colour colour, int radius);

    // Shadow inside a rounded rectangle (like CSS inset box-shadow).
    void drawInnerShadow (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius,
                          juce::Colour colour, int radius, juce::Point<int> offset);

    // Shadow below a rounded rectangle (like CSS box-shadow).
    void drawDropShadow (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius,
                         juce::Colour colour, int radius, juce::Point<int> offset);

    // Sets the font and colour and draws one line of text.
    void drawText (juce::Graphics&, const juce::String& text, const juce::Font& font, juce::Colour colour,
                   juce::Rectangle<float> area, juce::Justification justification);
}

// A key drawn in the theme: the ivory mini keys of the pads, Rec, 16 Levels.
// With `hasLed` the key shows a small LED on its left instead of turning red.
class ThemeKey final : public juce::Button
{
public:
    explicit ThemeKey (const juce::String& buttonText, float fontSize = 10.0f, bool hasLed = false);

    void setBorderAlpha (float alpha) { borderAlpha = alpha; repaint(); }

private:
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    float fontSize;
    bool hasLed;
    float borderAlpha = 1.0f;
};

// Knobs, their LCD value boxes, popup menus and tooltips.
class NzLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NzLookAndFeel();

    // Knobs with this property draw their arc from the centre (pan, pitch).
    static constexpr const char* bipolarProperty = "bipolar";

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    juce::TextLayout layoutTooltipText (const juce::String&, juce::Colour) noexcept;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
};

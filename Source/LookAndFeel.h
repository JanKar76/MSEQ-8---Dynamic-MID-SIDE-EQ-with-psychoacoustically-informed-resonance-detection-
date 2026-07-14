#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** Colour theme (dark, matches the reference UI). */
namespace Theme
{
    const juce::Colour background   { 0xff0d0f0e };
    const juce::Colour panel        { 0xff141716 };
    const juce::Colour panelLight   { 0xff1c201e };
    const juce::Colour outline      { 0xff262b29 };
    const juce::Colour text         { 0xffb8c0bc };
    const juce::Colour textDim      { 0xff5c6662 };
    const juce::Colour mid          { 0xff5fb89a };  // green: mid curve / mid mode
    const juce::Colour side         { 0xffc98a52 };  // orange: side curve / side mode
    const juce::Colour midSide      { 0xff9b9bb5 };  // purple-grey: mid+side
    const juce::Colour meterFill    { 0xff5fb89a };
    const juce::Colour spectrumPre  { 0xff8a938f };  // neutral grey: pre-EQ spectra

    inline juce::Colour bandColour (int msMode)  // 0=Mid, 1=Mid+Side, 2=Side
    {
        return msMode == 0 ? mid : (msMode == 2 ? side : midSide);
    }
}

//==============================================================================
/** Custom LookAndFeel: the rotary knobs (thin arc + pointer) plus a
    consistent theme for PopupMenu/ComboBox/AlertWindow/TextButton. Set as
    the global default (see PluginEditor) so all menus and dialogs — e.g.
    the right-click menus and "Save preset..." — match the rest of the
    dark, compact UI instead of JUCE's default look (wrong colours, font
    too large), which would otherwise stick out and clash in a dark mixing
    environment. */
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, Theme::mid);
        setColour (juce::Slider::textBoxTextColourId, Theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        // PopupMenu (right-click menus, the ComboBox list)
        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::headerTextColourId, Theme::textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::mid.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, Theme::text);

        // ComboBox (the preset selector)
        setColour (juce::ComboBox::backgroundColourId, Theme::panelLight);
        setColour (juce::ComboBox::textColourId, Theme::text);
        setColour (juce::ComboBox::outlineColourId, Theme::outline);
        setColour (juce::ComboBox::arrowColourId, Theme::textDim);
        setColour (juce::ComboBox::buttonColourId, Theme::panelLight);

        // TextButton (default for buttons that aren't coloured per-instance)
        setColour (juce::TextButton::buttonColourId, Theme::panelLight);
        setColour (juce::TextButton::buttonOnColourId, Theme::mid);
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, Theme::background);

        // AlertWindow ("Save preset..." etc.) + its TextEditor
        setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
        setColour (juce::AlertWindow::textColourId, Theme::text);
        setColour (juce::AlertWindow::outlineColourId, Theme::outline);
        setColour (juce::TextEditor::backgroundColourId, Theme::panelLight);
        setColour (juce::TextEditor::textColourId, Theme::text);
        setColour (juce::TextEditor::outlineColourId, Theme::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::mid);
    }

    // Smaller, consistent font size in menus/comboboxes — JUCE's default is
    // noticeably larger than the rest of the compact UI (9.5-13 pt throughout).
    juce::Font getPopupMenuFont() override        { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override
                                                    { return juce::Font (juce::FontOptions (12.5f)); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                    (float) width, (float) height).reduced (3.0f);
        const auto radius  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre  = bounds.getCentre();
        const auto angle   = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto lineW   = juce::jmax (2.0f, radius * 0.16f);
        const auto arcR    = radius - lineW * 0.5f;

        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        // Background arc
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (Theme::outline);
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Value arc
        if (sliderPos > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                                 rotaryStartAngle, angle, true);
            g.setColour (slider.isEnabled() ? accent : Theme::textDim);
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Pointer
        juce::Path pointer;
        pointer.startNewSubPath (centre.getPointOnCircumference (arcR * 0.35f, angle));
        pointer.lineTo (centre.getPointOnCircumference (arcR * 0.85f, angle));
        g.setColour (Theme::text);
        g.strokePath (pointer, juce::PathStrokeType (lineW * 0.8f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }
};

//==============================================================================
/** Simple vertical peak meter, driven by an atomic level polled via a Timer in the editor. */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        // Fast attack, slow release
        smoothed = newLevel > smoothed ? newLevel : smoothed * 0.92f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (r, 2.0f);

        const float db     = juce::Decibels::gainToDecibels (smoothed, -60.0f);
        const float norm   = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

        auto fill = r.removeFromBottom (r.getHeight() * norm);
        g.setColour (db > -3.0f ? Theme::side : Theme::meterFill);
        g.fillRoundedRectangle (fill, 2.0f);
    }

private:
    float smoothed = 0.0f;
};

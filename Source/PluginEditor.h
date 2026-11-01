#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"
#include "VuMeter.h"

class AudioLabOptoEditor : public juce::AudioProcessorEditor,
                           private juce::DarkModeSettingListener
{
public:
    explicit AudioLabOptoEditor (AudioLabOptoProcessor&);
    ~AudioLabOptoEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // Grille de 24 px (docs/06_GUI_Plugin_Compresseur.md) : Gain | VU | Peak Reduction, rangée du bas
    static constexpr int baseW = 634, baseH = 316;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (AudioLabOptoEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        AudioLabOptoEditor& ed;
    };

    void applyTheme();
    void refreshState();
    void darkModeSettingChanged() override { applyTheme(); }

    AudioLabOptoProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    std::unique_ptr<audiolab::ArcKnob> kGain, kPR, kMix;
    audiolab::VuFace vu;
    audiolab::SegButtons meterSeg { theme, { "GR", "+4", "+10" } };
    audiolab::SegButtons modeSeg { theme, { "Compress", "Limit" } };
    std::unique_ptr<juce::ParameterAttachment> bypassAtt;
    juce::Array<juce::Component*> body;
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabOptoEditor)
};

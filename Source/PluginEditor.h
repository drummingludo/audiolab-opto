#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"
#include "VuMeter.h"

class AudioLabOptoEditor : public juce::AudioProcessorEditor
{
public:
    explicit AudioLabOptoEditor (AudioLabOptoProcessor&);
    ~AudioLabOptoEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // Charte V2 (fiche 02) et docs/09 : Gain | VU | Peak Reduction, rangée du bas
    static constexpr int baseW = 574, baseH = 292;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (AudioLabOptoEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        AudioLabOptoEditor& ed;
    };

    void refreshState();

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
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabOptoEditor)
};

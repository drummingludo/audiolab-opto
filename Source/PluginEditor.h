#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"
#include "VuMeter.h"

class AudioLabOptoEditor : public juce::AudioProcessorEditor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    explicit AudioLabOptoEditor (AudioLabOptoProcessor&);
    ~AudioLabOptoEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // Charte V2 (fiche 02) et docs/09 : Gain | VU | Peak Reduction, rangée du bas
    // 2.0.0 : panneau « Advanced settings » repliable (comme Lab Comp) : 574 × 292 replié, 574 × 400 déplié
    static constexpr int baseW = 574, baseH = 292, baseHOpen = 400;

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
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void setAdvanced (bool open, bool resizeWindow);
    int currentH() const { return advOpen ? baseHOpen : baseH; }
    juce::String latencyText() const;

    AudioLabOptoProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    std::unique_ptr<audiolab::ArcKnob> kGain, kPR, kMix;
    audiolab::VuFace vu;
    audiolab::SegButtons meterSeg { theme, { "GR", "+4", "+10" } };
    audiolab::SegButtons modeSeg { theme, { "Compress", "Limit" } };
    audiolab::SegButtons osSeg { theme, { "Off", juce::String::fromUTF8 ("2×"), juce::String::fromUTF8 ("4×"), juce::String::fromUTF8 ("8×") } };
    std::unique_ptr<audiolab::SliderRow> rowEmph;
    audiolab::Disclosure advBtn { theme, "Advanced settings" };
    std::unique_ptr<juce::ParameterAttachment> bypassAtt;
    juce::Array<juce::Component*> advItems;
    bool bypassed = false, advOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabOptoEditor)
};

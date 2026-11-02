#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

static const char* helpGain = "Output make-up (tube stage). Compression lowers the level: bring it back up here. "
                              "Always compare at equal loudness (bypass): louder sounds \"better\".";
static const char* helpPR   = "Peak Reduction: a single knob that sets both the threshold and the amount of compression. "
                              "The further you turn it, the harder the optical cell works. The response is slow and musical.";
static const char* helpMode = "Compress: gentle ratio (3.5:1). Limit: high ratio (12:1), to hold the peaks.";
static const char* helpMtr  = "What the needle shows: GR = gain reduction; +4 / +10 = output level "
                              "(0 VU = −18 / −12 dBFS).";
static const char* helpMix  = "Dry/Wet: blends the processed sound with the direct sound (parallel compression).";

AudioLabOptoEditor::AudioLabOptoEditor (AudioLabOptoProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      vu (theme, [&p] { const auto m = p.readMeters(); return VuFace::Reading { m.grDb, m.outDb, m.active }; })
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& ap = proc.apvts;
    auto prm = [&] (const char* id) -> juce::RangedAudioParameter& { return *ap.getParameter (id); };

    // couleurs de la charte : gain = teal secondaire, Peak Reduction (seuil + ratio) = teal, Mix = teal secondaire
    kGain = std::make_unique<ArcKnob> (theme, prm (params::gain), "Gain", "dB", Family::teal2, ArcKnob::Size::md,
        [] (float v) { return (v > 0 ? "+" : "") + String (v, 1); }, 0.0f, U (helpGain));
    kGain->hint = U ("output make-up");
    kPR = std::make_unique<ArcKnob> (theme, prm (params::peakRed), "Peak Reduction", "%", Family::teal, ArcKnob::Size::md,
        [] (float v) { return String (juce::roundToInt (v)); }, 0.0f, U (helpPR));
    kPR->hint = U ("linked threshold + ratio");
    kMix = std::make_unique<ArcKnob> (theme, prm (params::mix), "Mix", "%", Family::teal2, ArcKnob::Size::xs,
        [] (float v) { return String (juce::roundToInt (v)); }, 0.0f, U (helpMix));

    vu.setModes ({ -18.0f, -12.0f }, { "OUTPUT  +4", "OUTPUT  +10" }, false);
    const int mtr = juce::jlimit (0, 2, (int) proc.ui.getProperty ("meter", 0));
    meterSeg.setSelected (mtr); vu.setMode (mtr);
    meterSeg.onSelect = [this] (int i) { vu.setMode (i); proc.ui.setProperty ("meter", i, nullptr); };
    modeSeg.attach (prm (params::mode));

    bypassAtt = std::make_unique<juce::ParameterAttachment> (prm (params::bypass), [this] (float v) {
        bypassed = v > 0.5f; refreshState(); }, nullptr);
    bypassAtt->sendInitialUpdate();

    for (auto* c : std::initializer_list<juce::Component*> { kGain.get(), kPR.get(), kMix.get(), &vu, &meterSeg, &modeSeg })
        content.addAndMakeVisible (c);
    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));   // lu avant les limites
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (baseH * sc));
    refreshState();
}

AudioLabOptoEditor::~AudioLabOptoEditor()
{
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabOptoEditor::refreshState()
{
    // Charte V2 : en bypass, la lampe du VU s'éteint ; les réglages restent accessibles
    vu.setLampOff (bypassed);
}

void AudioLabOptoEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, baseH);
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

void AudioLabOptoEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (ed.theme.pal().bg);
}

void AudioLabOptoEditor::Content::resized()
{
    auto& e = ed;
    // Rangée principale (y 24 → 224) : Gain (centre x 75,5) | VU 270 × 200 | Peak Reduction (centre x 498,5)
    e.kGain->setBounds (4, 68, 143, 150);
    e.vu.setBounds (152, 24, 270, 200);
    e.kPR->setBounds (427, 68, 143, 150);
    // Rangée du bas (y 240) : affichage du VU à gauche, Compress / Limit sous le VU, petit Mix à droite
    e.meterSeg.setBounds (24, 240, juce::roundToInt (e.meterSeg.idealWidth()), 28);
    e.meterSeg.setTooltip (U (helpMtr));
    const int mw = juce::roundToInt (e.modeSeg.idealWidth());
    e.modeSeg.setBounds (152 + 135 - mw / 2, 240, mw, 28);
    e.modeSeg.setTooltip (U (helpMode));
    const int kw = juce::roundToInt (e.kMix->idealWidth());
    e.kMix->setBounds (baseW - 24 - kw, 240, kw, 28);
}

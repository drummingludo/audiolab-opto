#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

static const char* helpGain = "Rattrapage de sortie (étage à tubes). La compression baisse le niveau : on le remonte ici. "
                              "Comparez toujours à niveau égal (bypass) : plus fort paraît « mieux ».";
static const char* helpPR   = "Peak Reduction : un seul bouton qui règle à la fois le seuil et la quantité de compression. "
                              "Plus on tourne, plus la cellule optique travaille. La réaction est lente et musicale.";
static const char* helpMode = "Compress : ratio doux (3,5:1). Limit : ratio fort (12:1), pour tenir les crêtes.";
static const char* helpMtr  = "Ce que montre l'aiguille : GR = réduction de gain ; +4 / +10 = niveau de sortie "
                              "(0 VU = −18 / −12 dBFS).";
static const char* helpMix  = "Dry/Wet : mélange le son traité avec le son direct (compression parallèle).";

AudioLabOptoEditor::AudioLabOptoEditor (AudioLabOptoProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      vu (theme, [&p] { const auto m = p.readMeters(); return VuFace::Reading { m.grDb, m.outDb, m.active }; })
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& ap = proc.apvts;
    auto prm = [&] (const char* id) -> juce::RangedAudioParameter& { return *ap.getParameter (id); };
    auto acc = [] (const Pal& q) { return q.accent; };

    kGain = std::make_unique<ArcKnob> (theme, prm (params::gain), "Gain", "dB", acc, ArcKnob::Size::md,
        [] (float v) { return (v > 0 ? "+" : "") + String (v, 1); }, 0.0f, U (helpGain));
    kGain->hint = U ("rattrapage de sortie");
    kPR = std::make_unique<ArcKnob> (theme, prm (params::peakRed), "Peak Reduction", "%", acc, ArcKnob::Size::md,
        [] (float v) { return String (juce::roundToInt (v)); }, 0.0f, U (helpPR));
    kPR->hint = U ("seuil + ratio couplés");
    kMix = std::make_unique<ArcKnob> (theme, prm (params::mix), "Mix", "%", acc, ArcKnob::Size::xs,
        [] (float v) { return String (juce::roundToInt (v)); }, 0.0f, U (helpMix));

    vu.setModes ({ -18.0f, -12.0f }, { "OUTPUT  +4", "OUTPUT  +10" }, false);
    const int mtr = juce::jlimit (0, 2, (int) proc.ui.getProperty ("meter", 0));
    meterSeg.setSelected (mtr); vu.setMode (mtr);
    meterSeg.onSelect = [this] (int i) { vu.setMode (i); proc.ui.setProperty ("meter", i, nullptr); };
    modeSeg.attach (prm (params::mode));

    bypassAtt = std::make_unique<juce::ParameterAttachment> (prm (params::bypass), [this] (float v) {
        bypassed = v > 0.5f; refreshState(); }, nullptr);
    bypassAtt->sendInitialUpdate();

    body = { kGain.get(), kPR.get(), kMix.get(), &vu, &meterSeg, &modeSeg };
    for (auto* c : body) content.addAndMakeVisible (c);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    applyTheme();
    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));   // lu avant les limites
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (baseH * sc));
    refreshState();
}

AudioLabOptoEditor::~AudioLabOptoEditor()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabOptoEditor::applyTheme()
{
    // Clair / sombre selon le réglage de l'ordinateur (« themeOverride » sert seulement aux tests)
    const String t = proc.ui.getProperty ("themeOverride", "auto").toString();
    const bool dark = t == "dark" || (t != "light" && juce::Desktop::getInstance().isDarkModeActive());
    theme.setDark (dark);
    lnf.refresh();
    sendLookAndFeelChange();
    content.repaint();
    for (auto* c : content.getChildren()) c->repaint();
}

void AudioLabOptoEditor::refreshState()
{
    for (auto* c : body) { c->setAlpha (bypassed ? 0.35f : 1.0f); c->setEnabled (! bypassed); }
    meterSeg.setEnabled (true); meterSeg.setAlpha (1.0f);   // le choix d'affichage reste possible
    content.repaint();
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
    g.fillAll (ed.theme.pal().bg2);
}

void AudioLabOptoEditor::Content::resized()
{
    auto& e = ed;
    // Rangée principale (y 24 → 248) : Gain (centre x 75,5) | VU 330 × 224 | Peak Reduction (centre x 557,5)
    e.kGain->setBounds (4, 62, 143, 150);
    e.vu.setBounds (152, 24, 330, 224);
    e.kPR->setBounds (486, 62, 143, 150);
    // Rangée du bas (y 264) : affichage du VU à gauche, Compress / Limit sous le VU, petit Mix à droite
    e.meterSeg.setBounds (24, 264, juce::roundToInt (e.meterSeg.idealWidth()), 28);
    e.meterSeg.setTooltip (U (helpMtr));
    const int mw = juce::roundToInt (e.modeSeg.idealWidth());
    e.modeSeg.setBounds (152 + 165 - mw / 2, 264, mw, 28);
    e.modeSeg.setTooltip (U (helpMode));
    const int kw = juce::roundToInt (e.kMix->idealWidth());
    e.kMix->setBounds (baseW - 24 - kw, 264, kw, 28);
}

// EZ EQ Pro — implementation. 8 parametric bands, RBJ biquads (natural
// phase), cuts cascaded to 24 dB/oct. State persists via the standard
// getStateInformation/setStateInformation (project saves capture it).
#include "ezq4.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cmath>

namespace ezq4 {
namespace {

// ---- RBJ cookbook biquad -------------------------------------------------
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    // per-channel state
    double x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};

    void makeIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    static Biquad lowShelving(double sr, double f, double gainDb, double q) {
        Biquad c;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double S = 1.0;                 // shelf slope (=1/Q-ish)
        const double alpha = sw / 2.0 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2);
        const double twoSqrtAalpha = 2 * std::sqrt(A) * alpha;
        c.b0 = A * ((A + 1) - (A - 1) * cw + twoSqrtAalpha);
        c.b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        c.b2 = A * ((A + 1) - (A - 1) * cw - twoSqrtAalpha);
        const double a0 = (A + 1) + (A - 1) * cw + twoSqrtAalpha;
        c.a1 = -2 * ((A - 1) + (A + 1) * cw);
        c.a2 = (A + 1) + (A - 1) * cw - twoSqrtAalpha;
        c.normalize(a0);
        return c;
    }
    static Biquad highShelving(double sr, double f, double gainDb, double q) {
        Biquad c;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double S = 1.0;
        const double alpha = sw / 2.0 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2);
        const double twoSqrtAalpha = 2 * std::sqrt(A) * alpha;
        c.b0 = A * ((A + 1) + (A - 1) * cw + twoSqrtAalpha);
        c.b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        c.b2 = A * ((A + 1) + (A - 1) * cw - twoSqrtAalpha);
        const double a0 = (A + 1) - (A - 1) * cw + twoSqrtAalpha;
        c.a1 = 2 * ((A - 1) - (A + 1) * cw);
        c.a2 = (A + 1) - (A - 1) * cw - twoSqrtAalpha;
        c.normalize(a0);
        return c;
    }
    static Biquad peaking(double sr, double f, double gainDb, double q) {
        Biquad c;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2 * q);
        c.b0 = 1 + alpha * A;
        c.b1 = -2 * cw;
        c.b2 = 1 - alpha * A;
        const double a0 = 1 + alpha / A;
        c.a1 = -2 * cw;
        c.a2 = 1 - alpha / A;
        c.normalize(a0);
        return c;
    }
    static Biquad lowPass(double sr, double f, double q) {
        Biquad c;
        const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2 * q);
        c.b0 = (1 - cw) / 2;
        c.b1 = 1 - cw;
        c.b2 = (1 - cw) / 2;
        const double a0 = 1 + alpha;
        c.a1 = -2 * cw;
        c.a2 = 1 - alpha;
        c.normalize(a0);
        return c;
    }
    static Biquad highPass(double sr, double f, double q) {
        Biquad c;
        const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2 * q);
        c.b0 = (1 + cw) / 2;
        c.b1 = -(1 + cw);
        c.b2 = (1 + cw) / 2;
        const double a0 = 1 + alpha;
        c.a1 = -2 * cw;
        c.a2 = 1 - alpha;
        c.normalize(a0);
        return c;
    }
    static Biquad notching(double sr, double f, double q) {
        Biquad c;
        const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2 * q);
        c.b0 = 1;
        c.b1 = -2 * cw;
        c.b2 = 1;
        const double a0 = 1 + alpha;
        c.a1 = -2 * cw;
        c.a2 = 1 - alpha;
        c.normalize(a0);
        return c;
    }
    static Biquad allPass() { Biquad c; c.b0 = 1; return c; }

    void normalize(double a0) {
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    void reset() { for (int ch = 0; ch < 2; ++ch) reset(ch); }
    void reset(int ch) { x1[ch] = x2[ch] = y1[ch] = y2[ch] = 0; }
    inline float process(int ch, float in) {
        const double out = b0 * in + b1 * x1[ch] + b2 * x2[ch]
                         - a1 * y1[ch] - a2 * y2[ch];
        x2[ch] = x1[ch]; x1[ch] = in;
        y2[ch] = y1[ch]; y1[ch] = out;
        return (float) out;
    }
};

enum BandType { kBell = 0, kLowCut, kLowShelf, kHighShelf, kHighCut, kNotch };

} // namespace


// Initial band layout: spread across the spectrum so the graph shows one
// node per band instead of eight stacked at the same point. Types follow the
// AudioParameterChoice order: Bell, Low cut, Low shelf, High shelf, High cut,
// Notch. Bands 0-3 start enabled.
constexpr float kInitFreq[kNumBands] = {40.f, 150.f, 900.f, 5000.f,
                                        300.f, 2500.f, 9000.f, 16000.f};
constexpr int kInitTy[kNumBands] = {1, 0, 0, 0, 0, 0, 3, 4};

// ---- Editor: Pro-Q-style parameter table, one row per band ---------------
class EzQ4Editor;

class EzQ4Processor : public juce::AudioProcessor {
public:
    EzQ4Processor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        for (int b = 0; b < kNumBands; ++b) {
            const std::string p = std::to_string(b);
            bandEn_[b] = new juce::AudioParameterBool(
                juce::ParameterID{"b" + p + "en", 1}, "Band " + p + " on", b < 4);
            addParameter(bandEn_[b]);
            bandTy_[b] = new juce::AudioParameterChoice(
                juce::ParameterID{"b" + p + "ty", 1}, "Band " + p + " type",
                juce::StringArray{"Bell", "Low cut", "Low shelf", "High shelf",
                                  "High cut", "Notch"},
                kInitTy[b]);
            addParameter(bandTy_[b]);
            freq_[b] = new juce::AudioParameterFloat(
                juce::ParameterID{"b" + p + "f", 1}, "Band " + p + " freq",
                juce::NormalisableRange<float>(20.f, 20000.f, 0.f, 0.35f),
                kInitFreq[b]);
            addParameter(freq_[b]);
            gain_[b] = new juce::AudioParameterFloat(
                juce::ParameterID{"b" + p + "g", 1}, "Band " + p + " gain",
                juce::NormalisableRange<float>(-24.f, 24.f, 0.1f), 0.f);
            addParameter(gain_[b]);
            q_[b] = new juce::AudioParameterFloat(
                juce::ParameterID{"b" + p + "q", 1}, "Band " + p + " Q",
                juce::NormalisableRange<float>(0.1f, 8.f, 0.f, 0.4f), 0.7f);
            addParameter(q_[b]);
        }
        bypass_ = new juce::AudioParameterBool(
            juce::ParameterID{"byp", 1}, "Bypass", false);
        addParameter(bypass_);
        outGain_ = new juce::AudioParameterFloat(
            juce::ParameterID{"out", 1}, "Output",
            juce::NormalisableRange<float>(-12.f, 12.f, 0.1f), 0.f);
        addParameter(outGain_);

        for (int b = 0; b < kNumBands; ++b)
            for (int st = 0; st < 2; ++st)
                for (int ch = 0; ch < 2; ++ch)
                    biq_[b][st].reset(ch);
    }

    const juce::String getName() const override { return {"EZ EQ Pro"}; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {"Default"}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sr, int) override {
        sampleRate_ = sr;
        for (int b = 0; b < kNumBands; ++b)
            for (int st = 0; st < 2; ++st)
                for (int ch = 0; ch < 2; ++ch)
                    biq_[b][st].reset(ch);
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet() == l.getMainOutputChannelSet()
            && (l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
                || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
    }

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer&) override {
        if (bypass_->get()) return;
        // Recalc every block: ~50 flops for 8 bands, and it guarantees the
        // coefficients track automation (the old dirty-flag froze them at
        // defaults forever once prepared).
        recalc();
        if ((recalcCounter_++ % 200) == 0)
            juce::Logger::writeToLog(juce::String::formatted(
                "[ezq] sr=%.0f b2: en=%d ty=%d f=%.0f | outGain=%.2f",
                sampleRate_, (int) bandEn_[2]->get(), bandTy_[2]->getIndex(),
                freq_[2]->get(), outGain_->get()));
        const int numCh = juce::jmin(2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        const float outGain = juce::Decibels::decibelsToGain(outGain_->get());
        for (int b = 0; b < kNumBands; ++b) {
            if (! bandEnabled(b)) continue;
            const int stages = (bandType(b) == kLowCut || bandType(b) == kHighCut) ? 2 : 1;
            for (int st = 0; st < stages; ++st)
                for (int ch = 0; ch < numCh; ++ch) {
                    auto* data = buffer.getWritePointer(ch);
                    for (int j = 0; j < n; ++j)
                        data[j] = biq_[b][st].process(ch, data[j]);
                }
        }
        if (outGain != 1.0f)
            for (int ch = 0; ch < numCh; ++ch)
                buffer.applyGain(ch, 0, n, outGain);

        // Spectrum tap for the editor (post-EQ, lock-free SPSC FIFO).
        {
            int s1, n1, s2, n2;
            tapFifo_.prepareToWrite(n, s1, n1, s2, n2);
            const int rch = numCh > 1 ? 1 : 0;
            if (n1 > 0)
            {
                juce::FloatVectorOperations::copy(tapL_.data() + s1,
                                                  buffer.getReadPointer(0), n1);
                juce::FloatVectorOperations::copy(tapR_.data() + s1,
                                                  buffer.getReadPointer(rch), n1);
            }
            if (n2 > 0)
            {
                juce::FloatVectorOperations::copy(tapL_.data() + s2,
                                                  buffer.getReadPointer(0) + n1, n2);
                juce::FloatVectorOperations::copy(tapR_.data() + s2,
                                                  buffer.getReadPointer(rch) + n1, n2);
            }
            tapFifo_.finishedWrite(n1 + n2);
        }
    }

    // ---- state ----
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::XmlElement x("EZQ");
        x.setAttribute("version", 1);
        for (int b = 0; b < kNumBands; ++b) {
            auto* e = new juce::XmlElement("band");
            e->setAttribute("i", b);
            e->setAttribute("en", (int) (bandEnabled(b) ? 1 : 0));
            e->setAttribute("ty", bandType(b));
            e->setAttribute("f", freq_[b]->get());
            e->setAttribute("g", gain_[b]->get());
            e->setAttribute("q", q_[b]->get());
            x.addChildElement(e);
        }
        x.setAttribute("byp", (int) (bypass_->get() ? 1 : 0));
        x.setAttribute("out", outGain_->get());
        copyXmlToBinary(x, dest);
    }
    void setStateInformation(const void* data, int size) override {
        auto x = getXmlFromBinary(data, size);
        if (x == nullptr || x->getTagName() != "EZQ") return;
        for (auto* e : x->getChildWithTagNameIterator("band")) {
            const int b = e->getIntAttribute("i", -1);
            if (b < 0 || b >= kNumBands) continue;
            enableParam(b)->setValueNotifyingHost(
                enableParam(b)->convertTo0to1(e->getBoolAttribute("en", true) ? 1.f : 0.f));
            typeParam(b)->setValueNotifyingHost(typeParam(b)->convertTo0to1(
                (float) e->getIntAttribute("ty", 0)));
            freq_[b]->setValueNotifyingHost(
                freq_[b]->convertTo0to1((float) e->getDoubleAttribute("f", 1000.0)));
            gain_[b]->setValueNotifyingHost(
                gain_[b]->convertTo0to1((float) e->getDoubleAttribute("g", 0.0)));
            q_[b]->setValueNotifyingHost(
                q_[b]->convertTo0to1((float) e->getDoubleAttribute("q", 0.7)));
        }
        bypass_->setValueNotifyingHost(e2(x->getBoolAttribute("byp", false)));
        outGain_->setValueNotifyingHost(outGain_->convertTo0to1(
            (float) x->getDoubleAttribute("out", 0.0)));
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    friend class EzQ4Editor;

private:
    static bool e2(bool v) { return v; }

    bool bandEnabled(int b) const {
        return bandEn_[b] != nullptr ? bandEn_[b]->get() : true;
    }
    int bandType(int b) const {
        return bandTy_[b] != nullptr ? bandTy_[b]->getIndex() : 0;
    }
    juce::AudioParameterBool* enableParam(int b) { return bandEn_[b]; }
    juce::AudioParameterChoice* typeParam(int b) { return bandTy_[b]; }

    void recalc() {
        for (int b = 0; b < kNumBands; ++b) {
            const double f = freq_[b]->get();
            const double g = gain_[b]->get();
            const double q = juce::jmax(0.05, (double) q_[b]->get());
            const int ty = bandType(b);
            Biquad s0 = Biquad::allPass(), s1 = Biquad::allPass();
            if (bandEnabled(b)) {
                switch (ty) {
                    case kBell: s0 = Biquad::peaking(sampleRate_, f, g, q); break;
                    case kLowShelf: s0 = Biquad::lowShelving(sampleRate_, f, g, q); break;
                    case kHighShelf: s0 = Biquad::highShelving(sampleRate_, f, g, q); break;
                    case kNotch: s0 = Biquad::notching(sampleRate_, f, q); break;
                    case kLowCut: s0 = Biquad::highPass(sampleRate_, f, 0.707);
                                  s1 = Biquad::highPass(sampleRate_, f, 0.707); break;
                    case kHighCut: s0 = Biquad::lowPass(sampleRate_, f, 0.707);
                                   s1 = Biquad::lowPass(sampleRate_, f, 0.707); break;
                }
            }
            for (int ch = 0; ch < 2; ++ch) {
                biq_[b][0].b0 = s0.b0; biq_[b][0].b1 = s0.b1; biq_[b][0].b2 = s0.b2;
                biq_[b][0].a1 = s0.a1; biq_[b][0].a2 = s0.a2; biq_[b][0].reset(ch);
                biq_[b][1].b0 = s1.b0; biq_[b][1].b1 = s1.b1; biq_[b][1].b2 = s1.b2;
                biq_[b][1].a1 = s1.a1; biq_[b][1].a2 = s1.a2; biq_[b][1].reset(ch);
            }
        }
    }

    juce::AudioParameterBool* bandEn_[kNumBands] = {};
    juce::AudioParameterChoice* bandTy_[kNumBands] = {};
    juce::AudioParameterFloat* freq_[kNumBands] = {};
    juce::AudioParameterFloat* gain_[kNumBands] = {};
    juce::AudioParameterFloat* q_[kNumBands] = {};
    juce::AudioParameterBool* bypass_ = nullptr;
    juce::AudioParameterFloat* outGain_ = nullptr;

    std::array<std::array<Biquad, 2>, kNumBands> biq_ {};
    int recalcCounter_ = 0;
    double sampleRate_ = 48000.0;

    // Post-EQ tap ring (audio thread writes, editor timer reads).
    static constexpr int kTapCapacity = 1 << 14;
    juce::AbstractFifo tapFifo_ { kTapCapacity };
    std::array<float, kTapCapacity> tapL_ {};
    std::array<float, kTapCapacity> tapR_ {};
};


// ---- Editor: Pro-Q-style interactive graph with draggable band nodes ------
// Mapping: X = log frequency (20 Hz..20 kHz), Y = linear dB (-24..+24).
// Mouse: click near a node = select + start drag; drag = move freq/gain;
//        scroll on a node = change Q. Double-click = toggle band on/off.

class EzQ4Editor : public juce::AudioProcessorEditor,
                   private juce::AudioProcessorParameter::Listener,
                   private juce::Timer {
public:
    explicit EzQ4Editor(EzQ4Processor& proc)
        : juce::AudioProcessorEditor(&proc), proc_(proc) {
        for (int b = 0; b < ezq4::kNumBands; ++b)
            for (int k = 0; k < 5; ++k)
                param(b, k)->addListener(this);
        bypass.onClick = [this] {
            proc_.bypass_->setValueNotifyingHost(
                proc_.bypass_->convertTo0to1(bypass.getToggleState() ? 1.f : 0.f));
        };
        bypass.setButtonText("Bypass");
        addAndMakeVisible(bypass);
        out.setSliderStyle(juce::Slider::LinearBar);
        out.setRange(-12.0, 12.0, 0.1);
        out.setValue(proc_.outGain_->get());
        out.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
        out.onValueChange = [this] {
            proc_.outGain_->setValueNotifyingHost(
                proc_.outGain_->convertTo0to1((float) out.getValue()));
        };
        addAndMakeVisible(out);

        bandOn.setButtonText("On");
        bandOn.onClick = [this] {
            auto* p = proc_.bandEn_[selectedBand_];
            p->setValueNotifyingHost(
                p->convertTo0to1(bandOn.getToggleState() ? 1.f : 0.f));
            repaint();
        };
        addAndMakeVisible(bandOn);

        for (int t = 0; t < ezq4::kNumTypes; ++t)
            bandTy.addItem(typeName(t), t + 1);
        bandTy.onChange = [this] {
            auto* p = proc_.bandTy_[selectedBand_];
            p->setValueNotifyingHost(
                p->convertTo0to1((float) (bandTy.getSelectedId() - 1)));
            repaint();
        };
        addAndMakeVisible(bandTy);

        styleLabel(freqL, "Freq");
        styleLabel(gainL, "Gain");
        styleLabel(qL, "Q");
        styleSlider(freqS, 20.0, 20000.0, 0.35, " Hz", 76);
        styleSlider(gainS, -24.0, 24.0, 1.0, " dB", 60);
        styleSlider(qS, 0.1, 8.0, 0.4, "", 44);
        freqS.onValueChange = [this] {
            auto* p = proc_.freq_[selectedBand_];
            p->setValueNotifyingHost(p->convertTo0to1((float) freqS.getValue()));
        };
        gainS.onValueChange = [this] {
            auto* p = proc_.gain_[selectedBand_];
            p->setValueNotifyingHost(p->convertTo0to1((float) gainS.getValue()));
        };
        qS.onValueChange = [this] {
            auto* p = proc_.q_[selectedBand_];
            p->setValueNotifyingHost(p->convertTo0to1((float) qS.getValue()));
        };

        selectedBand_ = 0;
        updateControls();
        startTimerHz(30);
        setSize(900, 560);
    }

    ~EzQ4Editor() override {
        stopTimer();
        for (int b = 0; b < ezq4::kNumBands; ++b)
            for (int k = 0; k < 5; ++k)
                param(b, k)->removeListener(this);
    }

    void parameterValueChanged(int, float) override { repaint(); updateControls(); }
    void parameterGestureChanged(int, bool) override {}

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xFF16171B));
        auto graphArea = getLocalBounds().reduced(8, 8)
            .withTrimmedTop(36).withTrimmedBottom(64).toFloat();
        drawGrid(g, graphArea);
        drawSpectrum(g, graphArea);
        drawResponseCurve(g, graphArea);
        drawNodes(g, graphArea);
    }

    // ---- real-time spectrum (post-EQ tap, FFT via timer) ----------------
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kSpecPoints = 160;
    static constexpr float kSpecOffset = -48.f; // dB display normalization

    void timerCallback() override {
        pullSpectrum();
        repaint();
    }

    void pullSpectrum() {
        auto& fifo = proc_.tapFifo_;
        int s1, n1, s2, n2;
        const int ready = fifo.getNumReady();
        const int skip = juce::jmax(0, ready - kFftSize);
        if (skip > 0) {
            fifo.prepareToRead(skip, s1, n1, s2, n2);
            fifo.finishedRead(skip);
        }
        fifo.prepareToRead(kFftSize, s1, n1, s2, n2);
        if (n1 + n2 < kFftSize) return; // wait for a full window
        for (int i = 0; i < n1; ++i)
            fftScratch_[(size_t) i] = proc_.tapL_[(size_t) (s1 + i)];
        for (int i = 0; i < n2; ++i)
            fftScratch_[(size_t) (n1 + i)] = proc_.tapL_[(size_t) (s2 + i)];
        fifo.finishedRead(n1 + n2);
        window_.multiplyWithWindowingTable(fftScratch_.data(), kFftSize);
        juce::zeromem(fftScratch_.data() + kFftSize, kFftSize * sizeof(float));
        fft_.performRealOnlyForwardTransform(fftScratch_.data(), true);

        const double sr = juce::jmax(8000.0, proc_.sampleRate_);
        auto binMag = [&](int bin) {
            const float re = fftScratch_[(size_t) (2 * bin)];
            const float im = fftScratch_[(size_t) (2 * bin + 1)];
            return std::sqrt(re * re + im * im);
        };
        for (int p = 0; p < kSpecPoints; ++p) {
            const double f = 20.0 * std::pow(1000.0, p / double(kSpecPoints - 1));
            const double bpos = f * kFftSize / sr;
            const int b0 = (int) std::floor(bpos);
            const int b1 = juce::jmin(kFftSize / 2 - 1, b0 + 1);
            const float frac = (float) (bpos - std::floor(bpos));
            const float m = binMag(b0) * (1.f - frac) + binMag(b1) * frac;
            specDb_[(size_t) p] = juce::Decibels::gainToDecibels(m, -100.0f);
        }
        specValid_ = true;
    }

    void drawSpectrum(juce::Graphics& g, juce::Rectangle<float> area) {
        if (! specValid_) return;
        juce::Path fill;
        for (int p = 0; p < kSpecPoints; ++p) {
            const double f = 20.0 * std::pow(1000.0, p / double(kSpecPoints - 1));
            const float x = freqToX((float) f, area);
            const float db = juce::jlimit(-24.f, 24.f,
                                          specDb_[(size_t) p] + kSpecOffset);
            const float y = gainToY(db, area);
            if (p == 0) fill.startNewSubPath(x, area.getBottom());
            fill.lineTo(x, y);
        }
        fill.lineTo(area.getRight(), area.getBottom());
        fill.closeSubPath();
        g.setColour(juce::Colour((juce::uint32) 0x504D9DF7));
        g.fillPath(fill);
    }

    void drawGrid(juce::Graphics& g, juce::Rectangle<float> area) {
        // Horizontal dB lines
        g.setColour(juce::Colour(0xFF23262E));
        for (float db = -24; db <= 24; db += 6) {
            if (std::abs(db) < 0.5f) g.setColour(juce::Colour(0xFF32363F));
            g.drawHorizontalLine((int) gainToY(db, area), 0.f, area.getWidth());
            if (std::abs(db) < 0.5f) g.setColour(juce::Colour(0xFF23262E));
        }
        // Vertical octave lines
        for (double f = 20; f <= 20000; f *= 2) {
            float x = freqToX(f, area);
            g.setColour(juce::Colour(0xFF2A2D35));
            g.drawVerticalLine((int) x, area.getY(), area.getBottom());
        }
        // 0 dB line and labels
        g.setColour(juce::Colour(0xFF5C6270));
        g.setFont(10.0f);
        for (float db = -24; db <= 24; db += 12) {
            g.drawText(juce::String((int) db) + " dB", 4,
                       (int) gainToY(db, area) - 6, 50, 12,
                       juce::Justification::left);
        }
    }

    void drawResponseCurve(juce::Graphics& g, juce::Rectangle<float> area) {
        // Compute combined magnitude at 200 log-spaced points.
        const int numPts = 200;
        juce::Path curve;
        bool started = false;
        for (int i = 0; i < numPts; ++i) {
            const double norm = (double) i / (numPts - 1);
            const double freq = 20.0 * std::pow(1000.0, norm); // 20..20k
            float mag = 1.0f;
            for (int b = 0; b < ezq4::kNumBands; ++b) {
                if (! proc_.bandEn_[b]->get()) continue;
                mag *= (float) bandMagnitude(b, freq);
            }
            const float x = freqToX((float) freq, area);
            const float db = juce::Decibels::gainToDecibels(mag);
            const float y = gainToY(db, area);
            if (! started) { curve.startNewSubPath(x, y); started = true; }
            else curve.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xFF4D9DF7));
        g.strokePath(curve, juce::PathStrokeType(2));
    }

    double bandMagnitude(int b, double freq) const {
        // |H(f)| via the same coefficient factories as the DSP path, so the
        // drawn curve matches what the filters actually do (2-stage 24 dB/oct
        // cuts; the DSP hardcodes Q=0.707 for cuts regardless of the Q param).
        using B = Biquad;
        const double sr = proc_.sampleRate_;
        const double f = juce::jlimit(10.0f, 22000.0f, proc_.freq_[b]->get());
        const double g = proc_.gain_[b]->get();
        const double q = juce::jmax(0.05, (double) proc_.q_[b]->get());
        B s0 = B::allPass(), s1 = B::allPass();
        switch (proc_.bandType(b)) {
            case kBell:      s0 = B::peaking(sr, f, g, q); break;
            case kLowShelf:  s0 = B::lowShelving(sr, f, g, q); break;
            case kHighShelf: s0 = B::highShelving(sr, f, g, q); break;
            case kNotch:     s0 = B::notching(sr, f, q); break;
            case kLowCut:    s0 = s1 = B::highPass(sr, f, 0.707); break;
            case kHighCut:   s0 = s1 = B::lowPass(sr, f, 0.707); break;
        }
        auto stageMag = [sr, freq](const B& c) {
            const double w = 2 * 3.14159265358979323846 * freq / sr;
            const double nRe = c.b0 + c.b1 * std::cos(w) + c.b2 * std::cos(2 * w);
            const double nIm = -(c.b1 * std::sin(w) + c.b2 * std::sin(2 * w));
            const double dRe = 1.0 + c.a1 * std::cos(w) + c.a2 * std::cos(2 * w);
            const double dIm = -(c.a1 * std::sin(w) + c.a2 * std::sin(2 * w));
            return std::sqrt((nRe * nRe + nIm * nIm) / (dRe * dRe + dIm * dIm));
        };
        return stageMag(s0) * stageMag(s1);
    }

    void drawNodes(juce::Graphics& g, juce::Rectangle<float> area) {
        for (int b = 0; b < ezq4::kNumBands; ++b) {
            const bool en = proc_.bandEn_[b]->get() != 0;
            const float f = proc_.freq_[b]->get();
            const float db = proc_.gain_[b]->get();
            const float x = freqToX(f, area);
            const float y = gainToY(db, area);
            const float r = (int) b == selectedBand_ ? 9.f : 6.f;
            auto colour = en ? juce::Colour(0xFF4D9DF7) : juce::Colour(0xFF5C6270);
            if ((int) b == selectedBand_) colour = juce::Colour(0xFF7FB2F0);
            g.setColour(colour);
            g.fillEllipse(x - r, y - r, r * 2, r * 2);
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawEllipse(x - r, y - r, r * 2, r * 2, 1.5f);
            g.setColour(juce::Colours::white);
            g.setFont(9.0f);
            g.drawText(juce::String(b + 1), (int) (x - r), (int) (y - r),
                       (int) (r * 2), (int) (r * 2),
                       juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const int hit = nodeAt(e.position, getGraphArea());
        if (hit >= 0) {
            selectedBand_ = hit;
            dragging_ = true;
            updateControls();
            repaint();
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int hit = nodeAt(e.position, getGraphArea());
        if (hit < 0) return;
        auto* p = proc_.bandEn_[hit];
        p->setValueNotifyingHost(p->convertTo0to1(p->get() != 0 ? 0.f : 1.f));
        updateControls();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (! dragging_ || selectedBand_ < 0) return;
        auto area = getGraphArea();
        const float x = juce::jlimit(area.getX(), area.getRight(), e.position.x);
        const float y = juce::jlimit(area.getY(), area.getBottom(), e.position.y);
        const double freq = xToFreq(x, area);
        const double db = yToGain(y, area);
        proc_.freq_[selectedBand_]->setValueNotifyingHost(
            proc_.freq_[selectedBand_]->convertTo0to1((float) freq));
        proc_.gain_[selectedBand_]->setValueNotifyingHost(
            proc_.gain_[selectedBand_]->convertTo0to1((float) db));
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { dragging_ = false; }

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override {
        int b = nodeAt(e.position, getGraphArea());
        if (b < 0) b = selectedBand_;
        if (b < 0) return;
        auto* qp = proc_.q_[b];
        const float delta = wheel.deltaY > 0 ? 0.85f : 1.18f;
        const float newQ = juce::jlimit(0.1f, 8.f, qp->get() * delta);
        qp->setValueNotifyingHost(qp->convertTo0to1(newQ));
        updateControls();
        repaint();
    }

    juce::Rectangle<float> getGraphArea() const {
        return getLocalBounds().reduced(8, 8)
            .withTrimmedTop(36).withTrimmedBottom(64).toFloat();
    }

    static float freqToX(float freq, juce::Rectangle<float> area) {
        const float norm = std::log(freq / 20.f) / std::log(1000.f);
        return area.getX() + norm * area.getWidth();
    }
    static float xToFreq(float x, juce::Rectangle<float> area) {
        return 20.f * std::pow(1000.f, (x - area.getX()) / area.getWidth());
    }
    static float gainToY(float db, juce::Rectangle<float> area) {
        return area.getY() + (1 - (db + 24) / 48) * area.getHeight();
    }
    static float yToGain(float y, juce::Rectangle<float> area) {
        return (1 - (y - area.getY()) / area.getHeight()) * 48 - 24;
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto header = area.removeFromTop(28);
        bypass.setBounds(header.removeFromLeft(90));
        out.setBounds(header.removeFromLeft(190));

        auto bar = area.removeFromBottom(52);
        bandOn.setBounds(bar.removeFromLeft(44).withTrimmedTop(14));
        bandTy.setBounds(bar.removeFromLeft(112).withTrimmedTop(14));
        const int cellW = bar.getWidth() / 3;
        juce::Rectangle<int> cells[3] = { bar.removeFromLeft(cellW),
                                          bar.removeFromLeft(cellW), bar };
        juce::Label* labels[3] = { &freqL, &gainL, &qL };
        juce::Slider* sliders[3] = { &freqS, &gainS, &qS };
        for (int i = 0; i < 3; ++i) {
            auto c = cells[i];
            labels[i]->setBounds(c.removeFromTop(14));
            sliders[i]->setBounds(c.reduced(2, 8));
        }
    }

private:
    juce::AudioProcessorParameter* param(int b, int k) {
        switch (k) {
            case 0: return proc_.bandEn_[b];
            case 1: return proc_.bandTy_[b];
            case 2: return proc_.freq_[b];
            case 3: return proc_.gain_[b];
            default: return proc_.q_[b];
        }
    }

    static const char* typeName(int t) {
        switch (t) {
            case 0: return "Bell";       case 1: return "Low cut";
            case 2: return "Low shelf";  case 3: return "High shelf";
            case 4: return "High cut";   default: return "Notch";
        }
    }

    void styleLabel(juce::Label& l, const char* title) {
        l.setText(title, juce::dontSendNotification);
        l.setFont(11.0f);
        l.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA2B1));
        addAndMakeVisible(l);
    }
    void styleSlider(juce::Slider& s, double lo, double hi, double skew,
                     const char* suffix, int boxW) {
        s.setSliderStyle(juce::Slider::LinearBar);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxW, 18);
        s.setRange(lo, hi, 0.1);
        s.setSkewFactor(skew);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);
    }

    // Pushes the selected band's parameters into the bottom-bar controls.
    void updateControls() {
        const int b = selectedBand_;
        if (b < 0) return;
        bandOn.setToggleState(proc_.bandEn_[b]->get() != 0,
                              juce::dontSendNotification);
        bandTy.setSelectedId(proc_.bandTy_[b]->getIndex() + 1,
                             juce::dontSendNotification);
        freqS.setValue(proc_.freq_[b]->get(), juce::dontSendNotification);
        gainS.setValue(proc_.gain_[b]->get(), juce::dontSendNotification);
        qS.setValue(proc_.q_[b]->get(), juce::dontSendNotification);
    }

    int nodeAt(juce::Point<float> pos, juce::Rectangle<float> area) const {
        int closest = -1;
        float closestDist = 16.f;
        for (int b = 0; b < ezq4::kNumBands; ++b) {
            const float x = freqToX(proc_.freq_[b]->get(), area);
            const float y = gainToY(proc_.gain_[b]->get(), area);
            const float dist = juce::Point<float>(x, y).getDistanceFrom(pos);
            if (dist < closestDist) { closestDist = dist; closest = b; }
        }
        return closest;
    }

    EzQ4Processor& proc_;
    juce::ToggleButton bypass, bandOn;
    juce::ComboBox bandTy;
    juce::Slider out, freqS, gainS, qS;
    juce::Label freqL, gainL, qL;
    juce::dsp::FFT fft_ { kFftOrder };
    juce::dsp::WindowingFunction<float> window_ { kFftSize,
        juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * kFftSize> fftScratch_ {};
    std::array<float, kSpecPoints> specDb_ {};
    bool specValid_ = false;
    bool dragging_ = false;
    int selectedBand_ = -1;
};

// Defined out of line so the editor class can follow the processor.
juce::AudioProcessorEditor* EzQ4Processor::createEditor() {
    return new EzQ4Editor(*this);
}

} // namespace ezq4

juce::AudioProcessor* ezq4::create() { return new ezq4::EzQ4Processor(); }

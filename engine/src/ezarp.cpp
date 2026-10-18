// EZArpeggio — implementation. See ezarp.h.
//
// The transform: incoming noteOns enter a HELD set (arrival order kept);
// on every step of the chosen rate the sequencer emits the next entry of
// the pattern sequence (held notes × octaves, ordered by the shape), with
// a gate-length noteOff. The incoming buffer is CONSUMED — the instrument
// only ever hears the arpeggio. Because the engine captures the inserted
// MIDI while recording an armed track, recording bakes the pattern into a
// clip: the part gets rewritten with the arpeggio.
#include "ezarp.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <random>
#include <set>
#include <vector>

namespace ezarp {
namespace {

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

// Rate presets in sixteenths per step.
struct RateInfo
{
    const char* name;
    double sixteenths; // step length in 1/16 notes (triplets: fractional)
};

constexpr RateInfo kRates[] = {
    { "1/4", 4.0 }, { "1/8", 2.0 }, { "1/8T", 4.0 / 3.0 },
    { "1/16", 1.0 }, { "1/16T", 2.0 / 3.0 }, { "1/32", 0.5 },
};
constexpr int kRateCount = 6;

constexpr const char* kShapes[] = {
    "Subida", "Bajada", "Sube-Baja", "Baja-Sube", "Converge", "Diverge",
    "Aleatorio", "Como tocado",
};
constexpr int kShapeCount = 8;

// Build the play order over `n` entries for a shape.
std::vector<int> playOrder(const int shape, const int n)
{
    std::vector<int> order;
    order.reserve((size_t) std::max(1, n));
    if (n <= 0)
        return order;
    switch (shape)
    {
        case 0: // up
            for (int i = 0; i < n; ++i)
                order.push_back(i);
            break;
        case 1: // down
            for (int i = n - 1; i >= 0; --i)
                order.push_back(i);
            break;
        case 2: // up-down (no end repeats)
            for (int i = 0; i < n; ++i)
                order.push_back(i);
            for (int i = n - 2; i >= 1; --i)
                order.push_back(i);
            break;
        case 3: // down-up
            for (int i = n - 1; i >= 0; --i)
                order.push_back(i);
            for (int i = 1; i < n - 1; ++i)
                order.push_back(i);
            break;
        case 4: // converge: outer-in
        {
            int lo = 0, hi = n - 1;
            while (lo <= hi)
            {
                order.push_back(lo++);
                if (lo <= hi)
                    order.push_back(hi--);
            }
            break;
        }
        case 5: // diverge: inner-out
        {
            int mid = (n - 1) / 2;
            order.push_back(mid);
            for (int d = 1; d < n; ++d)
            {
                if (mid - d >= 0)
                    order.push_back(mid - d);
                if (mid + d < n)
                    order.push_back(mid + d);
            }
            break;
        }
        case 6: // random — filled by the caller's shuffle
            for (int i = 0; i < n; ++i)
                order.push_back(i);
            break;
        default: // as played
            for (int i = 0; i < n; ++i)
                order.push_back(i);
            break;
    }
    return order;
}

} // namespace

class EzArpProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorParameter::Listener
{
public:
    EzArpProcessor()
        : AudioProcessor(BusesProperties())
    {
        juce::StringArray shapeNames;
        for (const auto* s : kShapes)
            shapeNames.add(u8(s));
        shape_ = new juce::AudioParameterChoice(
            juce::ParameterID { "shape", 1 }, u8("Patrón"), shapeNames, 0);
        addParameter(shape_);

        juce::StringArray rateNames;
        for (const auto& r : kRates)
            rateNames.add(r.name);
        rate_ = new juce::AudioParameterChoice(
            juce::ParameterID { "rate", 1 }, u8("Velocidad"), rateNames, 3);
        addParameter(rate_);

        oct_ = new juce::AudioParameterInt(
            juce::ParameterID { "oct", 1 }, u8("Octavas"), 1, 4, 1);
        addParameter(oct_);

        gate_ = new juce::AudioParameterFloat(
            juce::ParameterID { "gate", 1 }, u8("Gate"),
            juce::NormalisableRange<float>(0.1f, 1.2f, 0.01f), 0.8f);
        addParameter(gate_);

        juce::StringArray sigNames { u8("Auto (proyecto)"), "4/4", "3/4",
                                     "6/8", "5/4", "7/8" };
        sig_ = new juce::AudioParameterChoice(
            juce::ParameterID { "sig", 1 }, u8("Compás"), sigNames, 0);
        addParameter(sig_);

        for (auto* p : getParameters())
            p->addListener(this);
    }

    ~EzArpProcessor() override
    {
        for (auto* p : getParameters())
            p->removeListener(this);
    }

    // ---- editor surface ----
    juce::AudioParameterChoice* shapeParam() { return shape_; }
    juce::AudioParameterChoice* rateParam() { return rate_; }
    juce::AudioParameterInt* octParam() { return oct_; }
    juce::AudioParameterFloat* gateParam() { return gate_; }
    juce::AudioParameterChoice* sigParam() { return sig_; }
    int currentStep() const
    {
        return stepUi_.load(std::memory_order_relaxed);
    }
    int heldCount() const
    {
        return (int) held_.size();
    }

private:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool) override {}

public:
    void prepareToPlay(const double sr, const int) override
    {
        sr_ = sr > 1000.0 ? sr : 44100.0;
    }
    void releaseResources() override {}

    // A MIDI effect: no audio buses at all.
    bool isBusesLayoutSupported(const BusesLayout&) const override
    {
        return true;
    }

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi) override
    {
        const int numSamples = juce::jmax(1, buffer.getNumSamples());

        // 1) Harvest held notes from the incoming stream (clip or keys).
        for (const auto ev : midi)
        {
            const auto& m = ev.getMessage();
            if (m.isNoteOn())
            {
                const int p = m.getNoteNumber();
                if (! heldActive_.count(p))
                {
                    held_.push_back({ p, (int) m.getVelocity() });
                    heldActive_.insert(p);
                }
            }
            else if (m.isNoteOff() || m.isAllNotesOff())
            {
                if (m.isAllNotesOff())
                {
                    held_.clear();
                    heldActive_.clear();
                }
                else
                {
                    const int p = m.getNoteNumber();
                    held_.erase(std::remove_if(
                                    held_.begin(), held_.end(),
                                    [p](const HeldNote& h) {
                                        return h.pitch == p;
                                    }),
                                held_.end());
                    heldActive_.erase(p);
                    // Kill a sounding arp instance of that pitch now.
                    midi.addEvent(juce::MidiMessage::noteOff(1, p), 0);
                }
            }
        }

        // 2) The instrument must NOT hear the original stream — the arp
        //    output replaces it entirely.
        midi.clear();

        // 3) Transport sync.
        bool playing = false;
        std::int64_t timeInSamples = 0;
        double bpm = 120.0;
        int projNum = 4, projDen = 4;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                playing = pos->getIsPlaying();
                if (pos->getBpm().hasValue())
                    bpm = *pos->getBpm();
                if (pos->getTimeInSamples().hasValue())
                    timeInSamples = *pos->getTimeInSamples();
                if (pos->getTimeSignature().hasValue())
                {
                    projNum = juce::jmax(
                        1, pos->getTimeSignature()->numerator);
                    projDen = juce::jmax(
                        1, pos->getTimeSignature()->denominator);
                }
            }
        bpm = juce::jlimit(30.0, 300.0, bpm);
        if (! playing || held_.empty())
        {
            stepUi_.store(-1, std::memory_order_relaxed);
            return;
        }

        const int shape = juce::jlimit(0, kShapeCount - 1, shape_->getIndex());
        const int rateIdx = juce::jlimit(0, kRateCount - 1, rate_->getIndex());
        const double stepSixteenths = kRates[rateIdx].sixteenths;
        const int octaves = juce::jlimit(1, 4, oct_->get());
        const float gate = juce::jlimit(0.1f, 1.2f, gate_->get());

        const int sigChoice = sig_->getIndex();
        int num = projNum, den = projDen;
        if (sigChoice > 0)
        {
            static constexpr int nums[5] = { 4, 3, 6, 5, 7 };
            static constexpr int dens[5] = { 4, 4, 8, 4, 8 };
            num = nums[sigChoice - 1];
            den = dens[sigChoice - 1];
        }

        const double quarterSec = 60.0 / bpm;
        const double stepSec = quarterSec * stepSixteenths / 4.0;
        const std::int64_t stepSmp =
            (std::int64_t) juce::jmax(1.0, stepSec * sr_);
        const std::int64_t barSteps =
            (std::int64_t) juce::jmax(1.0,
                                      (double) num * 16.0 / (double) den
                                          / stepSixteenths);

        // Sequence: held notes × octaves, ordered by the shape.
        const int n = (int) held_.size();
        std::vector<int> order = playOrder(shape, n);
        if (shape == 6)
        {
            std::shuffle(order.begin(), order.end(), rng_);
        }
        std::vector<std::pair<int, int>> seq; // (pitch, vel)
        seq.reserve(order.size() * (size_t) octaves);
        for (int o = 0; o < octaves; ++o)
            for (int idx : order)
                seq.push_back({ held_[(size_t) idx].pitch + 12 * o,
                                held_[(size_t) idx].vel });
        if (seq.empty())
            return;

        const std::int64_t blockStart = timeInSamples;
        const std::int64_t blockEnd = blockStart + numSamples;
        if (! playing_)
        {
            playing_ = true;
            nextStepAbs_ =
                ((blockStart + stepSmp - 1) / stepSmp + 1) * stepSmp;
        }
        else if (nextStepAbs_ < blockStart - stepSmp * 4
                 || nextStepAbs_ > blockEnd + stepSmp * 4)
        {
            nextStepAbs_ =
                ((blockStart + stepSmp - 1) / stepSmp + 1) * stepSmp;
        }

        std::int64_t absStep = nextStepAbs_ / stepSmp;
        while (nextStepAbs_ < blockEnd)
        {
            const std::int64_t stepInBar = absStep % barSteps;
            stepUi_.store((int) stepInBar, std::memory_order_relaxed);
            // Bar wrap re-anchors the pattern to the downbeat — this is
            // what the time-signature setting does musically.
            if (stepInBar == 0)
                cursor_ = 0;
            if (cursor_ >= (int) seq.size())
                cursor_ = 0;
            const auto [pitch, vel] = seq[(size_t) cursor_++];

            const int off = (int) juce::jlimit<std::int64_t>(
                0, numSamples - 1, nextStepAbs_ - blockStart);
            midi.addEvent(juce::MidiMessage::noteOn(1, pitch,
                                                    (float) vel / 127.f),
                          off);
            const std::int64_t gateEnd =
                nextStepAbs_ + (std::int64_t) (stepSmp * gate);
            pendingOffs_.push_back({ gateEnd, pitch });

            nextStepAbs_ += stepSmp;
            ++absStep;
        }

        // Flush gate note-offs (and stop held notes from ringing forever:
        // every arp noteOn gets exactly one noteOff).
        for (auto it = pendingOffs_.begin(); it != pendingOffs_.end();)
        {
            if (it->first < blockEnd)
            {
                const int off = (int) juce::jlimit<std::int64_t>(
                    0, numSamples - 1,
                    juce::jmax<std::int64_t>(0, it->first - blockStart));
                midi.addEvent(juce::MidiMessage::noteOff(1, it->second), off);
                it = pendingOffs_.erase(it);
            }
            else
                ++it;
        }
    }

private:
    struct HeldNote
    {
        int pitch, vel;
    };

    const juce::String getName() const override { return { "EZArpeggio" }; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return { "Default" }; }
    void changeProgramName(int, const juce::String&) override {}
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        juce::XmlElement x("EZARP");
        x.setAttribute("shape", shape_->getIndex());
        x.setAttribute("rate", rate_->getIndex());
        x.setAttribute("oct", oct_->get());
        x.setAttribute("gate", (double) gate_->get());
        x.setAttribute("sig", sig_->getIndex());
        copyXmlToBinary(x, dest);
    }

    void setStateInformation(const void* data, const int size) override
    {
        if (auto x = getXmlFromBinary(data, size))
            if (x->hasTagName("EZARP"))
            {
                shape_->setValueNotifyingHost(shape_->convertTo0to1(
                    (float) x->getIntAttribute("shape", 0)));
                rate_->setValueNotifyingHost(rate_->convertTo0to1(
                    (float) x->getIntAttribute("rate", 3)));
                oct_->setValueNotifyingHost(oct_->convertTo0to1(
                    (float) x->getIntAttribute("oct", 1)));
                gate_->setValueNotifyingHost(gate_->convertTo0to1(
                    (float) x->getDoubleAttribute("gate", 0.8)));
                sig_->setValueNotifyingHost(sig_->convertTo0to1(
                    (float) x->getIntAttribute("sig", 0)));
            }
    }

    juce::AudioParameterChoice* shape_ = nullptr;
    juce::AudioParameterChoice* rate_ = nullptr;
    juce::AudioParameterInt* oct_ = nullptr;
    juce::AudioParameterFloat* gate_ = nullptr;
    juce::AudioParameterChoice* sig_ = nullptr;

    std::vector<HeldNote> held_;
    std::set<int> heldActive_;
    double sr_ = 44100.0;
    bool playing_ = false;
    std::int64_t nextStepAbs_ = 0;
    int cursor_ = 0;
    std::vector<std::pair<std::int64_t, int>> pendingOffs_;
    std::atomic<int> stepUi_ { -1 };
    std::mt19937 rng_ { 0x5EED };
};

// ---- Editor ------------------------------------------------------------------
class EzArpEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EzArpEditor(EzArpProcessor& proc)
        : AudioProcessorEditor(proc), p(proc)
    {
        auto addCombo = [this](std::unique_ptr<juce::ComboBox>& box,
                               juce::AudioParameterChoice* param,
                               const juce::String& title) {
            auto* lbl = new juce::Label(juce::String(), title);
            lbl->setFont(juce::FontOptions(11.f));
            lbl->setColour(juce::Label::textColourId,
                           juce::Colour(0xff9aa2b1));
            addAndMakeVisible(lbl);
            comboLabels_.emplace_back(lbl);
            box = std::make_unique<juce::ComboBox>();
            for (const auto& choice : param->choices)
                box->addItem(choice, box->getNumItems() + 1);
            box->setSelectedId(param->getIndex() + 1,
                               juce::dontSendNotification);
            box->onChange = [boxPtr = box.get(), param] {
                *param = boxPtr->getSelectedItemIndex();
            };
            addAndMakeVisible(*box);
        };
        addCombo(shapeBox_, p.shapeParam(), u8("Patrón"));
        addCombo(rateBox_, p.rateParam(), u8("Velocidad"));
        addCombo(sigBox_, p.sigParam(), u8("Compás"));

        octS_.setSliderStyle(juce::Slider::LinearBar);
        octS_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
        octS_.setRange(1, 4, 1);
        octS_.setValue(p.octParam()->get(), juce::dontSendNotification);
        octS_.onValueChange = [this] {
            *p.octParam() = (int) octS_.getValue();
        };
        addAndMakeVisible(octL_);
        octL_.setText(u8("Octavas"), juce::dontSendNotification);
        octL_.setFont(juce::FontOptions(11.f));
        octL_.setColour(juce::Label::textColourId, juce::Colour(0xff9aa2b1));
        addAndMakeVisible(octS_);

        gateS_.setSliderStyle(juce::Slider::LinearBar);
        gateS_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
        gateS_.setRange(0.1, 1.2, 0.01);
        gateS_.setValue(p.gateParam()->get(), juce::dontSendNotification);
        gateS_.onValueChange = [this] {
            *p.gateParam() = (float) gateS_.getValue();
        };
        addAndMakeVisible(gateL_);
        gateL_.setText(u8("Gate"), juce::dontSendNotification);
        gateL_.setFont(juce::FontOptions(11.f));
        gateL_.setColour(juce::Label::textColourId, juce::Colour(0xff9aa2b1));
        addAndMakeVisible(gateS_);

        status.setFont(juce::FontOptions(11.f));
        status.setColour(juce::Label::textColourId, juce::Colour(0xff8b93a0));
        status.setText(u8("Con la pista en R, grabar reescribe el clip con "
                          "el arpegio."),
                       juce::dontSendNotification);
        addAndMakeVisible(status);

        setSize(560, 170);
        startTimerHz(30);
    }

    ~EzArpEditor() override { stopTimer(); }

    void timerCallback() override
    {
        const int step = p.currentStep();
        const juce::String txt =
            u8("Paso ") + juce::String(step < 0 ? 0 : step + 1)
            + u8(" · notas sostenidas: ") + juce::String(p.heldCount())
            + u8(" · con la pista en R, grabar reescribe el clip con el "
                 "arpegio.");
        if (txt != status.getText())
            status.setText(txt, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto row = area.removeFromTop(28);
        shapeBox_->setBounds(row.removeFromLeft(150));
        rateBox_->setBounds(row.removeFromLeft(110).reduced(0, 2));
        sigBox_->setBounds(row.removeFromLeft(130).reduced(0, 2));
        area.removeFromTop(8);
        auto row2 = area.removeFromTop(28);
        octL_.setBounds(row2.removeFromLeft(60));
        octS_.setBounds(row2.removeFromLeft(130).reduced(0, 4));
        gateL_.setBounds(row2.removeFromLeft(50));
        gateS_.setBounds(row2.removeFromLeft(140).reduced(0, 4));
        status.setBounds(area.removeFromBottom(20));
    }

private:
    EzArpProcessor& p;
    std::unique_ptr<juce::ComboBox> shapeBox_, rateBox_, sigBox_;
    std::vector<juce::Label*> comboLabels_;
    juce::Label octL_, gateL_, status;
    juce::Slider octS_, gateS_;
};

juce::AudioProcessorEditor* EzArpProcessor::createEditor()
{
    return new EzArpEditor(*this);
}

juce::AudioProcessor* create() { return new EzArpProcessor(); }

} // namespace ezarp

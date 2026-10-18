// EZInstruments — implementation. FluidSynth-backed General MIDI module.
// MIDI in (notes/CC/pitch bend) → fluid_synth → stereo out. Everything on
// channel 0 when melodic; with the kit enabled everything moves to channel
// 9 (GM drums, bank 128) — one instrument track plays either melodic OR
// kit, mirroring how GM modules behave.
//
// Editor: category/instrument browser (the Mixcraft-style catalog picker)
// plus per-instrument controls (level, filter brightness, attack, release)
// that map to SoundFont generator offsets, so every instrument can be
// tweaked without leaving the DAW.
#include "ezinst.h"

#include <fluidsynth.h>

#include <array>
#include <cmath>
#include <vector>

namespace ezinst {
namespace {

// General MIDI Level 1 program names, index = preset number.
const char* kProgramNames[128] = {
    // Piano
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano",
    "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2",
    "Harpsichord", "Clavinet",
    // Chromatic percussion
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba",
    "Xylophone", "Tubular Bells", "Dulcimer",
    // Organ
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
    "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    // Guitar
    "Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar",
    "Muted Guitar", "Overdrive Guitar", "Distortion Guitar",
    "Guitar Harmonics",
    // Bass
    "Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass",
    "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    // Strings
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings",
    "Pizzicato Strings", "Orchestral Harp", "Timpani",
    // Ensemble
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1",
    "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice",
    "Orchestra Hit",
    // Brass
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn",
    "Brass Section", "Synth Brass 1", "Synth Brass 2",
    // Reed
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe",
    "English Horn", "Bassoon", "Clarinet",
    // Pipe
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle",
    "Shakuhachi", "Whistle", "Ocarina",
    // Synth lead
    "Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead",
    "Charang Lead", "Voice Lead", "Fifths Lead", "Bass Lead",
    // Synth pad
    "New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad", "Bowed Pad",
    "Metallic Pad", "Halo Pad", "Sweep Pad",
    // Synth effects
    "Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness",
    "Goblins", "Echoes", "Sci-Fi",
    // Ethnic
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle",
    "Shanai",
    // Percussive
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum",
    "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    // Sound effects
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
    "Telephone Ring", "Helicopter", "Applause", "Gunshot",
};

struct GmCategory { const char* name; int start; };
constexpr GmCategory kCategories[16] = {
    {"Piano", 0}, {"Perc. cromática", 8}, {"Órgano", 16}, {"Guitarras", 24},
    {"Bajos", 32}, {"Cuerdas", 40}, {"Ensemble", 48}, {"Metales", 56},
    {"Viento madera", 64}, {"Viento", 72}, {"Synth lead", 80},
    {"Synth pad", 88}, {"Efectos synth", 96}, {"Étnicos", 104},
    {"Perc. melódica", 112}, {"Efectos", 120},
};

juce::StringArray programChoices()
{
    juce::StringArray a;
    for (const char* n : kProgramNames)
        a.add(juce::String(n));
    return a;
}

// JUCE's String(const char*) assumes ASCII — accented literals must go
// through fromUTF8 or they render as mojibake.
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

// SoundFont candidates, quality-ordered (SF3 files need libsndfile in the
// fluidsynth build — a plain SF2 is the always-works fallback).
juce::StringArray soundFontCandidates()
{
    juce::StringArray out;
    // macOS standard AND JUCE's userApplicationDataDirectory (~/Library on
    // this JUCE build) — keep both covered.
    const juce::String supportDirs[] = {
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Application Support/EZMusicProduction/soundfonts")
            .getFullPathName(),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("EZMusicProduction")
            .getChildFile("soundfonts")
            .getFullPathName(),
    };
    for (const auto& dirName : supportDirs)
    {
        const juce::File support(dirName);
        for (const char* n : { "MSBasic.sf3", "MuseScore_General.sf3",
                               "FluidR3Mono_GM.sf3", "GeneralUser GS.sf2",
                               "TimGM6mb.sf2" })
            if (support.getChildFile(n).existsAsFile())
                out.add(support.getChildFile(n).getFullPathName());
    }
    for (const char* d : { "/usr/local/share/soundfonts",
                           "/usr/share/soundfonts", "/Library/Sounds" })
    {
        const juce::File dir(d);
        if (! dir.isDirectory())
            continue;
        for (const auto& f : dir.findChildFiles(
                 juce::File::findFiles, false, "*.sf2;*.sf3"))
            out.add(f.getFullPathName());
    }
    return out;
}

class EzInstEditor;

class EzInstProcessor : public juce::AudioProcessor
{
public:
    EzInstProcessor()
        : AudioProcessor(BusesProperties().withOutput(
              "Output", juce::AudioChannelSet::stereo(), true))
    {
        program_ = new juce::AudioParameterChoice(
            juce::ParameterID{"prog", 1}, "Instrumento", programChoices(), 0);
        addParameter(program_);
        kit_ = new juce::AudioParameterBool(
            juce::ParameterID{"kit", 1}, "Kit percusión", false);
        addParameter(kit_);
        gainP_ = new juce::AudioParameterFloat(
            juce::ParameterID{"gain", 1}, "Nivel",
            juce::NormalisableRange<float>(0.f, 2.f, 0.01f), 0.9f);
        addParameter(gainP_);
        brightP_ = new juce::AudioParameterFloat(
            juce::ParameterID{"bright", 1}, "Brillo",
            juce::NormalisableRange<float>(-4800.f, 4800.f, 1.f, 0.5f), 0.f);
        addParameter(brightP_);
        attackP_ = new juce::AudioParameterFloat(
            juce::ParameterID{"atk", 1}, "Ataque",
            juce::NormalisableRange<float>(0.f, 1.f, 0.01f), 0.f);
        addParameter(attackP_);
        releaseP_ = new juce::AudioParameterFloat(
            juce::ParameterID{"rel", 1}, "Release",
            juce::NormalisableRange<float>(-0.5f, 2.f, 0.01f), 0.f);
        addParameter(releaseP_);
    }

    ~EzInstProcessor() override { teardown(); }

    const juce::String getName() const override { return {"EZInstruments"}; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {"Default"}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sr, int) override
    {
        sampleRate_ = sr;
        setupSynth();
    }
    void releaseResources() override { teardown(); }

    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet().isDisabled()
            && (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                || l.getMainOutputChannelSet()
                       == juce::AudioChannelSet::mono());
    }

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi) override
    {
        buffer.clear();
        if (synth_ == nullptr)
            return;
        syncProgram();
        applyControls();

        const int ch = kit_->get() ? 9 : 0;
        for (const auto md : midi)
        {
            const auto msg = md.getMessage();
            if (msg.isNoteOn())
                fluid_synth_noteon(synth_, ch, msg.getNoteNumber(),
                                   msg.getVelocity());
            else if (msg.isNoteOff(/*returnTrueForNoteOnVelocity0*/ true))
                // Note-on vel 0 is the running-status note-off convention —
                // without this, sustained-loop patches (strings, winds) ring
                // forever while decaying patches hide the bug.
                fluid_synth_noteoff(synth_, ch, msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                fluid_synth_all_notes_off(synth_, ch);
            else if (msg.isController())
                fluid_synth_cc(synth_, ch, msg.getControllerNumber(),
                               msg.getControllerValue());
            else if (msg.isPitchWheel())
                fluid_synth_pitch_bend(synth_, ch, msg.getPitchWheelValue());
        }

        const int n = buffer.getNumSamples();
        float* l = buffer.getWritePointer(0);
        float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1)
                                               : nullptr;
        if (r != nullptr)
            fluid_synth_write_float(synth_, n, l, 0, 1, r, 0, 1);
        else
        {
            float mono[512];
            for (int pos = 0; pos < n; pos += 512)
            {
                const int chunk = juce::jmin(512, n - pos);
                fluid_synth_write_float(synth_, chunk, mono, 0, 1, mono, 0, 1);
                juce::FloatVectorOperations::copy(l + pos, mono, chunk);
            }
        }
    }

    // ---- state ----
    void getStateInformation(juce::MemoryBlock& dest) override
    {
        juce::XmlElement x("EZINST");
        x.setAttribute("prog", program_->getIndex());
        x.setAttribute("kit", (int) (kit_->get() ? 1 : 0));
        x.setAttribute("g", gainP_->get());
        x.setAttribute("b", brightP_->get());
        x.setAttribute("a", attackP_->get());
        x.setAttribute("r", releaseP_->get());
        copyXmlToBinary(x, dest);
    }
    void setStateInformation(const void* data, int size) override
    {
        auto x = getXmlFromBinary(data, size);
        if (x == nullptr || x->getTagName() != "EZINST")
            return;
        program_->setValueNotifyingHost(program_->convertTo0to1(
            (float) juce::jlimit(0, 127, x->getIntAttribute("prog", 0))));
        kit_->setValueNotifyingHost(
            kit_->convertTo0to1(x->getBoolAttribute("kit", false) ? 1.f : 0.f));
        gainP_->setValueNotifyingHost(
            gainP_->convertTo0to1((float) x->getDoubleAttribute("g", 0.9)));
        brightP_->setValueNotifyingHost(
            brightP_->convertTo0to1((float) x->getDoubleAttribute("b", 0.0)));
        attackP_->setValueNotifyingHost(
            attackP_->convertTo0to1((float) x->getDoubleAttribute("a", 0.0)));
        releaseP_->setValueNotifyingHost(
            releaseP_->convertTo0to1((float) x->getDoubleAttribute("r", 0.0)));
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    friend class EzInstEditor;

private:
    void setupSynth()
    {
        teardown();
        fluid_settings_t* st = new_fluid_settings();
        fluid_settings_setnum(st, "synth.sample-rate", sampleRate_);
        // The FX chain owns space and modulation.
        fluid_settings_setint(st, "synth.reverb.active", 0);
        fluid_settings_setint(st, "synth.chorus.active", 0);
        fluid_settings_setint(st, "synth.polyphony", 256);
        // SF3 with libsndfile otherwise vorbis-decodes EVERY sample at
        // sfload (minutes per instance on Intel); decode on first use.
        fluid_settings_setint(st, "synth.dynamic-sample-loading", 1);
        synth_ = new_fluid_synth(st);
        settings_ = st;
        fluid_synth_set_gain(synth_, 0.9f);

        // Try candidates in order until one actually loads (an SF3 fails
        // without libsndfile; the SF2 fallback must then kick in).
        for (const auto& path : soundFontCandidates())
        {
            sfid_ = fluid_synth_sfload(synth_, path.toRawUTF8(), 1);
            juce::Logger::writeToLog("[ezinst] sfload " + path + " -> " +
                                     juce::String(sfid_));
            if (sfid_ >= 0)
                break;
        }
        progDirty_ = true;
        ctrlDirty_ = true;
    }

    void teardown()
    {
        if (synth_ != nullptr)
        {
            delete_fluid_synth(synth_);
            synth_ = nullptr;
        }
        if (settings_ != nullptr)
        {
            delete_fluid_settings(settings_);
            settings_ = nullptr;
        }
        sfid_ = -1;
    }

    // Push program/kit parameter state into the synth (changed values only).
    void syncProgram()
    {
        const int prog = program_->getIndex();
        const bool kit = kit_->get();
        if (! progDirty_ && prog == lastProg_ && kit == lastKit_)
            return;
        if (synth_ == nullptr || sfid_ < 0)
            return;
        if (kit)
            fluid_synth_program_select(synth_, 9, sfid_, 128, 0);
        else
            fluid_synth_program_select(synth_, 0, sfid_, 0, prog);
        lastProg_ = prog;
        lastKit_ = kit;
        progDirty_ = false;
    }

    // Per-instrument tweaks as SoundFont generator offsets (both channels,
    // so the kit honors them too). Offsets are additive to the preset.
    void applyControls()
    {
        const float g = gainP_->get();
        const float b = brightP_->get();
        const float a = attackP_->get();
        const float r = releaseP_->get();
        if (! ctrlDirty_ && g == lastGain_ && b == lastBright_ &&
            a == lastAtk_ && r == lastRel_)
            return;
        if (synth_ == nullptr)
            return;
        auto timecents = [](float extraSecs) {
            const float scale = juce::jmax(0.05f, 1.0f + extraSecs);
            return (int) (1200.0 * std::log2(scale));
        };
        fluid_synth_set_gain(synth_, juce::jmax(0.01f, g));
        for (const int ch : { 0, 9 })
        {
            fluid_synth_set_gen(synth_, ch, GEN_FILTERFC, b);
            fluid_synth_set_gen(synth_, ch, GEN_VOLENVATTACK,
                                (float) timecents(a));
            fluid_synth_set_gen(synth_, ch, GEN_VOLENVRELEASE,
                                (float) timecents(r));
        }
        lastGain_ = g;
        lastBright_ = b;
        lastAtk_ = a;
        lastRel_ = r;
        ctrlDirty_ = false;
    }

    juce::AudioParameterChoice* program_ = nullptr;
    juce::AudioParameterBool* kit_ = nullptr;
    juce::AudioParameterFloat* gainP_ = nullptr;
    juce::AudioParameterFloat* brightP_ = nullptr;
    juce::AudioParameterFloat* attackP_ = nullptr;
    juce::AudioParameterFloat* releaseP_ = nullptr;

    fluid_synth_t* synth_ = nullptr;
    fluid_settings_t* settings_ = nullptr;
    int sfid_ = -1;
    double sampleRate_ = 48000.0;
    int lastProg_ = -1;
    bool lastKit_ = false;
    bool progDirty_ = true;
    float lastGain_ = -1.f, lastBright_ = 0.f, lastAtk_ = 0.f, lastRel_ = 0.f;
    bool ctrlDirty_ = true;
};

// ---- Editor: Mixcraft-style catalog browser + instrument controls -------
class EzInstEditor : public juce::AudioProcessorEditor,
                     private juce::AudioProcessorParameter::Listener
{
public:
    explicit EzInstEditor(EzInstProcessor& proc)
        : juce::AudioProcessorEditor(&proc), proc_(proc)
    {
        for (int k = 0; k < numParams; ++k)
            param(k)->addListener(this);

        search_.setMultiLine(false, false);
        search_.setTextToShowWhenEmpty(u8("Buscar instrumento…"),
                                       juce::Colour(0xFF5C6270));
        search_.setFont(juce::FontOptions(13.0f));
        search_.onTextChange = [this] { refreshInstrumentRows(); };
        addAndMakeVisible(search_);

        progLabel_.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
        progLabel_.setColour(juce::Label::textColourId,
                             juce::Colour(0xFFE8EAEE));
        progLabel_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(progLabel_);

        kit_.setButtonText(u8("Kit percusión"));
        kit_.onClick = [this] {
            auto* p = proc_.kit_;
            p->setValueNotifyingHost(
                p->convertTo0to1(kit_.getToggleState() ? 1.f : 0.f));
        };
        addAndMakeVisible(kit_);

        catList_.setModel(&catModel_);
        catList_.setRowHeight(20);
        catList_.setColour(juce::ListBox::backgroundColourId,
                           juce::Colour(0xFF1B1D22));
        catModel_.e = this;
        instModel_.e = this;
        catList_.selectRow(0);
        addAndMakeVisible(catList_);

        instList_.setModel(&instModel_);
        instList_.setRowHeight(20);
        instList_.setColour(juce::ListBox::backgroundColourId,
                            juce::Colour(0xFF1B1D22));
        addAndMakeVisible(instList_);

        styleLabel(lvlL_, "Nivel");
        styleLabel(brtL_, "Brillo (semitonos)");
        styleLabel(atkL_, "Ataque (s)");
        styleLabel(relL_, "Release (s)");
        styleSlider(lvlS_, 0.0, 2.0, 1.0, "", 44);
        styleSlider(brtS_, -4800.0, 4800.0, 0.5, "", 56);
        styleSlider(atkS_, 0.0, 1.0, 1.0, "", 44);
        styleSlider(relS_, -0.5, 2.0, 1.0, "", 44);
        lvlS_.onValueChange = [this] {
            auto* p = proc_.gainP_;
            p->setValueNotifyingHost(p->convertTo0to1((float) lvlS_.getValue()));
        };
        brtS_.onValueChange = [this] {
            auto* p = proc_.brightP_;
            p->setValueNotifyingHost(p->convertTo0to1((float) brtS_.getValue()));
        };
        atkS_.onValueChange = [this] {
            auto* p = proc_.attackP_;
            p->setValueNotifyingHost(p->convertTo0to1((float) atkS_.getValue()));
        };
        relS_.onValueChange = [this] {
            auto* p = proc_.releaseP_;
            p->setValueNotifyingHost(p->convertTo0to1((float) relS_.getValue()));
        };

        updateControls();
        setSize(940, 520);
    }

    ~EzInstEditor() override
    {
        for (int k = 0; k < numParams; ++k)
            param(k)->removeListener(this);
    }

    void parameterValueChanged(int, float) override { updateControls(); }
    void parameterGestureChanged(int, bool) override {}

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF16171B));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto header = area.removeFromTop(28);
        search_.setBounds(header.removeFromLeft(230));
        kit_.setBounds(header.removeFromRight(130));
        progLabel_.setBounds(header);

        auto body = area.removeFromTop(area.getHeight() - 64);
        catList_.setBounds(body.removeFromLeft(160));
        instList_.setBounds(body.reduced(4, 0));

        auto bar = area;
        const int cellW = bar.getWidth() / 4;
        juce::Rectangle<int> cells[4] = { bar.removeFromLeft(cellW),
                                          bar.removeFromLeft(cellW),
                                          bar.removeFromLeft(cellW), bar };
        juce::Label* labels[4] = { &lvlL_, &brtL_, &atkL_, &relL_ };
        juce::Slider* sliders[4] = { &lvlS_, &brtS_, &atkS_, &relS_ };
        for (int i = 0; i < 4; ++i)
        {
            auto c = cells[i];
            labels[i]->setBounds(c.removeFromTop(14));
            sliders[i]->setBounds(c.reduced(2, 8));
        }
    }

private:
    static constexpr int numParams = 6;
    static constexpr int kRowH = 20;

    juce::AudioProcessorParameter* param(int k)
    {
        switch (k)
        {
            case 0: return proc_.program_;
            case 1: return proc_.kit_;
            case 2: return proc_.gainP_;
            case 3: return proc_.brightP_;
            case 4: return proc_.attackP_;
            default: return proc_.releaseP_;
        }
    }

    void styleLabel(juce::Label& l, const char* title)
    {
        l.setText(title, juce::dontSendNotification);
        l.setFont(juce::FontOptions(11.0f));
        l.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA2B1));
        addAndMakeVisible(l);
    }
    void styleSlider(juce::Slider& s, double lo, double hi, double skew,
                     const char* suffix, int boxW)
    {
        s.setSliderStyle(juce::Slider::LinearBar);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxW, 18);
        s.setRange(lo, hi, 0.01);
        s.setSkewFactor(skew);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);
    }

    void updateControls()
    {
        const int prog = proc_.program_->getIndex();
        progLabel_.setText(juce::String(prog + 1) + u8(" · ") +
                               kProgramNames[prog],
                           juce::dontSendNotification);
        kit_.setToggleState(proc_.kit_->get() != 0,
                            juce::dontSendNotification);
        lvlS_.setValue(proc_.gainP_->get(), juce::dontSendNotification);
        brtS_.setValue(proc_.brightP_->get(), juce::dontSendNotification);
        atkS_.setValue(proc_.attackP_->get(), juce::dontSendNotification);
        relS_.setValue(proc_.releaseP_->get(), juce::dontSendNotification);
        syncInstrumentSelection();
        instList_.repaint();
        catList_.repaint();
    }

    // Row list: search results (all categories) or the selected category.
    std::vector<int> rows_;

    void refreshInstrumentRows()
    {
        rows_.clear();
        const auto query = search_.getText().trim();
        if (query.isNotEmpty())
        {
            for (int i = 0; i < 128; ++i)
                if (juce::String(kProgramNames[i]).containsIgnoreCase(query))
                    rows_.push_back(i);
        }
        else
        {
            const int start = kCategories[catModel_.lastCat].start;
            for (int i = start; i < start + 8; ++i)
                rows_.push_back(i);
        }
        instList_.updateContent();
        syncInstrumentSelection();
    }

    void syncInstrumentSelection()
    {
        const int prog = proc_.program_->getIndex();
        int row = -1;
        for (int i = 0; i < (int) rows_.size(); ++i)
            if (rows_[(size_t) i] == prog) { row = i; break; }
        if (row >= 0 && instList_.getSelectedRow() != row)
            instList_.selectRow(row, true, true);
        instList_.repaint();
    }

    void chooseProgram(int programIndex)
    {
        auto* p = proc_.program_;
        p->setValueNotifyingHost(
            p->convertTo0to1((float) juce::jlimit(0, 127, programIndex)));
    }

    struct CatModel : juce::ListBoxModel
    {
        EzInstEditor* e = nullptr;
        int lastCat = 0;

        int getNumRows() override { return 16; }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                              bool selected) override
        {
            if (selected)
            {
                g.setColour(juce::Colour(0xFF4D9DF7));
                g.fillRect(0, 0, w, h);
            }
            g.setColour(selected ? juce::Colours::white
                                 : juce::Colour(0xFFC7CBD4));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(u8(kCategories[row].name), 6, 0, w - 8, h,
                       juce::Justification::centredLeft);
        }

        void selectedRowsChanged(int row) override
        {
            if (row < 0 || e == nullptr)
                return;
            lastCat = row;
            e->search_.clear();
            e->refreshInstrumentRows();
        }
    } catModel_;

    struct InstModel : juce::ListBoxModel
    {
        EzInstEditor* e = nullptr;

        int getNumRows() override
        {
            return e != nullptr ? (int) e->rows_.size() : 0;
        }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                              bool selected) override
        {
            if (e == nullptr || row >= (int) e->rows_.size())
                return;
            const int prog = e->rows_[(size_t) row];
            if (selected)
            {
                g.setColour(juce::Colour(0xFF4D9DF7));
                g.fillRect(0, 0, w, h);
            }
            g.setColour(selected ? juce::Colours::white
                                 : juce::Colour(0xFFC7CBD4));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(juce::String(prog + 1) + "  " + kProgramNames[prog],
                       8, 0, w - 10, h, juce::Justification::centredLeft);
        }

        void selectedRowsChanged(int row) override
        {
            if (e == nullptr || row < 0 || row >= (int) e->rows_.size())
                return;
            e->chooseProgram(e->rows_[(size_t) row]);
        }
    } instModel_;

    EzInstProcessor& proc_;
    juce::TextEditor search_;
    juce::ListBox catList_, instList_;
    juce::Label progLabel_;
    juce::ToggleButton kit_;
    juce::Slider lvlS_, brtS_, atkS_, relS_;
    juce::Label lvlL_, brtL_, atkL_, relL_;
};

} // namespace

// Editor needs access to the internal rows machinery — defined out of line
// so the editor class can follow the processor.
juce::AudioProcessorEditor* EzInstProcessor::createEditor()
{
    return new EzInstEditor(*this);
}

juce::AudioProcessor* create() { return new EzInstProcessor(); }

} // namespace ezinst

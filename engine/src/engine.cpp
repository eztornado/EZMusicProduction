#include "cadence/engine.hpp"
#include "ai_generation.h"
#include "ezq4.h"
#include "ezinst.h"
#include "ezsynth.h"
#include "ezorq.h"
#include "ezdrums.h"
#include "ezarp.h"
#include "native_dialogs.h"
#include "piano_voice.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace cadence {

namespace {
constexpr double kMinClipLength = 0.05; // seconds; one gesture cannot vanish a clip

float absMax(const float* data, int numSamples)
{
    const auto range = juce::Range<float>::findMinAndMax(data, numSamples);
    return std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
}

// Stereo pan gains. Mono material uses the constant-power law, which
// attenuates a centre-panned signal by 3 dB — the same default stereo pan law
// Cubase applies to mono channels. Stereo material keeps unity at centre
// instead (balance), matching Cubase's stereo panner, where the project pan
// law does not apply: dropping a stereo file on a track and leaving it
// centred must not lose 3 dB.
void panGains(bool stereo, float pan, float gain, float& left, float& right)
{
    const float p = juce::jlimit(-1.0f, 1.0f, pan);
    if (stereo)
    {
        left  = juce::jmin(1.0f, 1.0f - p) * gain;
        right = juce::jmin(1.0f, 1.0f + p) * gain;
        return;
    }
    const float angle = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    left  = std::cos(angle) * gain;
    right = std::sin(angle) * gain;
}

// True when any clip plays a multi-channel (stereo) source, which selects the
// balance pan law above (see panGains).
bool hasStereoSource(const Project& project, const Track& track)
{
    for (const auto& clip : track.clips)
    {
        const auto it = project.sources.find(clip.sourceId);
        if (it != project.sources.end() && it->second != nullptr
            && it->second->samples.getNumChannels() >= 2)
            return true;
    }
    return false;
}

// Automation curve evaluation (§7.12): linear interpolation between sorted
// points, clamped before the first and after the last.
float evalCurveAt(const AutomationCurve* curve, std::int64_t samplePos,
                  double rate, float fallback)
{
    if (curve == nullptr || curve->points.empty())
        return fallback;
    const double t = (double) samplePos / rate;
    const auto& pts = curve->points;
    if (t <= pts.front().timeSeconds)
        return (float) pts.front().value;
    if (t >= pts.back().timeSeconds)
        return (float) pts.back().value;
    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (t <= pts[i].timeSeconds)
        {
            const auto& a = pts[i - 1];
            const auto& b = pts[i];
            const double f = (t - a.timeSeconds)
                             / std::max(1e-9, b.timeSeconds - a.timeSeconds);
            return (float) (a.value + (b.value - a.value) * f);
        }
    }
    return (float) pts.back().value;
}

// Step evaluation for on/off lanes (sustain): the value of the LAST point
// at or before the position holds until the next point (square wave).
float evalStepAt(const AutomationCurve* curve, std::int64_t samplePos,
                 double rate)
{
    if (curve == nullptr || curve->points.empty())
        return 0.0f;
    const double t = (double) samplePos / rate;
    const auto& pts = curve->points;
    if (t <= pts.front().timeSeconds)
        return 0.0f;
    const AutomationPoint* current = &pts.front();
    for (const auto& point : pts)
    {
        if (point.timeSeconds > t)
            break;
        current = &point;
    }
    return (float) current->value;
}

// Volume automation (§7.12): falls back to the static volume when the track
// has no curve.
float volumeAt(const Track& track, const AutomationCurve* curve,
               std::int64_t samplePos, double rate)
{
    if (curve == nullptr || curve->points.empty())
        return track.volume;
    const double t = (double) samplePos / rate;
    const auto& pts = curve->points;
    if (t <= pts.front().timeSeconds)
        return (float) pts.front().value;
    if (t >= pts.back().timeSeconds)
        return (float) pts.back().value;
    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (t <= pts[i].timeSeconds)
        {
            const auto& a = pts[i - 1];
            const auto& b = pts[i];
            const double f = (t - a.timeSeconds)
                             / std::max(1e-9, b.timeSeconds - a.timeSeconds);
            return (float) (a.value + (b.value - a.value) * f);
        }
    }
    return (float) pts.back().value;
}

// Full MIDI panic. CC123 alone is not enough: several hosted instruments
// (including our own EZOrquestra) ignore it, and an instrument that only
// handles note-offs keeps every voice alive after a stop. Sustain-off goes
// first so the note-offs are not converted back into held notes.
void addPanicMessages(juce::MidiBuffer& midi)
{
    for (int channel = 1; channel <= 16; ++channel)
    {
        midi.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
        for (int note = 0; note < 128; ++note)
            midi.addEvent(juce::MidiMessage::noteOff(channel, note), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(channel, 120, 0), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(channel, 123, 0), 0);
    }
}

// Hard-stop every voice of an internal synth. allNotesOff(..., false) is not
// enough for PianoVoice: it keeps the voice active while its short release
// decays, which would be heard again when playback resumes.
void silenceSynthVoices(juce::Synthesiser& synth)
{
    for (int channel = 1; channel <= 16; ++channel)
        synth.allNotesOff(channel, false);
    for (int v = 0; v < synth.getNumVoices(); ++v)
    {
        auto* voice = synth.getVoice(v);
        if (voice == nullptr)
            continue;
        voice->controllerMoved(64, 0); // release the sustain pedal
        if (auto* piano = dynamic_cast<PianoVoice*>(voice))
            piano->hardStop();
    }
}
} // namespace

// Test hook definition (used by RecorderCallback; see setDebugLoopback).
std::atomic<bool> Engine::debugLoopback_ { false };

// ---------- Native instrument (v8/v9): shared piano voice ----------
// PianoSound/PianoVoice live in piano_voice.h — one implementation
// backs the zero-config fallback AND the EZSynth built-in instrument.

// ---------- PluginHost ----------

void PluginHost::setup()
{
    formatManager_.addDefaultFormats();
    deadMansPedalFile_.getParentDirectory().createDirectory();

    // Previous scans are cached, so a session starts with the full list and
    // only pays the (minutes-long) scan again when it is explicitly kicked.
    if (cacheFile_.existsAsFile())
        if (auto xml = juce::parseXML(cacheFile_))
            known_.recreateFromXml(*xml);

    // Built-in DSP (EZ EQ Pro): first in the list, instantiated directly.
    juce::PluginDescription d;
    d.pluginFormatName = "Internal";
    d.name = "EZ EQ Pro";
    d.descriptiveName = "EZ EQ Pro";
    d.manufacturerName = "EZMusicProduction";
    d.uniqueId = (int) (juce::String("ezq4").hashCode());
    d.fileOrIdentifier = "internal:ezq4";
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    d.isInstrument = false;
    d.category = "Effect";
    builtins_.push_back(d);

    // Built-in instrument (EZInstruments): the FluidSynth-backed GM catalog.
    juce::PluginDescription di;
    di.pluginFormatName = "Internal";
    di.name = "EZInstruments";
    di.descriptiveName = "EZInstruments";
    di.manufacturerName = "EZMusicProduction";
    di.uniqueId = (int) (juce::String("ezinst").hashCode());
    di.fileOrIdentifier = "internal:ezinst";
    di.numInputChannels = 0;
    di.numOutputChannels = 2;
    di.isInstrument = true;
    di.category = "Instrument";
    builtins_.push_back(di);

    // Built-in instrument (EZSynth): the shared piano voice as a plugin —
    // selectable, persistent, automatable.
    juce::PluginDescription dk;
    dk.pluginFormatName = "Internal";
    dk.name = "EZSynth";
    dk.descriptiveName = "EZSynth";
    dk.manufacturerName = "EZMusicProduction";
    dk.uniqueId = (int) (juce::String("ezsynth").hashCode());
    dk.fileOrIdentifier = "internal:ezsynth";
    dk.numInputChannels = 0;
    dk.numOutputChannels = 2;
    dk.isInstrument = true;
    dk.category = "Instrument";
    builtins_.push_back(dk);

    // Built-in instrument (EZOrquestra): orchestral sampler over freely
    // licensed WAV libraries (Alpine Project layout).
    juce::PluginDescription doq;
    doq.pluginFormatName = "Internal";
    doq.name = "EZOrquestra";
    doq.descriptiveName = "EZOrquestra";
    doq.manufacturerName = "EZMusicProduction";
    doq.uniqueId = (int) (juce::String("ezorq").hashCode());
    doq.fileOrIdentifier = "internal:ezorq";
    doq.numInputChannels = 0;
    doq.numOutputChannels = 2;
    doq.isInstrument = true;
    doq.category = "Instrument";
    builtins_.push_back(doq);

    // Built-in instrument (EZDrums): drum machine with pattern banks —
    // generates its own MIDI, so recording an armed track captures the
    // pattern into a normal clip.
    juce::PluginDescription dd;
    dd.pluginFormatName = "Internal";
    dd.name = "EZDrums";
    dd.descriptiveName = "EZDrums";
    dd.manufacturerName = "EZMusicProduction";
    dd.uniqueId = (int) (juce::String("ezdrums").hashCode());
    dd.fileOrIdentifier = "internal:ezdrums";
    dd.numInputChannels = 0;
    dd.numOutputChannels = 2;
    dd.isInstrument = true;
    dd.category = "Instrument";
    builtins_.push_back(dd);

    // Built-in MIDI effect (EZArpeggio): arpeggiator insert for instrument
    // tracks — transforms the track's MIDI stream pre-instrument.
    juce::PluginDescription da;
    da.pluginFormatName = "Internal";
    da.name = "EZArpeggio";
    da.descriptiveName = "EZArpeggio";
    da.manufacturerName = "EZMusicProduction";
    da.uniqueId = (int) (juce::String("ezarp").hashCode());
    da.fileOrIdentifier = "internal:ezarp";
    da.numInputChannels = 0;
    da.numOutputChannels = 0;
    da.isInstrument = false;
    da.category = "Effect";
    builtins_.push_back(da);
}

void PluginHost::beginScan()
{
    if (scanning_.exchange(true))
        return;
    buildScanners();
    triggerAsyncUpdate();
}

void PluginHost::scanNow()
{
    // Blocking variant for hosts without a pumped run loop (CLI, tests):
    // scans everything in one go. Must run on the message thread.
    //
    // Plugin code is hostile — it can trap (ObjC class collisions) or hang
    // forever (license daemons). Real hosts scan out-of-process; until then
    // we self-heal: the in-flight file is recorded before every step, and a
    // watchdog kills the process on a hung step. The next run blacklists
    // whatever the marker held and continues from there.
    if (scanning_.exchange(true))
        return;

    if (inFlightFile_.existsAsFile())
    {
        appendToDeadMansPedal(inFlightFile_.loadFileAsString().trim());
        inFlightFile_.deleteFile();
    }

    buildScanners();
    stepDeadline_.store(juce::Time::getMillisecondCounter() + (juce::uint64) 30000);
    std::thread watchdog([this] {
        while (scanning_.load())
        {
            if (juce::Time::getMillisecondCounter() > stepDeadline_.load())
            {
                appendToDeadMansPedal(inFlightFile_.loadFileAsString().trim());
                std::_Exit(137); // brutal on purpose: a hung AU is unkillable in-process
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });

    while (scanStep())
    {
        stepDeadline_.store(juce::Time::getMillisecondCounter() + (juce::uint64) 30000);
    }
    scanning_ = false;
    watchdog.join();
    saveCache();
}

void PluginHost::buildScanners()
{
    scanList_ = std::make_unique<juce::KnownPluginList>();
    // Seed with everything already known (cache + this session's merges):
    // with dontRescanIfAlreadyInList the scanner skips those files, so
    // repeated healing attempts only pay for the not-yet-scanned tail.
    {
        std::lock_guard<std::mutex> lock(knownMutex_);
        for (int i = 0; i < known_.getNumTypes(); ++i)
            if (auto* t = known_.getType(i))
                scanList_->addType(*t);
    }
    scanners_.clear();
    scannerIndex_ = 0;
    for (auto* format : formatManager_.getFormats())
    {
        // The scanner resolves the search paths itself (it calls
        // searchPathsForPlugins internally) — pass the raw default locations,
        // NOT the resolved plugin file list.
        scanners_.push_back(std::make_unique<juce::PluginDirectoryScanner>(
            *scanList_, *format, format->getDefaultLocationsToSearch(), true,
            deadMansPedalFile_));
    }
}

bool PluginHost::scanStep()
{
    while (scannerIndex_ < scanners_.size())
    {
        const juce::String next =
            scanners_[scannerIndex_]->getNextPluginFileThatWillBeScanned();
        if (next.isEmpty())
        {
            ++scannerIndex_; // this format is exhausted
            continue;
        }
        writeInFlightFile(next);
        juce::String name;
        const bool more = scanners_[scannerIndex_]->scanNextFile(true, name);
        mergeNewTypes();
        inFlightFile_.deleteFile();
        if (more)
            return true;
        ++scannerIndex_;
    }
    return false;
}

void PluginHost::writeInFlightFile(const juce::String& file) const
{
    inFlightFile_.deleteFile();
    if (auto out = inFlightFile_.createOutputStream())
    {
        out->writeText(file, false, false, "\n");
        out->flush();
    }
}

void PluginHost::appendToDeadMansPedal(const juce::String& file) const
{
    if (file.isEmpty())
        return;
    juce::StringArray lines;
    deadMansPedalFile_.readLines(lines);
    lines.removeEmptyStrings();
    if (! lines.contains(file))
        lines.add(file);
    deadMansPedalFile_.deleteFile();
    if (auto out = deadMansPedalFile_.createOutputStream())
    {
        out->writeText(lines.joinIntoString("\n"), false, false, "\n");
        out->flush();
    }
}

void PluginHost::handleAsyncUpdate()
{
    // One file per event: heavy plugins take seconds to instantiate, and the
    // message thread must stay responsive (device ops marshal through it).
    if (scanStep())
        triggerAsyncUpdate();
    else
    {
        scanning_ = false;
        saveCache();
    }
}

void PluginHost::mergeNewTypes()
{
    if (scanList_ == nullptr)
        return;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(knownMutex_);
        for (int i = 0; i < scanList_->getNumTypes(); ++i)
            if (auto* desc = scanList_->getType(i))
                changed = known_.addType(*desc) || changed;
    }
    if (changed)
        saveCache(); // persist progress: crashes must not lose scanned plugins
}

void PluginHost::saveCache() const
{
    auto xml = [&] {
        std::lock_guard<std::mutex> lock(knownMutex_);
        return known_.createXml();
    }();
    if (xml == nullptr)
        return;
    cacheFile_.deleteFile(); // FileOutputStream would append to an existing file
    if (auto out = cacheFile_.createOutputStream())
        xml->writeTo(*out);
}

int PluginHost::pluginCount() const
{
    std::lock_guard<std::mutex> lock(knownMutex_);
    return builtinCount() + known_.getNumTypes();
}

std::unique_ptr<juce::PluginDescription> PluginHost::descriptionAt(int index) const
{
    std::lock_guard<std::mutex> lock(knownMutex_);
    if (index >= 0 && index < builtinCount())
        return std::make_unique<juce::PluginDescription>(builtins_[(size_t) index]);
    if (auto* desc = known_.getType(index - builtinCount()))
        return std::make_unique<juce::PluginDescription>(*desc);
    return nullptr;
}

std::unique_ptr<juce::AudioProcessor> PluginHost::create(
    const juce::String& uid, double sampleRate, int blockSize)
{
    {
        std::lock_guard<std::mutex> lock(knownMutex_);
        // Built-ins (internal DSP/instruments) are plain AudioProcessors.
        for (const auto& b : builtins_)
            if (b.createIdentifierString() == uid)
            {
                if (b.fileOrIdentifier == "internal:ezinst")
                    return std::unique_ptr<juce::AudioProcessor>(
                        ezinst::create());
                if (b.fileOrIdentifier == "internal:ezsynth")
                    return std::unique_ptr<juce::AudioProcessor>(
                        ezsynth::create());
                if (b.fileOrIdentifier == "internal:ezorq")
                    return std::unique_ptr<juce::AudioProcessor>(
                        ezorq::create());
                if (b.fileOrIdentifier == "internal:ezdrums")
                    return std::unique_ptr<juce::AudioProcessor>(
                        ezdrums::create());
                if (b.fileOrIdentifier == "internal:ezarp")
                    return std::unique_ptr<juce::AudioProcessor>(
                        ezarp::create());
                return std::unique_ptr<juce::AudioProcessor>(ezq4::create());
            }
    }
    std::unique_ptr<juce::PluginDescription> desc;
    {
        std::lock_guard<std::mutex> lock(knownMutex_);
        desc = known_.getTypeForIdentifierString(uid);
        if (desc == nullptr)
        {
            // Plugins update and their identifier string changes (AU
            // uniqueId, version bumps) — saved projects must keep working.
            // Re-link by format + name (the identifier is
            // "format-name-hash-hash"; the name may itself contain dashes,
            // so the name ends before the last two hash segments).
            const int firstDash = uid.indexOf("-");
            const int lastDash = uid.lastIndexOf("-");
            int prevDash = -1;
            for (int i = lastDash - 1; i > firstDash; --i)
                if (uid[i] == '-') { prevDash = i; break; }
            if (firstDash > 0 && prevDash > firstDash)
            {
                const auto format = uid.substring(0, firstDash);
                const auto name = uid.substring(firstDash + 1, prevDash);
                for (int i = 0; i < known_.getNumTypes(); ++i)
                {
                    auto* t = known_.getType(i);
                    if (t->pluginFormatName == format && t->name == name)
                    {
                        desc = std::make_unique<juce::PluginDescription>(*t);
                        break;
                    }
                }
            }
        }
    }
    if (desc == nullptr)
        return nullptr;
    juce::String error;
    return formatManager_.createPluginInstance(*desc, sampleRate,
                                               blockSize, error);
}

// ---------- ProjectAudioSource ----------

namespace {

// Some FX (iZotope Ozone etc.) ship with their input bus DISABLED and a
// plain setPlayConfigDetails does not enable it — processBlock then sees an
// empty input and the plugin "receives no audio". Force every bus to stereo
// where the plugin allows it, before preparing.
void activateStereoBuses(juce::AudioProcessor& proc)
{
    // Strategy: try all-stereo first; if the plugin rejects the whole
    // layout (aux buses with their own requirements — iZotope does this),
    // fall back to main-bus-only with the aux buses disabled. VERIFY the
    // result: a silently-failed request leaves the input bus disabled and
    // the plugin passes audio through unprocessed forever.
    const auto inputCount = proc.getBusCount(true);
    const auto outputCount = proc.getBusCount(false);

    juce::AudioProcessor::BusesLayout allStereo;
    for (int i = 0; i < inputCount; ++i)
        allStereo.inputBuses.add(juce::AudioChannelSet::stereo());
    for (int i = 0; i < outputCount; ++i)
        allStereo.outputBuses.add(juce::AudioChannelSet::stereo());

    if (! proc.setBusesLayout(allStereo))
    {
        juce::AudioProcessor::BusesLayout mainOnly;
        for (int i = 0; i < inputCount; ++i)
            mainOnly.inputBuses.add(i == 0
                ? juce::AudioChannelSet::stereo()
                : juce::AudioChannelSet::disabled());
        for (int i = 0; i < outputCount; ++i)
            mainOnly.outputBuses.add(i == 0
                ? juce::AudioChannelSet::stereo()
                : juce::AudioChannelSet::disabled());
        if (! proc.setBusesLayout(mainOnly))
            juce::Logger::writeToLog(
                "[plugins] could not enable input bus of "
                + proc.getName());
    }

    const auto mainIn = proc.getBusesLayout()
                            .getMainInputChannelSet();
    if (mainIn.isDisabled() || mainIn.size() == 0)
        juce::Logger::writeToLog("[plugins] WARNING input bus disabled on "
            + proc.getName());
}

} // namespace

namespace {

// Native window hosting one plugin's own editor component. The shared_ptr
// keeps the instance alive even if a later reconcile rebuilds the chain.
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    explicit PluginEditorWindow(std::shared_ptr<juce::AudioProcessor> proc)
        : DocumentWindow(proc->getName(), juce::Colour(0xFF252830),
                         juce::DocumentWindow::allButtons),
          proc_(std::move(proc))
    {
        setUsingNativeTitleBar(true);
        if (auto* editor = proc_->createEditorIfNeeded())
        {
            setContentNonOwned(editor, true);
            // Minimum viable size: small editors (built-ins) would otherwise
            // open at near-zero dimensions.
            centreWithSize(juce::jmax(600, editor->getWidth()),
                           juce::jmax(340, editor->getHeight()));
        }
        else
        {
            // Plugins without an editor still get a parameter sheet.
            setContentOwned(new juce::GenericAudioProcessorEditor(*proc_), true);
            centreWithSize(420, 520);
        }
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    std::shared_ptr<juce::AudioProcessor> proc_;
};

} // namespace

void ProjectAudioSource::setProject(std::shared_ptr<const Project> newProject)
{
    // Never erase synths while the audio thread may hold a raw pointer to
    // one. They are tiny and reused if the track id comes back (ids are
    // monotonic, so in practice only removed tracks keep their allocation).
    const double rate = deviceRate_.load(std::memory_order_acquire) > 0.0
        ? deviceRate_.load(std::memory_order_acquire)
        : newProject->sampleRate;
    prepareInternalSynths(*newProject, rate);
    std::atomic_store(&project_, newProject);
}

void ProjectAudioSource::play()
{
    playing_.store(true);
}

void ProjectAudioSource::pause()
{
    playing_.store(false);
}

void ProjectAudioSource::stop()
{
    playing_.store(false);
    positionSamples_.store(0);
}

void ProjectAudioSource::seek(double seconds)
{
    const auto project = std::atomic_load(&project_);
    const double rate = project ? project->sampleRate : 48000.0;
    positionSamples_.store((std::int64_t) std::max(0.0, seconds * rate));
}

double ProjectAudioSource::positionSeconds() const
{
    const auto project = std::atomic_load(&project_);
    const double rate = project ? project->sampleRate : 48000.0;
    return (double) positionSamples_.load() / rate;
}

std::int64_t ProjectAudioSource::positionSamples() const
{
    return positionSamples_.load();
}

void ProjectAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    const int block = juce::jmax(1, samplesPerBlockExpected);
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    // Device configuration first: the audio thread and reconcileNodes() use
    // these values to decide whether a plugin is safe to process.
    deviceRate_.store(rate, std::memory_order_release);
    deviceBlock_.store(block, std::memory_order_release);

    // Preallocate every buffer the callback can touch. The old code resized
    // trackScratch_/midiFxSilent_ inside getNextAudioBlock(), which could
    // allocate (and therefore block) on the audio thread after a device or
    // buffer-size change.
    trackScratch_.setSize(2, block, false, false, true);
    midiFxSilent_.setSize(1, block, false, false, true);
    masterBuffer_.setSize(2, block, false, false, true);
    // Reserve the panic buffer up front: stop/seek runs on the audio thread
    // and must not allocate while delivering 16-channel note-offs.
    blockMidi_.ensureSize(8192);
    panicMidi_.ensureSize(4096);
    fxEmptyMidi_.ensureSize(1024);
    fxEmptyMidi_.clear();
    panicMidi_.clear();
    addPanicMessages(panicMidi_);
    lastSustainCc_.fill(0);

    // Prepare the fallback synths for the new device rate on the message
    // thread, not on the first note of the audio callback.
    if (auto project = std::atomic_load(&project_))
        prepareInternalSynths(*project, rate);
    {
        const juce::SpinLock::ScopedLockType lock(synthsLock_);
        if (std::abs(synthsRate_ - rate) > 0.5)
        {
            for (auto& [id, synth] : synths_)
                synth->setCurrentPlaybackSampleRate(rate);
            synthsRate_ = rate;
        }
    }
}

void ProjectAudioSource::releaseResources()
{
    // The device is stopped here, so no audio thread is inside the callback.
    // Free the large scratch buffers; prepareToPlay() allocates them again
    // before the next stream starts.
    trackScratch_.setSize(0, 0);
    midiFxSilent_.setSize(0, 0);
    masterBuffer_.setSize(0, 0);
    deviceRate_.store(0.0, std::memory_order_release);
    deviceBlock_.store(0, std::memory_order_release);
}

juce::Synthesiser* ProjectAudioSource::getOrCreateSynth(TrackId trackId, double rate)
{
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    if (std::abs(synthsRate_ - rate) > 0.5)
    {
        for (auto& [id, s] : synths_)
            s->setCurrentPlaybackSampleRate(rate);
        synthsRate_ = rate;
    }

    auto& slot = synths_[trackId];
    if (slot == nullptr)
    {
        slot = std::make_unique<juce::Synthesiser>();
        slot->addSound(new PianoSound());
        for (int v = 0; v < 8; ++v)
            slot->addVoice(new PianoVoice());
        // Voices stuck in a long release (or left claimed by a stopped pass)
        // must never make a new noteOn silently disappear.
        slot->setNoteStealingEnabled(true);
        slot->setCurrentPlaybackSampleRate(rate);
    }
    return slot.get();
}

void ProjectAudioSource::silenceSynthForTrack(TrackId trackId)
{
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    auto it = synths_.find(trackId);
    if (it != synths_.end())
        silenceSynthVoices(*it->second);
}

void ProjectAudioSource::prepareInternalSynths(const Project& project, double rate)
{
    if (rate <= 0.0)
        rate = 48000.0;
    for (const auto& track : project.tracks)
        if (track.type == Track::Type::Instrument)
            getOrCreateSynth(track.id, rate);
}

TrackAudioNode& ProjectAudioSource::ensureNode(TrackId trackId)
{
    // The map is read by the audio thread under synthsLock_. Insertions and
    // the first state publication must take the same lock; the old code
    // inserted with trackNodesMutex_ only and raced nodeFor().
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    auto& nodePtr = trackNodes_[trackId];
    if (nodePtr == nullptr)
    {
        nodePtr = std::make_unique<TrackAudioNode>();
        nodePtr->state = std::make_shared<const TrackNodeState>();
    }
    return *nodePtr;
}

void ProjectAudioSource::pushExternalMidi(const juce::MidiMessage& m)
{
    if (offlineRender_.load(std::memory_order_acquire))
        return;
    const std::scoped_lock lock(externalMutex_);
    externalQueue_.push_back(m);
}

void ProjectAudioSource::drainExternalMidi()
{
    const std::scoped_lock lock(externalMutex_);
    pendingExternal_.swap(externalQueue_);
    externalQueue_.clear();
}

void ProjectAudioSource::setRecording(bool r, std::int64_t startSamples,
                                      std::shared_ptr<const CaptureList> caps)
{
    recording_.store(r);
    recordStartSamples_.store(r ? startSamples : 0);
    if (r)
        std::atomic_store(&captureList_, caps);
    else
        std::atomic_store(&captureList_,
                          std::shared_ptr<const CaptureList>());
}

// Live monitoring (§7.11): with the transport stopped, armed instrument
// tracks still voice the incoming MIDI notes through their synth.
void ProjectAudioSource::renderArmedInstrumentMonitoring(
    const juce::AudioSourceChannelInfo& info, const Project& project)
{
    // NOTE: this must render EVERY block while a key is held — the noteOn
    // arrives in exactly one block, and held voices (synth or plugin) only
    // keep sounding if the track keeps being rendered. Gating on "this block
    // has new MIDI events" made notes die after ~10 ms.
    if (trackScratch_.getNumSamples() < info.numSamples)
        trackScratch_.setSize(2, info.numSamples, false, false, true);

    const double deviceRate = deviceRate_.load(std::memory_order_acquire);
    const double pluginRate = deviceRate > 0.0 ? deviceRate : project.sampleRate;

    int trackIndex = 0;
    for (const auto& track : project.tracks)
    {
        const int meterSlot = trackIndex++ & (kMeterTracks - 1);
        if (track.type != Track::Type::Instrument || track.mute
            || ! track.armed)
            continue;

        blockMidi_.clear();
        for (const auto& m : pendingExternal_)
            blockMidi_.addEvent(m, 0);

        // Live input goes through the instrument plugin when one is loaded,
        // exactly like playback — the internal synth is only the fallback.
        auto* node = nodeFor(track.id);
        auto state = node != nullptr ? std::atomic_load(&node->state) : nullptr;
        const bool hasInstrumentPlugin =
            state != nullptr && state->instrument != nullptr
            && ! state->instrumentBypass
            && state->instrumentPreparedRate == pluginRate
            && info.numSamples <= state->instrumentPreparedBlock;

        if (hasInstrumentPlugin)
        {
            trackScratch_.clear();
            state->instrument->processBlock(trackScratch_, blockMidi_);
        }
        else
        {
            auto* synth = getOrCreateSynth(track.id, pluginRate);
            trackScratch_.clear();
            synth->renderNextBlock(trackScratch_, blockMidi_, 0,
                                   info.numSamples);
        }

        float trackPeak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            trackPeak = std::max(trackPeak,
                absMax(trackScratch_.getReadPointer(ch, 0), info.numSamples));
        trackPeaks_[meterSlot].store(std::bit_cast<std::uint32_t>(trackPeak),
                                     std::memory_order_relaxed);

        const float angle = (juce::jlimit(-1.0f, 1.0f, track.pan) + 1.0f)
                            * juce::MathConstants<float>::pi * 0.25f;
        const float left = std::cos(angle) * track.volume;
        const float right = std::sin(angle) * track.volume;
        masterBuffer_.addFrom(0, 0, trackScratch_, 0, 0,
                              info.numSamples, left);
        masterBuffer_.addFrom(1, 0, trackScratch_, 1, 0,
                              info.numSamples, right);
    }
}

// Stop/pause/seek panic. This deliberately walks EVERY instrument track,
// not just armed ones: a plugin on an unarmed track never rendered a panic
// while the transport was stopped, so its voices survived until the next
// play and were then heard as notes that are not in the current clip.
void ProjectAudioSource::panicAllInstrumentTracks(const Project& project,
                                                  int numSamples)
{
    if (numSamples <= 0)
        return;

    if (panicMidi_.isEmpty())
        addPanicMessages(panicMidi_);

    for (const auto& track : project.tracks)
    {
        if (track.type != Track::Type::Instrument)
            continue;

        auto* node = nodeFor(track.id);
        auto state = node != nullptr ? std::atomic_load(&node->state) : nullptr;
        const int prepared = state != nullptr ? state->instrumentPreparedBlock : 0;
        const bool hasInstrumentPlugin =
            state != nullptr && state->instrument != nullptr
            && ! state->instrumentBypass && prepared > 0;

        if (hasInstrumentPlugin)
        {
            const int n = std::min(numSamples, prepared);
            if (trackScratch_.getNumSamples() < numSamples)
                trackScratch_.setSize(2, numSamples, false, false, true);
            trackScratch_.clear();
            // A plugin may consume/modify the MIDI buffer, so give each one
            // its own copy; panicMidi_ stays pristine for the next track.
            blockMidi_.clear();
            for (const auto meta : panicMidi_)
                blockMidi_.addEvent(meta.getMessage(), 0);
            // Non-owning view: the plugin may have been prepared for a
            // smaller block than the device is asking for right now.
            juce::AudioBuffer<float> view(
                trackScratch_.getArrayOfWritePointers(), 2, n);
            state->instrument->processBlock(view, blockMidi_);
        }
        else
        {
            silenceSynthForTrack(track.id);
        }
    }
}

// Rebuilds fx/instrument plugin instances whose configuration changed.
// Must run on the message thread (plugin instantiation is not RT-safe);
// completed chains are swapped into the audio path under a short lock.
void ProjectAudioSource::reconcileNodes(const Project& project, PluginHost& host,
                                        int blockSize, bool deferHeavyPlugins)
{
    std::unique_lock<std::mutex> lock(trackNodesMutex_);

    // Plugins are prepared at the DEVICE rate, not the project rate: the
    // audio callback hands them device-rate buffers. If the project rate is
    // different we still need to fix that separately (resampling), but at
    // least the plugin contract matches the buffers it receives.
    const double deviceRate = deviceRate_.load(std::memory_order_acquire);
    const double pluginRate = deviceRate > 0.0 ? deviceRate : project.sampleRate;
    // Plugins get the device's real block size as their maximum contract;
    // the audio thread skips blocks larger than this instead of overrunning.
    const int maxBlock = juce::jlimit(256, 8192, blockSize > 0 ? blockSize : 512);

    // Stale queued jobs: drop any whose track/config no longer matches.
    pluginJobs_.erase(std::remove_if(pluginJobs_.begin(), pluginJobs_.end(),
        [&](const PluginJob& job) {
            for (const auto& track : project.tracks)
                if (track.id == job.trackId)
                    return job.sampleRate != pluginRate
                        || job.maxBlock != maxBlock
                        || track.instrument.pluginUid
                            != job.instrument.pluginUid
                        || track.fx.size() != job.fx.size()
                        || [&] {
                               for (int i = 0; i < (int) job.fx.size(); ++i)
                                   if (track.fx[(size_t) i].pluginUid
                                       != job.fx[(size_t) i].pluginUid)
                                       return false;
                               return true;
                           }();
            return true; // track gone
        }), pluginJobs_.end());

    // Build fresh chain data first (instantiation may fail per slot).
    struct ChainBuild {
        std::shared_ptr<const FxChain> fx;
        std::shared_ptr<juce::AudioProcessor> instrument;
        std::shared_ptr<juce::AudioProcessor> midiFx;
        juce::String midiFxUid;
        bool instrumentBypass = false;
        bool midiFxBypass = false;
        // Deferred load: keep the old snapshot audible until the job lands.
        std::shared_ptr<const TrackNodeState> keepState;
    };
    std::map<TrackId, ChainBuild> builds;

    for (const auto& track : project.tracks)
    {
        const bool hasPlugins = ! track.fx.empty()
            || ! track.instrument.pluginUid.isEmpty();
        auto existingIt = trackNodes_.find(track.id);
        std::shared_ptr<const TrackNodeState> oldState;
        if (existingIt != trackNodes_.end() && existingIt->second != nullptr)
            oldState = std::atomic_load(&existingIt->second->state);

        if (! hasPlugins && oldState == nullptr)
            continue;

        // The MIDI insert lives in the fx list (EZArpeggio). It is driven
        // from the instrument render path, never from the audio fx chain.
        juce::String desiredMidiFxUid;
        bool desiredMidiFxBypass = false;
        for (const auto& slot : track.fx)
            if (slot.pluginUid.containsIgnoreCase("ezarp"))
            {
                desiredMidiFxUid = slot.pluginUid;
                desiredMidiFxBypass = slot.bypass;
                break;
            }

        const bool chainSame = oldState != nullptr && oldState->fx != nullptr
            && oldState->fx->uids.size() == track.fx.size()
            && [&] {
                   for (int i = 0; i < oldState->fx->uids.size(); ++i)
                       if (oldState->fx->uids[i] != track.fx[(size_t) i].pluginUid)
                           return false;
                   return true;
               }()
            && oldState->fx->preparedRate == pluginRate
            && oldState->fx->preparedBlock == maxBlock;
        const bool instrumentSame = oldState != nullptr
            && oldState->instrumentUid == track.instrument.pluginUid
            && (oldState->instrument != nullptr)
                   == track.instrument.pluginUid.isNotEmpty()
            && (track.instrument.pluginUid.isEmpty()
                || (oldState->instrumentPreparedRate == pluginRate
                    && oldState->instrumentPreparedBlock == maxBlock));
        const bool midiFxSame = oldState != nullptr
            && oldState->midiFxUid == desiredMidiFxUid
            && (oldState->midiFx != nullptr) == desiredMidiFxUid.isNotEmpty()
            && (desiredMidiFxUid.isEmpty()
                || (oldState->midiFxPreparedRate == pluginRate
                    && oldState->midiFxPreparedBlock == maxBlock));

        if (chainSame && instrumentSame && midiFxSame)
        {
            // Same live instances: publish a new snapshot with the updated
            // bypass flags. Never mutate the published state in place.
            auto next = std::make_shared<TrackNodeState>();
            if (oldState->fx != nullptr)
            {
                auto updated = std::make_shared<FxChain>(*oldState->fx);
                for (int i = 0;
                     i < (int) updated->bypass.size()
                             && i < (int) track.fx.size();
                     ++i)
                    updated->bypass[(size_t) i] = track.fx[(size_t) i].bypass;
                next->fx = std::move(updated);
            }
            next->instrument = oldState->instrument;
            next->instrumentBypass = track.instrument.bypass;
            next->instrumentUid = oldState->instrumentUid;
            next->instrumentPreparedRate = oldState->instrumentPreparedRate;
            next->instrumentPreparedBlock = oldState->instrumentPreparedBlock;
            next->midiFx = oldState->midiFx;
            next->midiFxBypass = desiredMidiFxBypass;
            next->midiFxUid = oldState->midiFxUid;
            next->midiFxPreparedRate = oldState->midiFxPreparedRate;
            next->midiFxPreparedBlock = oldState->midiFxPreparedBlock;
            builds[track.id].keepState = std::move(next);
            continue;
        }

        // Deferred mode (project load / adding a heavy plugin): a track whose
        // plugins are not live yet keeps its previous sound (or stays dry if
        // it is new) and gets a queued job. Each message-loop pass loads ONE
        // track's plugins, keeping the UI responsive.
        if (deferHeavyPlugins
            || jobQueuedFor(track, pluginRate, maxBlock))
        {
            enqueuePluginJob(track, pluginRate, maxBlock);
            builds[track.id].keepState = oldState; // null = dry until job lands
            continue;
        }

        // Pool of LIVE instances reusable across this rebuild (same uid →
        // same object): adding/removing one fx must NOT re-instantiate the
        // others — fresh instances reset every preset to default.
        std::multimap<juce::String, std::shared_ptr<juce::AudioProcessor>>
            reusePool;
        std::shared_ptr<juce::AudioProcessor> reuseInstrument;
        std::shared_ptr<juce::AudioProcessor> reuseMidiFx;
        if (oldState != nullptr && oldState->fx != nullptr
            && oldState->fx->preparedRate == pluginRate
            && oldState->fx->preparedBlock == maxBlock)
        {
            for (size_t k = 0; k < oldState->fx->processors.size(); ++k)
                if (oldState->fx->processors[k] != nullptr
                    && ! oldState->fx->uids[k].containsIgnoreCase("ezarp"))
                    reusePool.emplace(oldState->fx->uids[k],
                                      oldState->fx->processors[k]);
        }
        if (oldState != nullptr && oldState->instrument != nullptr
            && oldState->instrumentUid == track.instrument.pluginUid
            && oldState->instrumentPreparedRate == pluginRate
            && oldState->instrumentPreparedBlock == maxBlock)
            reuseInstrument = oldState->instrument;
        if (oldState != nullptr && oldState->midiFx != nullptr
            && oldState->midiFxUid == desiredMidiFxUid
            && oldState->midiFxPreparedRate == pluginRate
            && oldState->midiFxPreparedBlock == maxBlock)
            reuseMidiFx = oldState->midiFx;

        ChainBuild build;
        build.instrumentBypass = track.instrument.bypass;
        build.midiFxBypass = desiredMidiFxBypass;

        auto fx = std::make_shared<FxChain>();
        fx->preparedRate = pluginRate;
        fx->preparedBlock = maxBlock;
        for (const auto& slot : track.fx)
        {
            std::shared_ptr<juce::AudioProcessor> proc;
            const bool isMidiInsert = slot.pluginUid.containsIgnoreCase("ezarp");
            if (isMidiInsert)
            {
                if (reuseMidiFx != nullptr)
                {
                    proc = std::move(reuseMidiFx);
                }
                else
                {
                    proc = host.create(slot.pluginUid, pluginRate, maxBlock);
                    if (proc)
                    {
                        proc->setPlayHead(this);
                        proc->setPlayConfigDetails(0, 0, pluginRate, maxBlock);
                        proc->prepareToPlay(pluginRate, maxBlock);
                    }
                    if (proc && slot.state.isNotEmpty())
                    {
                        juce::MemoryBlock mb;
                        if (mb.fromBase64Encoding(slot.state))
                            proc->setStateInformation(mb.getData(),
                                                      (int) mb.getSize());
                    }
                }
                build.midiFx = proc;
                build.midiFxUid = slot.pluginUid;
            }
            else
            {
                auto reuseIt = reusePool.find(slot.pluginUid);
                if (reuseIt != reusePool.end())
                {
                    proc = reuseIt->second; // same object: preset survives
                    reusePool.erase(reuseIt);
                }
                else
                {
                    proc = host.create(slot.pluginUid, pluginRate, maxBlock);
                    if (proc)
                    {
                        // Plugins are prepared on the message thread: VST3
                        // prepareToPlay takes the MessageManagerLock and
                        // deadlocks the device if called from the audio thread.
                        proc->setPlayHead(this);
                        activateStereoBuses(*proc);
                        proc->setPlayConfigDetails(2, 2, pluginRate, maxBlock);
                        proc->prepareToPlay(pluginRate, maxBlock);
                    }
                    if (proc && slot.state.isNotEmpty())
                    {
                        juce::MemoryBlock mb;
                        if (mb.fromBase64Encoding(slot.state))
                            proc->setStateInformation(mb.getData(),
                                                      (int) mb.getSize());
                    }
                }
            }
            fx->processors.push_back(std::move(proc));
            fx->bypass.push_back(slot.bypass);
            fx->uids.add(slot.pluginUid);
            fx->valid.push_back(fx->processors.back() != nullptr);
            fx->preparedRates.push_back(pluginRate);
        }
        build.fx = std::move(fx);

        if (! track.instrument.pluginUid.isEmpty())
        {
            if (reuseInstrument != nullptr)
            {
                build.instrument = std::move(reuseInstrument);
            }
            else
            {
                build.instrument = host.create(track.instrument.pluginUid,
                                               pluginRate, maxBlock);
                if (build.instrument)
                {
                    // Instruments have no input bus: use the plugin's own
                    // layout — forcing 2 inputs leaves VST3s unactivated.
                    build.instrument->setPlayHead(this);
                    activateStereoBuses(*build.instrument);
                    build.instrument->setPlayConfigDetails(
                        build.instrument->getMainBusNumInputChannels(),
                        build.instrument->getMainBusNumOutputChannels(),
                        pluginRate, maxBlock);
                    build.instrument->prepareToPlay(pluginRate, maxBlock);
                }
                if (build.instrument
                    && track.instrument.state.isNotEmpty())
                {
                    juce::MemoryBlock mb;
                    if (mb.fromBase64Encoding(track.instrument.state))
                        build.instrument->setStateInformation(
                            mb.getData(), (int) mb.getSize());
                }
            }
        }
        builds[track.id] = std::move(build);
    }

    // Apply: publish immutable snapshots. The audio thread loads them at the
    // start of a block; old snapshots stay alive until it drops its reference.
    for (const auto& track : project.tracks)
    {
        auto it = builds.find(track.id);
        if (it == builds.end())
            continue;
        auto& node = ensureNode(track.id);
        if (it->second.keepState != nullptr)
        {
            std::atomic_store(&node.state, it->second.keepState);
            continue;
        }
        auto next = std::make_shared<TrackNodeState>();
        next->fx = it->second.fx;
        next->instrument = it->second.instrument;
        next->instrumentBypass = it->second.instrumentBypass;
        next->instrumentUid = track.instrument.pluginUid;
        next->instrumentPreparedRate = next->instrument ? pluginRate : 0.0;
        next->instrumentPreparedBlock = next->instrument ? maxBlock : 0;
        next->midiFx = it->second.midiFx;
        next->midiFxBypass = it->second.midiFxBypass;
        next->midiFxUid = it->second.midiFxUid;
        next->midiFxPreparedRate = next->midiFx ? pluginRate : 0.0;
        next->midiFxPreparedBlock = next->midiFx ? maxBlock : 0;
        std::atomic_store(&node.state,
                          std::shared_ptr<const TrackNodeState>(std::move(next)));
    }

    // Removed tracks: publish an empty snapshot. NEVER erase the node: the
    // audio thread may still hold a raw pointer to it. This releases the
    // plugins as soon as the audio thread drops its last snapshot.
    {
        const juce::SpinLock::ScopedLockType structLock(synthsLock_);
        for (auto& [id, nodePtr] : trackNodes_)
        {
            if (nodePtr == nullptr)
                continue;
            const bool live = [&] {
                for (const auto& track : project.tracks)
                    if (track.id == id)
                        return true;
                return false;
            }();
            if (! live)
                std::atomic_store(&nodePtr->state,
                                  std::shared_ptr<const TrackNodeState>());
        }
    }

    // Master inserts: same immutable-snapshot rules as track chains. The old
    // code declared masterFxChain_ but never built it, so the mixer's master
    // inserts were silently bypassed.
    {
        auto oldMaster = std::atomic_load(&masterFxChain_);
        const bool same = oldMaster != nullptr
            && oldMaster->uids.size() == (int) project.masterFx.size()
            && [&] {
                   for (int i = 0; i < oldMaster->uids.size(); ++i)
                       if (oldMaster->uids[i]
                           != project.masterFx[(size_t) i].pluginUid)
                           return false;
                   return true;
               }()
            && oldMaster->preparedRate == pluginRate
            && oldMaster->preparedBlock == maxBlock;
        if (same)
        {
            auto updated = std::make_shared<FxChain>(*oldMaster);
            for (int i = 0;
                 i < (int) updated->bypass.size()
                         && i < (int) project.masterFx.size();
                 ++i)
                updated->bypass[(size_t) i] =
                    project.masterFx[(size_t) i].bypass;
            std::atomic_store(&masterFxChain_,
                              std::shared_ptr<const FxChain>(std::move(updated)));
        }
        else
        {
            std::multimap<juce::String, std::shared_ptr<juce::AudioProcessor>>
                reusePool;
            if (oldMaster != nullptr
                && oldMaster->preparedRate == pluginRate
                && oldMaster->preparedBlock == maxBlock)
                for (size_t k = 0; k < oldMaster->processors.size(); ++k)
                    if (oldMaster->processors[k] != nullptr)
                        reusePool.emplace(oldMaster->uids[(int) k],
                                          oldMaster->processors[k]);

            auto fx = std::make_shared<FxChain>();
            fx->preparedRate = pluginRate;
            fx->preparedBlock = maxBlock;
            for (const auto& slot : project.masterFx)
            {
                std::shared_ptr<juce::AudioProcessor> proc;
                auto reuseIt = reusePool.find(slot.pluginUid);
                if (reuseIt != reusePool.end())
                {
                    proc = reuseIt->second;
                    reusePool.erase(reuseIt);
                }
                else
                {
                    proc = host.create(slot.pluginUid, pluginRate, maxBlock);
                    if (proc)
                    {
                        proc->setPlayHead(this);
                        activateStereoBuses(*proc);
                        proc->setPlayConfigDetails(2, 2, pluginRate, maxBlock);
                        proc->prepareToPlay(pluginRate, maxBlock);
                    }
                    if (proc && slot.state.isNotEmpty())
                    {
                        juce::MemoryBlock mb;
                        if (mb.fromBase64Encoding(slot.state))
                            proc->setStateInformation(mb.getData(),
                                                      (int) mb.getSize());
                    }
                }
                fx->processors.push_back(std::move(proc));
                fx->bypass.push_back(slot.bypass);
                fx->uids.add(slot.pluginUid);
                fx->valid.push_back(fx->processors.back() != nullptr);
                fx->preparedRates.push_back(pluginRate);
            }
            std::atomic_store(&masterFxChain_,
                              std::shared_ptr<const FxChain>(std::move(fx)));
        }
    }

    // The fallback synth is prepared on the message thread, not on the
    // audio thread's first note.
    prepareInternalSynths(project, pluginRate);

    lock.unlock();
    if (pluginsChangedCallback_)
        pluginsChangedCallback_();

    // Kick off the deferred job pump: one plugin load per message-loop
    // pass, so Flutter keeps painting between loads.
    if (! pluginJobs_.empty() && ! pluginJobsPumping_)
    {
        pluginJobsPumping_ = true;
        juce::MessageManager::callAsync([this] { pumpOnePluginJob(); });
    }
}

void ProjectAudioSource::enqueuePluginJob(const Track& track, double rate,
                                          int maxBlock)
{
    if (jobQueuedFor(track, rate, maxBlock))
        return;
    pluginJobs_.push_back(
        { track.id, track.fx, track.instrument, rate, maxBlock });
}

bool ProjectAudioSource::jobQueuedFor(const Track& track, double rate,
                                      int maxBlock) const
{
    for (const auto& job : pluginJobs_)
    {
        if (job.trackId != track.id || job.sampleRate != rate
            || job.maxBlock != maxBlock)
            continue;
        if (job.instrument.pluginUid != track.instrument.pluginUid
            || job.fx.size() != track.fx.size())
            continue;
        bool same = true;
        for (int i = 0; i < (int) job.fx.size(); ++i)
            if (job.fx[(size_t) i].pluginUid
                != track.fx[(size_t) i].pluginUid)
            {
                same = false;
                break;
            }
        if (same)
            return true;
    }
    return false;
}

// Message thread: instantiate ONE queued track's plugins and swap them into
// its node. The audio thread picks the new chain up on the next block.
void ProjectAudioSource::pumpOnePluginJob()
{
    if (pluginJobs_.empty())
    {
        pluginJobsPumping_ = false;
        return;
    }
    const auto job = pluginJobs_.front();
    pluginJobs_.erase(pluginJobs_.begin());
    applyPluginJob(job, *pluginHost_);
    if (! pluginJobs_.empty())
        juce::MessageManager::callAsync([this] { pumpOnePluginJob(); });
    else
        pluginJobsPumping_ = false;
}

bool ProjectAudioSource::hasPendingPluginJobs() const
{
    return ! pluginJobs_.empty();
}

void ProjectAudioSource::applyPluginJob(const PluginJob& job,
                                        PluginHost& host)
{
    std::unique_lock<std::mutex> lock(trackNodesMutex_);
    auto& node = ensureNode(job.trackId);
    auto oldState = std::atomic_load(&node.state);

    // Reuse live instances ONLY when they were prepared for the same device
    // configuration. Re-preparing a live instance would call prepareToPlay()
    // while the audio thread is inside processBlock() — exactly the kind of
    // race that crashed Analog Lab V. A new instance is safer.
    std::multimap<juce::String, std::shared_ptr<juce::AudioProcessor>> reusePool;
    std::shared_ptr<juce::AudioProcessor> reuseInstrument;
    std::shared_ptr<juce::AudioProcessor> reuseMidiFx;
    if (oldState != nullptr && oldState->fx != nullptr
        && oldState->fx->preparedRate == job.sampleRate
        && oldState->fx->preparedBlock == job.maxBlock)
    {
        for (size_t k = 0; k < oldState->fx->processors.size(); ++k)
            if (oldState->fx->processors[k] != nullptr
                && ! oldState->fx->uids[k].containsIgnoreCase("ezarp"))
                reusePool.emplace(oldState->fx->uids[k],
                                  oldState->fx->processors[k]);
    }
    if (oldState != nullptr && oldState->instrument != nullptr
        && oldState->instrumentUid == job.instrument.pluginUid
        && oldState->instrumentPreparedRate == job.sampleRate
        && oldState->instrumentPreparedBlock == job.maxBlock)
        reuseInstrument = oldState->instrument;
    if (oldState != nullptr && oldState->midiFx != nullptr
        && oldState->midiFxPreparedRate == job.sampleRate
        && oldState->midiFxPreparedBlock == job.maxBlock)
        reuseMidiFx = oldState->midiFx;

    auto fx = std::make_shared<FxChain>();
    fx->preparedRate = job.sampleRate;
    fx->preparedBlock = job.maxBlock;
    std::shared_ptr<juce::AudioProcessor> midiFx;
    juce::String midiFxUid;
    bool midiFxBypass = false;

    for (const auto& slot : job.fx)
    {
        std::shared_ptr<juce::AudioProcessor> proc;
        const bool isMidiInsert = slot.pluginUid.containsIgnoreCase("ezarp");
        if (isMidiInsert)
        {
            if (reuseMidiFx != nullptr)
            {
                proc = std::move(reuseMidiFx);
            }
            else
            {
                proc = host.create(slot.pluginUid, job.sampleRate, job.maxBlock);
                if (proc)
                {
                    proc->setPlayHead(this);
                    proc->setPlayConfigDetails(0, 0, job.sampleRate,
                                               job.maxBlock);
                    proc->prepareToPlay(job.sampleRate, job.maxBlock);
                }
                if (proc && slot.state.isNotEmpty())
                {
                    juce::MemoryBlock mb;
                    if (mb.fromBase64Encoding(slot.state))
                        proc->setStateInformation(mb.getData(),
                                                  (int) mb.getSize());
                }
            }
            midiFx = proc;
            midiFxUid = slot.pluginUid;
            midiFxBypass = slot.bypass;
        }
        else
        {
            auto reuseIt = reusePool.find(slot.pluginUid);
            if (reuseIt != reusePool.end())
            {
                proc = reuseIt->second; // same object: preset survives
                reusePool.erase(reuseIt);
            }
            else
            {
                proc = host.create(slot.pluginUid, job.sampleRate, job.maxBlock);
                if (proc)
                {
                    proc->setPlayHead(this);
                    activateStereoBuses(*proc);
                    proc->setPlayConfigDetails(2, 2, job.sampleRate,
                                               job.maxBlock);
                    proc->prepareToPlay(job.sampleRate, job.maxBlock);
                }
                if (proc && slot.state.isNotEmpty())
                {
                    juce::MemoryBlock mb;
                    if (mb.fromBase64Encoding(slot.state))
                        proc->setStateInformation(mb.getData(),
                                                  (int) mb.getSize());
                }
            }
        }
        fx->processors.push_back(std::move(proc));
        fx->bypass.push_back(slot.bypass);
        fx->uids.add(slot.pluginUid);
        fx->valid.push_back(fx->processors.back() != nullptr);
        fx->preparedRates.push_back(job.sampleRate);
    }

    std::shared_ptr<juce::AudioProcessor> instrument;
    if (! job.instrument.pluginUid.isEmpty())
    {
        if (reuseInstrument != nullptr)
        {
            instrument = std::move(reuseInstrument); // same object: patch survives
        }
        else
        {
            instrument = host.create(job.instrument.pluginUid, job.sampleRate,
                                     job.maxBlock);
            if (instrument)
            {
                instrument->setPlayHead(this);
                activateStereoBuses(*instrument);
                instrument->setPlayConfigDetails(
                    instrument->getMainBusNumInputChannels(),
                    instrument->getMainBusNumOutputChannels(),
                    job.sampleRate, job.maxBlock);
                instrument->prepareToPlay(job.sampleRate, job.maxBlock);
            }
            if (instrument && job.instrument.state.isNotEmpty())
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding(job.instrument.state))
                    instrument->setStateInformation(mb.getData(),
                                                    (int) mb.getSize());
            }
        }
    }

    auto next = std::make_shared<TrackNodeState>();
    next->fx = std::move(fx);
    next->instrument = std::move(instrument);
    next->instrumentBypass = job.instrument.bypass;
    next->instrumentUid = job.instrument.pluginUid;
    next->instrumentPreparedRate = next->instrument ? job.sampleRate : 0.0;
    next->instrumentPreparedBlock = next->instrument ? job.maxBlock : 0;
    next->midiFx = std::move(midiFx);
    next->midiFxBypass = midiFxBypass;
    next->midiFxUid = std::move(midiFxUid);
    next->midiFxPreparedRate = next->midiFx ? job.sampleRate : 0.0;
    next->midiFxPreparedBlock = next->midiFx ? job.maxBlock : 0;
    std::atomic_store(&node.state,
                      std::shared_ptr<const TrackNodeState>(std::move(next)));

    lock.unlock();
    if (pluginsChangedCallback_)
        pluginsChangedCallback_();
}

TrackAudioNode* ProjectAudioSource::nodeFor(TrackId trackId) const
{
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    auto it = trackNodes_.find(trackId);
    return it != trackNodes_.end() ? it->second.get() : nullptr;
}

// Plugin-parameter automation (§7.12 v1.5): evaluates every param lane of
// the track at the block position and pushes the value into the target
// processor. Audio-thread safe: JUCE VST3 parameter changes are queued.
void ProjectAudioSource::applyParamAutomation(const Track& track,
                                              const TrackNodeState* state,
                                              std::int64_t blockStart,
                                              double rate)
{
    if (track.paramLanes.empty() || state == nullptr)
        return;

    for (const auto& lane : track.paramLanes)
    {
        std::shared_ptr<juce::AudioProcessor> proc;
        if (lane.slotKind == 1)
            proc = state->instrument;
        else if (state->fx != nullptr
                 && lane.slotIndex < (int) state->fx->processors.size())
            proc = state->fx->processors[(size_t) lane.slotIndex];
        if (proc == nullptr)
            continue;
        proc->setParameter(lane.paramIndex,
                           evalCurveAt(lane.curve.get(), blockStart, rate,
                                       0.0f));
    }
}

std::shared_ptr<juce::AudioProcessor> ProjectAudioSource::fxProcessorFor(
    TrackId trackId, int slotIndex)
{
    auto* node = nodeFor(trackId);
    if (node == nullptr)
        return nullptr;
    auto state = std::atomic_load(&node->state);
    if (state == nullptr || state->fx == nullptr || slotIndex < 0
        || slotIndex >= (int) state->fx->processors.size())
        return nullptr;
    return state->fx->processors[(size_t) slotIndex];
}

std::shared_ptr<juce::AudioProcessor> ProjectAudioSource::instrumentProcessorFor(
    TrackId trackId)
{
    auto* node = nodeFor(trackId);
    if (node == nullptr)
        return nullptr;
    auto state = std::atomic_load(&node->state);
    return state != nullptr ? state->instrument : nullptr;
}

std::shared_ptr<juce::AudioProcessor> ProjectAudioSource::midiFxProcessorFor(
    TrackId trackId)
{
    auto* node = nodeFor(trackId);
    if (node == nullptr)
        return nullptr;
    auto state = std::atomic_load(&node->state);
    return state != nullptr ? state->midiFx : nullptr;
}

std::shared_ptr<juce::AudioProcessor> ProjectAudioSource::masterFxProcessorFor(
    int slotIndex)
{
    auto chain = std::atomic_load(&masterFxChain_);
    if (chain == nullptr || slotIndex < 0
        || slotIndex >= (int) chain->processors.size())
        return nullptr;
    return chain->processors[(size_t) slotIndex];
}

std::vector<juce::AudioProcessor*> ProjectAudioSource::liveProcessors() const
{
    const std::scoped_lock lock(trackNodesMutex_);
    std::vector<juce::AudioProcessor*> out;
    for (const auto& entry : trackNodes_)
    {
        const auto& node = entry.second;
        if (node == nullptr)
            continue;
        auto state = std::atomic_load(&node->state);
        if (state == nullptr)
            continue;
        if (state->instrument != nullptr)
            out.push_back(state->instrument.get());
        if (state->midiFx != nullptr)
            out.push_back(state->midiFx.get());
        if (state->fx != nullptr)
            for (const auto& proc : state->fx->processors)
                if (proc != nullptr)
                    out.push_back(proc.get());
    }
    if (auto master = std::atomic_load(&masterFxChain_))
        for (const auto& proc : master->processors)
            if (proc != nullptr)
                out.push_back(proc.get());
    return out;
}

void ProjectAudioSource::processFxChain(const FxChain& chain,
                                        juce::AudioBuffer<float>& buffer,
                                        double rate, int numSamples)
{
    // A block larger than the prepared maximum would overrun the plugins'
    // internal buffers — skip the block rather than process garbage.
    if (chain.preparedBlock <= 0 || numSamples > chain.preparedBlock)
        return;

    for (size_t i = 0; i < chain.processors.size(); ++i)
    {
        if (i < chain.bypass.size() && chain.bypass[i])
            continue;
        auto* proc = chain.processors[i].get();
        if (proc == nullptr)
            continue;
        // MIDI inserts (EZArpeggio) are driven from the instrument render
        // path with the track's real MIDI stream — never here (audio-only).
        if (i < (size_t) chain.uids.size()
            && chain.uids[(int) i].containsIgnoreCase("ezarp"))
            continue;
        if (i >= chain.preparedRates.size()
            || chain.preparedRates[i] != rate)
            continue;
        fxEmptyMidi_.clear();
        proc->processBlock(buffer, fxEmptyMidi_);
    }
}

void ProjectAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    if (info.buffer == nullptr)
        return;

    // Live MIDI monitoring: empty the input queue so the notes played reach
    // the armed instrument tracks' synths. Offline export has no live input.
    if (! offlineRender_.load(std::memory_order_acquire))
        drainExternalMidi();

    const auto project = std::atomic_load(&project_);
    // A queued panic (stop/pause/seek) sends a full MIDI panic to every
    // instrument track while audio runs — plugins (SINE Player, ZENOLOGY,
    // LABS) otherwise keep a stuck voice sounding forever. The generation
    // counter is read (not exchanged) so a request made while a block is
    // already running is guaranteed to be seen by the next block.
    const auto panicGen = panicGeneration_.load(std::memory_order_acquire);
    const bool injectPanic = panicGen != lastPanicGeneration_;
    lastPanicGeneration_ = panicGen;

    if (project == nullptr)
    {
        info.clearActiveBufferRegion();
        for (auto& peak : masterPeaks_)
            peak.store(0, std::memory_order_relaxed);
        return;
    }

    const int numSamples = info.numSamples;
    if (numSamples <= 0)
        return;

    // The master bus is always two channels. Track scratch, the MIDI insert
    // buffer and this bus are preallocated in prepareToPlay(); the resize is
    // only a safety net for a device that violates its own contract.
    if (masterBuffer_.getNumSamples() < numSamples)
        masterBuffer_.setSize(2, numSamples, false, false, true);
    masterBuffer_.clear();

    const double rate = project->sampleRate;
    const double deviceRate = deviceRate_.load(std::memory_order_acquire);
    const double pluginRate = deviceRate > 0.0 ? deviceRate : rate;

    // Master inserts + master fader + meters + output copy. Master gain is
    // applied HERE, after the inserts, not per track: that is what makes the
    // master fader a true master fader and keeps track meters independent of
    // it.
    auto finishMaster = [&] {
        if (auto masterFx = std::atomic_load(&masterFxChain_))
            processFxChain(*masterFx, masterBuffer_, pluginRate, numSamples);
        masterBuffer_.applyGain(project->masterGain);

        const int numOut = std::min(2, info.buffer->getNumChannels());
        for (int ch = 0; ch < numOut; ++ch)
        {
            masterPeaks_[ch].store(
                std::bit_cast<std::uint32_t>(
                    absMax(masterBuffer_.getReadPointer(ch, 0), numSamples)),
                std::memory_order_relaxed);
            info.buffer->copyFrom(ch, info.startSample, masterBuffer_, ch, 0,
                                  numSamples);
        }
        if (numOut < 2)
            masterPeaks_[1].store(0, std::memory_order_relaxed);
    };

    if (! playing_.load())
    {
        info.clearActiveBufferRegion();
        if (injectPanic)
        {
            // Drop live notes queued before the stop; a key held after
            // the stop arrives in a later block and still monitors.
            pendingExternal_.clear();
            lastSustainCc_.fill(0);
            panicAllInstrumentTracks(*project, numSamples);
        }
        renderArmedInstrumentMonitoring(info, *project);
        finishMaster();
        return;
    }

    const std::int64_t blockStart = positionSamples_.fetch_add(numSamples);
    const std::int64_t endSamples = (std::int64_t) (project->durationSeconds() * rate);

    // While recording, keep the transport rolling past the project end so
    // the capture is not cut short by the auto-stop.
    if (blockStart >= endSamples && ! recording_.load())
    {
        playing_.store(false);
        // The project end is an implicit stop: notes that were still held
        // must be released, not left frozen until the next play.
        requestPanic();
        info.clearActiveBufferRegion();
        for (auto& peak : masterPeaks_)
            peak.store(0, std::memory_order_relaxed);
        return;
    }

    info.clearActiveBufferRegion();

    if (injectPanic)
    {
        pendingExternal_.clear();
        lastSustainCc_.fill(0);
        // Internal synths ignore CC64 through Synthesiser::renderNextBlock,
        // so a pedal-held voice would survive the MIDI panic. Hard-stop them
        // here; plugins get the full panic in their MIDI buffer below.
        for (const auto& track : project->tracks)
            if (track.type == Track::Type::Instrument)
                silenceSynthForTrack(track.id);
    }

    bool anySolo = false;
    for (const auto& track : project->tracks)
        anySolo = anySolo || track.solo;

    if (trackScratch_.getNumSamples() < numSamples)
        trackScratch_.setSize(2, numSamples, false, false, true);

    int trackIndex = 0;
    for (const auto& track : project->tracks)
    {
        const int meterSlot = trackIndex++ & (kMeterTracks - 1);
        if (track.mute || (anySolo && ! track.solo))
        {
            trackPeaks_[meterSlot].store(0, std::memory_order_relaxed);
            continue;
        }

        float trackPeak = 0.0f;

        // Load ONE immutable snapshot per track per block. The audio thread
        // never locks or reads a mutable plugin field again: reconcile and
        // deferred jobs publish a new snapshot and leave this one alive until
        // we drop it at the end of the block.
        auto* node = nodeFor(track.id);
        auto state = node != nullptr ? std::atomic_load(&node->state) : nullptr;

        // Instrument tracks: render their MIDI clips through the internal
        // synth or through the loaded instrument plugin.
        if (track.type == Track::Type::Instrument)
        {
            blockMidi_.clear();
            if (injectPanic)
            {
                // Sustain-off + explicit note-offs + CC120/CC123. CC123
                // alone is ignored by several hosted instruments.
                addPanicMessages(blockMidi_);
            }
            if (track.armed)
                for (const auto& m : pendingExternal_)
                    blockMidi_.addEvent(m, 0);
            for (const auto& clip : track.clips)
            {
                if (clip.muted || ! clip.isMidi() || clip.midi == nullptr)
                    continue;
                const std::int64_t clipStartSamples = (std::int64_t) (clip.startSeconds * rate);
                // Trimming a MIDI clip cuts playback: notes starting beyond
                // the visible clip end stay silent (Cubase semantics).
                const std::int64_t clipEndSamples = clipStartSamples
                    + (std::int64_t) (clip.lengthSeconds * rate);
                for (const auto& note : clip.midi->notes)
                {
                    const std::int64_t on = clipStartSamples
                        + (std::int64_t) (note.startSeconds * rate);
                    const std::int64_t off = on + (std::int64_t) (note.lengthSeconds * rate);
                    // Full scan, no break: recorded notes are paired in
                    // note-OFF order, so the array is NOT sorted by start.
                    if (on >= blockStart + numSamples)
                        continue;
                    if (on >= clipEndSamples)
                        continue;
                    // Notes fully hidden behind a LEFT trim stay silent.
                    if (off <= clipStartSamples)
                        continue;
                    if (off < blockStart)
                        continue; // ended earlier; its noteOff went out in that block

                    if (on >= blockStart)
                    {
                        blockMidi_.addEvent(
                            juce::MidiMessage::noteOn(1, note.pitch, note.velocity),
                            (int) (on - blockStart));
                    }

                    // Only send noteOff when it lands inside THIS block — a
                    // clamped one here would cut every note to one block long.
                    if (off < blockStart + numSamples)
                        blockMidi_.addEvent(
                            juce::MidiMessage::noteOff(1, note.pitch),
                            (int) (off - blockStart));
                }
            }

            applyParamAutomation(track, state.get(), blockStart, rate);
            const bool hasInstrumentPlugin =
                state != nullptr && state->instrument != nullptr
                && ! state->instrumentBypass
                && state->instrumentPreparedRate == pluginRate
                && numSamples <= state->instrumentPreparedBlock;

            // Sustain pedal (CC64) lane → MIDI CC events at the block head.
            // STEP semantics: on/off, never a fade between points.
            if (auto* susCurve = track.sustainAutomation.get())
            {
                const int cc = (int) std::lround(
                    evalStepAt(susCurve, blockStart, rate) * 127.0f);
                int& last = lastSustainCc_[meterSlot];
                if (cc != last)
                {
                    blockMidi_.addEvent(
                        juce::MidiMessage::controllerEvent(1, 64, cc), 0);
                    last = cc;
                }
            }
            if (debugMidi_ && blockMidi_.getNumEvents() > 0)
                std::fprintf(stderr,
                             "[midi-dbg] node=%p inst=%p bypass=%d "
                             "preparedBlock=%d numSamples=%d events=%d\n",
                             (void*) node,
                             state != nullptr
                                 ? (void*) state->instrument.get()
                                 : nullptr,
                             state != nullptr
                                 ? (int) state->instrumentBypass
                                 : -1,
                             state != nullptr
                                 ? state->instrumentPreparedBlock
                                 : -1,
                             numSamples, blockMidi_.getNumEvents());

            // MIDI insert (arpeggiator): the instrument hears the
            // TRANSFORMED stream — the original clip notes are consumed.
            const bool midiFxActive =
                state != nullptr && state->midiFx != nullptr
                && ! state->midiFxBypass
                && state->midiFxPreparedRate == pluginRate
                && state->midiFxPreparedBlock > 0
                && numSamples <= state->midiFxPreparedBlock;
            if (debugArp_)
                std::fprintf(stderr,
                             "[arp-dbg] node=%p mfx=%p prepared=%d "
                             "bypass=%d numSamples=%d events=%d\n",
                             (void*) node,
                             state != nullptr
                                 ? (void*) state->midiFx.get()
                                 : nullptr,
                             state != nullptr
                                 ? state->midiFxPreparedBlock
                                 : -1,
                             state != nullptr
                                 ? (int) state->midiFxBypass
                                 : -1,
                             numSamples, blockMidi_.getNumEvents());
            if (midiFxActive)
            {
                if (midiFxSilent_.getNumSamples() < numSamples)
                    midiFxSilent_.setSize(1, numSamples, false, false, true);
                midiFxSilent_.clear();
                state->midiFx->processBlock(midiFxSilent_, blockMidi_);
            }

            if (hasInstrumentPlugin)
            {
                trackScratch_.clear();
                state->instrument->processBlock(trackScratch_, blockMidi_);

                // MIDI generators (EZDrums): whatever the plugin
                // "performed" this block is written into the armed
                // track's MIDI take while recording — the machine records
                // itself into a normal clip, exactly like a player would.
                if (recording_.load(std::memory_order_relaxed) && track.armed)
                {
                    const bool ezdrumsGen = dynamic_cast<ezdrums::MidiGenerator*>(
                                                state->instrument.get())
                                            != nullptr;
                    if (ezdrumsGen)
                    {
                        auto* gen = dynamic_cast<ezdrums::MidiGenerator*>(
                            state->instrument.get());
                        juce::MidiBuffer performed;
                        gen->drainGeneratedMidi(performed);
                        if (performed.getNumEvents() > 0)
                        {
                            const std::int64_t rel0 =
                                blockStart
                                - recordStartSamples_.load(
                                    std::memory_order_relaxed);
                            if (auto caps = std::atomic_load(&captureList_))
                                for (auto& capture : *caps)
                                {
                                    if (! capture->isMidi
                                        || capture->trackId != track.id)
                                        continue;
                                    // Never block the audio thread on the
                                    // message thread finalizing a take.
                                    std::unique_lock<std::mutex> lock(
                                        capture->midiMutex,
                                        std::try_to_lock);
                                    if (! lock.owns_lock())
                                        continue;
                                    for (const auto ev : performed)
                                    {
                                        const auto& m = ev.getMessage();
                                        const std::int64_t rel =
                                            rel0 + ev.samplePosition;
                                        if (rel < 0)
                                            continue;
                                        const auto* raw = m.getRawData();
                                        if (raw == nullptr
                                            || m.getRawDataSize() < 1)
                                            continue;
                                        capture->midiEvents.push_back(
                                            { (int) std::min<std::int64_t>(
                                                  rel, INT32_MAX),
                                              raw[0],
                                              m.getRawDataSize() > 1
                                                  ? raw[1]
                                                  : (std::uint8_t) 0,
                                              m.getRawDataSize() > 2
                                                  ? raw[2]
                                                  : (std::uint8_t) 0 });
                                    }
                                }
                        }
                    }
                    else if (midiFxActive)
                    {
                        // The arpeggiator REWRITES the part: capture the
                        // transformed stream so the take holds the new
                        // pattern, not the original clip notes.
                        if (auto caps = std::atomic_load(&captureList_))
                            for (auto& capture : *caps)
                            {
                                if (! capture->isMidi
                                    || capture->trackId != track.id)
                                    continue;
                                const std::int64_t rel0 =
                                    blockStart
                                    - recordStartSamples_.load(
                                        std::memory_order_relaxed);
                                std::unique_lock<std::mutex> lock(
                                    capture->midiMutex,
                                    std::try_to_lock);
                                if (! lock.owns_lock())
                                    continue;
                                for (const auto ev : blockMidi_)
                                {
                                    const auto& m = ev.getMessage();
                                    const std::int64_t rel =
                                        rel0 + ev.samplePosition;
                                    if (rel < 0)
                                        continue;
                                    const auto* raw = m.getRawData();
                                    if (raw == nullptr
                                        || m.getRawDataSize() < 1)
                                        continue;
                                    capture->midiEvents.push_back(
                                        { (int) std::min<std::int64_t>(
                                              rel, INT32_MAX),
                                          raw[0],
                                          m.getRawDataSize() > 1
                                              ? raw[1]
                                              : (std::uint8_t) 0,
                                          m.getRawDataSize() > 2
                                              ? raw[2]
                                              : (std::uint8_t) 0 });
                                }
                            }
                    }
                }
            }
            else
            {
                auto* synth = getOrCreateSynth(track.id, pluginRate);
                // Synthesiser ignores CC messages: route the sustain pedal
                // to the voices ourselves (PianoVoice implements sustain).
                for (const auto ev : blockMidi_)
                {
                    const auto& m = ev.getMessage();
                    if (m.isController() && m.getControllerNumber() == 64)
                        for (int v = 0; v < synth->getNumVoices(); ++v)
                            if (auto* voice = synth->getVoice(v))
                                voice->controllerMoved(64,
                                                       m.getControllerValue());
                }
                trackScratch_.clear();
                synth->renderNextBlock(trackScratch_, blockMidi_, 0, numSamples);
            }

            if (state != nullptr && state->fx != nullptr)
                processFxChain(*state->fx, trackScratch_, pluginRate, numSamples);

            // Volume + pan automation (§7.12, Read always active): gain
            // ramps across the block from the curves, replacing the static
            // volume/pan. Master gain is applied later, on the master bus.
            const auto* volCurve = track.volumeAutomation.get();
            const auto* panCurve = track.panAutomation.get();
            const float v0 = volumeAt(track, volCurve, blockStart, rate);
            const float v1 = volumeAt(track, volCurve,
                                      blockStart + numSamples, rate);
            const float pan0 = panCurve
                ? evalCurveAt(panCurve, blockStart, rate, 0.5f) * 2.0f - 1.0f
                : track.pan;
            const float pan1 = panCurve
                ? evalCurveAt(panCurve, blockStart + numSamples, rate,
                              0.5f) * 2.0f - 1.0f
                : track.pan;

            const float ang0 = (juce::jlimit(-1.0f, 1.0f, pan0) + 1.0f)
                               * juce::MathConstants<float>::pi * 0.25f;
            const float ang1 = (juce::jlimit(-1.0f, 1.0f, pan1) + 1.0f)
                               * juce::MathConstants<float>::pi * 0.25f;
            const float left0 = std::cos(ang0) * v0;
            const float left1 = std::cos(ang1) * v1;
            const float right0 = std::sin(ang0) * v0;
            const float right1 = std::sin(ang1) * v1;
            trackScratch_.applyGainRamp(0, 0, numSamples, left0, left1);
            trackScratch_.applyGainRamp(1, 0, numSamples, right0, right1);

            for (int ch = 0; ch < 2; ++ch)
                trackPeak = std::max(trackPeak,
                    absMax(trackScratch_.getReadPointer(ch, 0), numSamples));

            masterBuffer_.addFrom(0, 0, trackScratch_, 0, 0, numSamples, 1.0f);
            masterBuffer_.addFrom(1, 0, trackScratch_, 1, 0, numSamples, 1.0f);

            trackPeaks_[meterSlot].store(std::bit_cast<std::uint32_t>(trackPeak),
                                         std::memory_order_relaxed);
            continue;
        }

        // Audio tracks: accumulate clips into scratch, then fx, then pan.
        // MUST clear first: the scratch is shared across tracks, and stale
        // samples from the previous track would be reverb-fed and re-mixed
        // (audible artifacts, the Raum report).
        trackScratch_.clear();
        for (const auto& clip : track.clips)
        {
            if (clip.muted)
                continue;

            const auto srcIt = project->sources.find(clip.sourceId);
            if (srcIt == project->sources.end() || srcIt->second == nullptr)
                continue;
            const auto& source = srcIt->second->samples;

            const std::int64_t clipStart = (std::int64_t) (clip.startSeconds * rate);
            const std::int64_t clipEnd = (std::int64_t) (clip.endSeconds() * rate);
            const std::int64_t from = std::max(blockStart, clipStart);
            const std::int64_t to = std::min(blockStart + (std::int64_t) numSamples, clipEnd);
            if (to <= from)
                continue;

            std::int64_t srcStart = (std::int64_t) (clip.sourceOffsetSeconds * rate) + (from - clipStart);
            int count = (int) (to - from);
            if (srcStart >= source.getNumSamples())
                continue;
            count = (int) std::min<std::int64_t>(count, source.getNumSamples() - srcStart);
            if (count <= 0)
                continue;

            const int dstOffset = (int) (from - blockStart);

            for (int ch = 0; ch < 2; ++ch)
            {
                const int srcCh = std::min(ch, source.getNumChannels() - 1);
                juce::FloatVectorOperations::copyWithMultiply(
                    trackScratch_.getWritePointer(ch, dstOffset),
                    source.getReadPointer(srcCh, (int) srcStart),
                    clip.gain, count);
            }

            // Linear fade ramps (§7.3), multiplied over their sample ranges.
            auto applyFade = [&](double fadeSeconds, bool fadeIn) {
                if (fadeSeconds <= 0.0)
                    return;
                const double fSamples = fadeSeconds * rate;
                if (fSamples <= 0.0)
                    return;
                const double regionStart = fadeIn
                    ? (double) clipStart
                    : (double) clipStart + clip.lengthSeconds * rate - fSamples;
                const double regionEnd = regionStart + fSamples;
                const double s = std::max<double>((double) from, regionStart);
                const double e = std::min<double>((double) to, regionEnd);
                if (e <= s)
                    return;
                const int rs = (int) (s - from);
                const int re = (int) (e - from);
                const double g0 = fadeIn ? (s - regionStart) / fSamples
                                         : (regionEnd - s) / fSamples;
                const double g1 = fadeIn ? (e - regionStart) / fSamples
                                         : (regionEnd - e) / fSamples;
                for (int ch = 0; ch < 2; ++ch)
                    trackScratch_.applyGainRamp(ch, dstOffset + rs, re - rs,
                                                (float) g0, (float) g1);
            };
            applyFade(clip.fadeInSeconds, true);
            applyFade(clip.fadeOutSeconds, false);
        }

        // Volume + pan automation ramps (§7.12, Read always active) —
        // applied before the meter so the meters show the automated level.
        const auto* volCurve = track.volumeAutomation.get();
        const auto* panCurve = track.panAutomation.get();
        const float v0 = volumeAt(track, volCurve, blockStart, rate);
        const float v1 = volumeAt(track, volCurve,
                                  blockStart + numSamples, rate);
        const float pan0 = panCurve
            ? evalCurveAt(panCurve, blockStart, rate, 0.5f) * 2.0f - 1.0f
            : track.pan;
        const float pan1 = panCurve
            ? evalCurveAt(panCurve, blockStart + numSamples, rate,
                          0.5f) * 2.0f - 1.0f
            : track.pan;

        applyParamAutomation(track, state.get(), blockStart, rate);
        if (state != nullptr && state->fx != nullptr)
            processFxChain(*state->fx, trackScratch_, pluginRate, numSamples);

        // Stereo sources pan as a balance (see panGains): a centred stereo
        // clip keeps its level, matching Cubase's stereo panner.
        const bool stereo = hasStereoSource(*project, track);
        float left0 = 0.0f, right0 = 0.0f, left1 = 0.0f, right1 = 0.0f;
        panGains(stereo, pan0, v0, left0, right0);
        panGains(stereo, pan1, v1, left1, right1);
        trackScratch_.applyGainRamp(0, 0, numSamples, left0, left1);
        trackScratch_.applyGainRamp(1, 0, numSamples, right0, right1);

        for (int ch = 0; ch < 2; ++ch)
            trackPeak = std::max(trackPeak,
                absMax(trackScratch_.getReadPointer(ch, 0), numSamples));

        masterBuffer_.addFrom(0, 0, trackScratch_, 0, 0, numSamples, 1.0f);
        masterBuffer_.addFrom(1, 0, trackScratch_, 1, 0, numSamples, 1.0f);

        trackPeaks_[meterSlot].store(std::bit_cast<std::uint32_t>(trackPeak),
                                     std::memory_order_relaxed);
    }

    // Metronome click: accent on the beat that starts a bar (4/4 fixed until
    // the tempo/time-signature editing lands). It goes through the master bus
    // so the master meter reflects what is actually leaving the engine.
    if (metronome_.load(std::memory_order_relaxed))
    {
        const double beatSamples = 60.0 / project->tempoBpm * rate;
        std::int64_t beat = (std::int64_t) std::ceil((double) blockStart / beatSamples);
        for (; beat * beatSamples < blockStart + numSamples; ++beat)
        {
            const std::int64_t clickPos = (std::int64_t) (beat * beatSamples) - blockStart;
            const int offset = (int) clickPos;
            const int clickLen = (int) std::min<std::int64_t>(
                (std::int64_t) (0.03 * rate), numSamples - offset);
            const bool accent = (beat % 4) == 0;
            const double freq = accent ? 1568.0 : 880.0;
            for (int i = 0; i < clickLen; ++i)
            {
                const float env = 1.0f - (float) i / (float) clickLen;
                const float sample = (float) (0.4 * env
                    * std::sin(juce::MathConstants<double>::twoPi * freq * i / rate));
                for (int ch = 0; ch < 2; ++ch)
                    masterBuffer_.addSample(ch, offset + i, sample);
            }
        }
    }

    finishMaster();
}

void ProjectAudioSource::allNotesOff()
{
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    for (auto& [id, synth] : synths_)
        silenceSynthVoices(*synth);
}

// AudioPlayHead: report the real transport to plugins. iZotope (Ozone)
// gates its metering/DSP on this — without a playhead the plugin thinks
// the host is stopped and idles even though samples arrive.
juce::Optional<juce::AudioPlayHead::PositionInfo>
ProjectAudioSource::getPosition() const
{
    const auto project = std::atomic_load(&project_);
    const bool playing = playing_.load();
    const auto timeInSamples = positionSamples_.load();
    const double rate = project != nullptr ? project->sampleRate : 48000.0;

    juce::AudioPlayHead::PositionInfo info;
    info.setIsPlaying(playing);
    info.setTimeInSamples(timeInSamples);
    info.setTimeInSeconds(timeInSamples / rate);
    info.setBpm(project != nullptr ? project->tempoBpm : 120.0);
    juce::AudioPlayHead::TimeSignature ts;
    ts.numerator = project != nullptr ? project->timeSigNumerator : 4;
    ts.denominator = project != nullptr ? project->timeSigDenominator : 4;
    info.setTimeSignature(ts);
    info.setIsLooping(false);
    info.setIsRecording(recording_.load());
    return juce::Optional<juce::AudioPlayHead::PositionInfo>(info);
}

bool ProjectAudioSource::trackInstrumentLive(TrackId trackId) const
{
    auto* node = nodeFor(trackId);
    if (node == nullptr)
        return false;
    auto state = std::atomic_load(&node->state);
    return state != nullptr && state->instrument != nullptr;
}

void ProjectAudioSource::flushAllNotes()
{
    // Internal synths: hard-stop, including voices held by the sustain pedal.
    allNotesOff();

    const auto project = std::atomic_load(&project_);
    if (project == nullptr)
        return;

    // Hosted instruments: give each one a silent panic block. This runs on
    // the message thread while the device is stopped/restarting, so it cannot
    // race processBlock(). It covers the case where stop/pause/seek requested
    // a panic but the device died before another callback could deliver it.
    constexpr int kPanicBlock = 64;
    juce::AudioBuffer<float> silent(2, kPanicBlock);
    silent.clear();
    for (const auto& track : project->tracks)
    {
        if (track.type != Track::Type::Instrument)
            continue;
        auto* node = nodeFor(track.id);
        auto state = node != nullptr ? std::atomic_load(&node->state) : nullptr;
        if (state != nullptr && state->instrument != nullptr
            && ! state->instrumentBypass
            && state->instrumentPreparedBlock > 0)
        {
            const int n = std::min(kPanicBlock,
                                   state->instrumentPreparedBlock);
            juce::AudioBuffer<float> view(
                silent.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer panic;
            addPanicMessages(panic);
            state->instrument->processBlock(view, panic);
        }
        silenceSynthForTrack(track.id);
    }
    lastSustainCc_.fill(0);
}

float ProjectAudioSource::masterPeak(int channel)
{
    if (channel < 0 || channel > 1)
        return 0.0f;
    return std::bit_cast<float>(masterPeaks_[channel].exchange(0, std::memory_order_relaxed));
}

float ProjectAudioSource::trackPeak(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= kMeterTracks)
        return 0.0f;
    return std::bit_cast<float>(trackPeaks_[trackIndex].exchange(0, std::memory_order_relaxed));
}

// ---------- OutputCallback (single audio callback: capture + mix) ----------

class Engine::OutputCallback : public juce::AudioIODeviceCallback
{
public:
    explicit OutputCallback(Engine& owner) : engine(owner) {}

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        engine.handleAudioDeviceAboutToStart(device);
    }

    void audioDeviceStopped() override
    {
        engine.handleAudioDeviceStopped(
            engine.intentionalDeviceStop_.load(std::memory_order_acquire));
    }

    void audioDeviceError(const juce::String& message) override
    {
        engine.handleAudioDeviceError(message);
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numFrames,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        engine.noteAudioCallback();

        // 1) Capture inputs into armed-track buffers (recording).
        if (engine.recording_.load(std::memory_order_relaxed))
        {
            auto captures = std::atomic_load(&engine.captures_);
            if (captures != nullptr)
            {
                for (const auto& capture : *captures)
                {
                    // MIDI captures: loopback synthesizes a note every 0.25 s.
                    if (capture->isMidi)
                    {
                        if (! Engine::debugLoopback_.load(std::memory_order_relaxed))
                            continue;
                        const int now = capture->played.fetch_add(numFrames,
                            std::memory_order_relaxed);
                        const int boundary = 12000; // 0.25 s at 48 kHz
                        std::unique_lock<std::mutex> lock(capture->midiMutex,
                                                         std::try_to_lock);
                        if (! lock.owns_lock())
                            continue;
                        while (capture->loopbackBoundary + boundary <= now)
                        {
                            capture->loopbackBoundary += boundary;
                            const int at = capture->loopbackBoundary;
                            const auto pitch = (std::uint8_t) (60
                                + (capture->loopbackBoundary / boundary) % 12);
                            capture->midiEvents.push_back({ at, 0x90, pitch, 100 });
                            capture->midiEvents.push_back(
                                { at + boundary / 2, 0x80, pitch, 0 });
                        }
                        continue;
                    }

                    const int index = capture->written.load(std::memory_order_relaxed);
                    const int capacity = capture->buffer.getNumSamples();
                    if (index + numFrames > capacity)
                        continue; // buffer full: recording caps at preallocated length

                    if (Engine::debugLoopback_.load(std::memory_order_relaxed))
                    {
                        // Test mode: synthesize a tone instead of reading hardware.
                        const double rate = 48000.0;
                        for (int i = 0; i < numFrames; ++i)
                        {
                            const double t = double(index + i) / rate;
                            capture->buffer.setSample(0, index + i,
                                (float) (0.3 * std::sin(juce::MathConstants<double>::twoPi * 440.0 * t)));
                        }
                        capture->written.store(index + numFrames, std::memory_order_relaxed);
                        continue;
                    }

                    const int ch = capture->inputChannel;
                    if (inputChannelData == nullptr || ch >= numInputChannels
                        || inputChannelData[ch] == nullptr)
                        continue;

                    capture->buffer.copyFrom(0, index, inputChannelData[ch], numFrames);
                    capture->written.store(index + numFrames, std::memory_order_relaxed);
                }
            }
        }

        // 2) Render the transport into the output channels.
        if (outputChannelData == nullptr || outputChannelData[0] == nullptr || numFrames <= 0)
            return;

        juce::AudioBuffer<float> output(outputChannelData, numOutputChannels, numFrames);
        const juce::AudioSourceChannelInfo info { &output, 0, numFrames };
        engine.transport_->getNextAudioBlock(info);

        // 3) Mix the sound-library preview on top (never part of the project).
        engine.renderPreview(outputChannelData, numOutputChannels, numFrames);
    }

private:
    Engine& engine;
};

// ---------- MidiInCallback (routes the selected MIDI input) ----------

class Engine::MidiInCallback : public juce::MidiInputCallback
{
public:
    explicit MidiInCallback(Engine& owner) : engine(owner) {}

    void handleIncomingMidiMessage(juce::MidiInput*,
                                   const juce::MidiMessage& message) override
    {
        engine.onMidiMessage(message);
    }

private:
    Engine& engine;
};

// ---------- sound-library preview ----------
//
// One file on its own channel, mixed on top of the transport and untouched by
// it: previewing a library sound must never alter the project. The audio
// thread only touches the player behind a try-lock, so swapping the source
// (message thread) can never block or crash the callback.

class Engine::PreviewWatchdog : public juce::Timer
{
public:
    explicit PreviewWatchdog(Engine& owner) : engine(owner) {}

    void timerCallback() override
    {
        if (engine.previewTransport_.hasStreamFinished())
            engine.stopPreview();
    }

private:
    Engine& engine;
};

class Engine::AudioHealthWatchdog : public juce::Timer
{
public:
    explicit AudioHealthWatchdog(Engine& owner) : engine(owner) {}

    void timerCallback() override { engine.checkAudioHealth(); }

private:
    Engine& engine;
};

void Engine::preparePreview(int blockSize, double sampleRate)
{
    const juce::ScopedLock sl(previewLock_);
    previewTransport_.prepareToPlay(blockSize, sampleRate);
    previewBuffer_.setSize(2, juce::jmax(blockSize, 512), false, true, true);
    previewBuffer_.clear();
}

void Engine::releasePreview()
{
    const juce::ScopedLock sl(previewLock_);
    previewTransport_.stop();
    previewTransport_.setSource(nullptr);
    previewReader_.reset();
    previewBuffer_.setSize(0, 0);
    previewActive_.store(false, std::memory_order_relaxed);
    if (previewWatchdog_ != nullptr)
        previewWatchdog_->stopTimer();
}

void Engine::renderPreview(float* const* outputChannelData, int numOutputChannels,
                           int numFrames)
{
    if (! previewActive_.load(std::memory_order_relaxed))
        return;
    // Never block the audio thread: if the message thread is mid-swap, the
    // current block simply goes without preview.
    const juce::ScopedTryLock stl(previewLock_);
    if (! stl.isLocked() || previewBuffer_.getNumSamples() < numFrames)
        return;

    previewBuffer_.clear(0, numFrames);
    juce::AudioSourceChannelInfo info { &previewBuffer_, 0, numFrames };
    previewTransport_.getNextAudioBlock(info);

    const int channels = juce::jmin(numOutputChannels, previewBuffer_.getNumChannels());
    for (int ch = 0; ch < channels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::add(outputChannelData[ch],
                                             previewBuffer_.getReadPointer(ch), numFrames);
    juce::ignoreUnused(channels);
}

juce::Result Engine::previewSound(const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail("No existe el archivo: " + file.getFullPathName());

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(file));
    if (reader == nullptr)
        return juce::Result::fail("Formato no soportado: " + file.getFileName());

    auto source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    source->setLooping(false);

    bool ready;
    {
        const juce::ScopedLock sl(previewLock_);
        previewTransport_.stop();
        previewTransport_.setSource(nullptr);
        previewReader_ = std::move(source);
        previewTransport_.setSource(previewReader_.get());
        previewTransport_.setPosition(0.0);
        previewTransport_.setGain(1.0);
        ready = previewBuffer_.getNumSamples() > 0;
        if (ready)
            previewTransport_.start();
    }
    previewActive_.store(ready, std::memory_order_relaxed);
    if (ready && previewWatchdog_ != nullptr)
        previewWatchdog_->startTimer(120);
    return juce::Result::ok();
}

void Engine::stopPreview()
{
    previewTransport_.stop();
    previewActive_.store(false, std::memory_order_relaxed);
    if (previewWatchdog_ != nullptr)
        previewWatchdog_->stopTimer();
}

// ---------- Engine ----------

Engine::Engine()
{
    startMessageLoop(); // first: the dedicated thread must own the message manager
    formatManager_.registerBasicFormats();
    // Library sounds (Wikimedia/CC catalogs) are mostly OGG; production
    // sample packs (Tonejs/audio etc.) are mostly MP3 — decode both.
    formatManager_.registerFormat(new juce::OggVorbisAudioFormat(), true);
    formatManager_.registerFormat(new juce::MP3AudioFormat(), true);
    pluginHost_.setup();
    transport_ = std::make_unique<ProjectAudioSource>();
    transport_->setPluginHost(&pluginHost_);
    transport_->setPluginsChangedCallback(
        [this] { closeOrphanedEditorWindows(); });
    outputCallback_ = std::make_unique<OutputCallback>(*this);
    midiInCallback_ = std::make_unique<MidiInCallback>(*this);
    previewWatchdog_ = std::make_unique<PreviewWatchdog>(*this);
    audioHealthWatchdog_ = std::make_unique<AudioHealthWatchdog>(*this);
    audioHealthWatchdog_->startTimer(500);
    aiService_ = std::make_unique<ai::Service>();
}

Engine::~Engine()
{
    shuttingDown_.store(true, std::memory_order_release);
    // Close every plugin editor before the instances are torn down. Letting
    // AppKit destroy a live VST3 window during process teardown crashed
    // Analog Lab V inside its componentVisibilityChanged callback.
    closeAllEditorWindows();
    if (audioHealthWatchdog_ != nullptr)
    {
        audioHealthWatchdog_->stopTimer();
        audioHealthWatchdog_.reset();
    }

    aiCancel_.store(true, std::memory_order_release);
    if (aiThread_.joinable())
        aiThread_.join();
    if (initCancel_ != nullptr)
        initCancel_->store(true, std::memory_order_release);

    // A device close during shutdown is intentional: never let the error
    // path schedule a recovery while the Engine is being destroyed.
    intentionalDeviceStop_.store(true, std::memory_order_release);
    transport_->pause();
    transport_->requestPanic();
    deviceManager_.removeAudioCallback(outputCallback_.get());
    deviceManager_.closeAudioDevice();
    // Deliver the panic to plugins while the device is stopped and the
    // message thread owns them, then destroy every instance BEFORE the
    // plugin host/format manager members are destroyed. The old member order
    // destroyed pluginHost_ before transport_, unloading plugin modules while
    // live instances still existed (the Analog Lab V abort at exit).
    transport_->flushAllNotes();
    transport_->releaseResources();
    transport_.reset();
    stopMessageLoop();
}

void Engine::initialiseAudioInBackground()
{
    // Open the device on the next message-loop turn instead of blocking the
    // Dart FFI caller. initialise() itself must run on the message thread
    // (JUCE device management), so a worker thread would only wait for it.
    // The shared cancellation token makes the deferred lambda safe if the
    // app quits before it runs.
    auto* mm = juce::MessageManager::getInstance();
    auto cancel = initCancel_;
    mm->callAsync([this, cancel] {
        if (cancel == nullptr || cancel->load(std::memory_order_acquire))
            return;
        const auto r = initialise();
        if (cancel->load(std::memory_order_acquire))
            return;
        audioReady_.store(r.wasOk(), std::memory_order_release);
    });
}

void Engine::startMessageLoop()
{
    // On macOS JUCE's dispatch loop IS [NSApp run] — AppKit only allows it
    // on the process main thread, where Flutter already pumps its own loop.
    // Spawning a dedicated dispatch thread exits silently ([NSApp run] is
    // not legal off-main), so instead we create the manager here: its first
    // caller becomes the message thread, JUCE async work (chunked plugin
    // scans, marshaled device ops) is processed by the main run loop that
    // Flutter keeps pumping, and runOnMessageThread() from Dart FFI calls
    // (which arrive on the main thread) runs synchronously.
    juce::MessageManager::getInstance();
    messageLoopRunning_ = true;
}

void Engine::stopMessageLoop()
{
    // No dispatch loop of our own to stop — and stopDispatchLoop() must NOT
    // be sent to the shared main-thread run loop that Flutter owns.
    messageLoopRunning_ = false;
}

void Engine::runOnMessageThread(const std::function<void()>& fn)
{
    auto* mm = juce::MessageManager::getInstance();
    if (mm->isThisTheMessageThread())
    {
        fn();
        return;
    }

    juce::WaitableEvent done;
    mm->callAsync([&] {
        fn();
        done.signal();
    });
    done.wait();
}

juce::Result Engine::addClipFromPath(TrackId trackId, const juce::String& path,
                                     double startSeconds)
{
    return addClip(trackId, juce::File(path), startSeconds, 1.0f) >= 0
        ? juce::Result::ok()
        : juce::Result::fail("cannot add clip from " + path);
}

juce::Result Engine::restartAudio()
{
    // Explicit user action or automatic recovery. Reopen the SAME device
    // setup; the old code called initialiseWithDefaultDevices() and silently
    // threw away the user's UR22 selection/buffer size.
    juce::Result result = juce::Result::ok();
    runOnMessageThread([&] {
        // A device restart must not throw away an in-progress take.
        if (isRecording())
            finalizeRecording();
        const bool wasPlaying = transport_->isPlaying();
        const double position = transport_->positionSeconds();

        audioReady_.store(false, std::memory_order_release);
        intentionalDeviceStop_.store(true, std::memory_order_release);
        transport_->pause();
        transport_->requestPanic();

        deviceManager_.removeAudioCallback(outputCallback_.get());
        deviceManager_.closeAudioDevice();
        // The device is stopped: deliver a hard panic now. A stop/pause panic
        // requested just before a device death would otherwise never reach
        // the plugins, leaving stuck voices after recovery.
        transport_->flushAllNotes();

        const auto setup = deviceManager_.getAudioDeviceSetup();
        if (setup.outputDeviceName.isNotEmpty()
            || setup.inputDeviceName.isNotEmpty())
            deviceManager_.restartLastAudioDevice();

        if (deviceManager_.getCurrentAudioDevice() == nullptr)
        {
            const auto err = deviceManager_.initialiseWithDefaultDevices(2, 2);
            if (err.isNotEmpty())
                result = juce::Result::fail("device restart: " + err);
        }

        if (result.wasOk() && deviceManager_.getCurrentAudioDevice() != nullptr)
        {
            deviceManager_.addAudioCallback(outputCallback_.get());
            audioDeviceRunning_.store(true, std::memory_order_release);
            audioCallbackHeartbeat_.store(
                audioCallbackCount_.load(std::memory_order_relaxed));
            audioStallTicks_ = 0;
            // Prepare the plugin graph for the new rate/block before the
            // transport resumes; otherwise the first blocks silently skip
            // instruments/fx until the async reconcile lands.
            reconcileAfterDeviceChange();
            transport_->seek(position);
            if (wasPlaying)
                transport_->play();
            audioReady_.store(true, std::memory_order_release);
        }

        intentionalDeviceStop_.store(false, std::memory_order_release);
        if (result.failed())
        {
            audioReady_.store(false, std::memory_order_release);
            audioDeviceRunning_.store(false, std::memory_order_release);
        }
    });

    if (result.failed())
        lastErrorUtf8_ = result.getErrorMessage().toStdString();
    return result;
}

juce::Result Engine::initialise()
{
    juce::Result error = juce::Result::ok();
    runOnMessageThread([&] {
        // loadDeviceSettings() only parses settings now; it must not start
        // CoreAudio during Flutter boot (that can deadlock AudioDeviceStart).
        if (deviceManager_.getCurrentAudioDevice() != nullptr)
        {
            deviceManager_.addAudioCallback(outputCallback_.get());
            audioReady_.store(true, std::memory_order_release);
            audioConfigDirty_.store(true, std::memory_order_release);
            reconcileAfterDeviceChange();
            return;
        }

        // Open output-only: a wedged input device (UR22) can block forever
        // in AudioDeviceStart. Inputs open on demand in startRecord().
        juce::String openError;
        if (savedDeviceState_ != nullptr)
            openError = deviceManager_.initialise(
                0, 2, savedDeviceState_.get(), true);

        if (deviceManager_.getCurrentAudioDevice() == nullptr)
        {
            auto setup = deviceManager_.getAudioDeviceSetup();
            setup.inputDeviceName = {};
            setup.useDefaultInputChannels = false;
            setup.inputChannels.clear();
            if (setup.outputDeviceName.isNotEmpty())
                deviceManager_.setAudioDeviceSetup(setup, true);
        }

        if (deviceManager_.getCurrentAudioDevice() == nullptr)
        {
            openError = deviceManager_.initialiseWithDefaultDevices(0, 2);
            if (openError.isNotEmpty())
            {
                error = juce::Result::fail(openError);
                return;
            }
        }

        deviceManager_.addAudioCallback(outputCallback_.get());
        audioReady_.store(deviceManager_.getCurrentAudioDevice() != nullptr,
                          std::memory_order_release);
        audioConfigDirty_.store(true, std::memory_order_release);
        reconcileAfterDeviceChange();
    });

    if (error.failed())
    {
        audioReady_.store(false, std::memory_order_release);
        lastErrorUtf8_ = error.getErrorMessage().toStdString();
        return juce::Result::fail(error.getErrorMessage());
    }
    return juce::Result::ok();
}

bool Engine::isInitialised() const
{
    // Do not dereference the device pointer here: Dart can poll this while
    // CoreAudio is closing/reopening the device. The atomic is published by
    // audioDeviceAboutToStart/audioDeviceStopped and by initialise().
    return audioReady_.load(std::memory_order_acquire);
}

int Engine::audioXRunCount() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        const int xruns = device->getXRunCount();
        if (xruns >= 0)
            return xruns;
    }
    return audioXRunCount_.load(std::memory_order_relaxed);
}

const char* Engine::lastAudioErrorUtf8()
{
    const std::scoped_lock lock(audioErrorMutex_);
    lastAudioErrorUtf8_ = lastAudioError_.toStdString();
    return lastAudioErrorUtf8_.c_str();
}

void Engine::prepareTransportForDevice(juce::AudioIODevice* device)
{
    if (device == nullptr)
    {
        transport_->releaseResources();
        releasePreview();
        return;
    }
    transport_->prepareToPlay(device->getCurrentBufferSizeSamples(),
                              device->getCurrentSampleRate());
    preparePreview(device->getCurrentBufferSizeSamples(),
                   device->getCurrentSampleRate());
}

void Engine::handleAudioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
    {
        transport_->releaseResources();
        releasePreview();
        audioDeviceRunning_.store(false, std::memory_order_release);
        return;
    }

    prepareTransportForDevice(device);
    audioDeviceRunning_.store(true, std::memory_order_release);
    audioCallbackHeartbeat_.store(
        audioCallbackCount_.load(std::memory_order_relaxed));
    audioStallTicks_ = 0;
    audioReady_.store(true, std::memory_order_release);
    // The device-start callback may run while the device manager holds its
    // callback lock. Never block here: mark the graph dirty and let the
    // message-thread watchdog reconcile it.
    audioConfigDirty_.store(true, std::memory_order_release);
}

void Engine::handleAudioDeviceStopped(bool intentional)
{
    transport_->releaseResources();
    releasePreview();
    audioDeviceRunning_.store(false, std::memory_order_release);
    audioReady_.store(false, std::memory_order_release);
    if (! intentional && ! shuttingDown_.load(std::memory_order_acquire))
        scheduleAudioRecovery("audio device stopped unexpectedly");
}

void Engine::handleAudioDeviceError(const juce::String& message)
{
    {
        const std::scoped_lock lock(audioErrorMutex_);
        lastAudioError_ = message;
        lastAudioErrorUtf8_ = message.toStdString();
    }
    scheduleAudioRecovery("CoreAudio: " + message);
}

void Engine::scheduleAudioRecovery(const juce::String& reason)
{
    if (shuttingDown_.load(std::memory_order_acquire)
        || intentionalDeviceStop_.load(std::memory_order_acquire))
        return;
    {
        const std::scoped_lock lock(audioErrorMutex_);
        if (lastAudioError_.isEmpty())
            lastAudioError_ = reason;
    }
    audioRecoveryPending_.store(true, std::memory_order_release);
}

void Engine::recoverAudio()
{
    if (shuttingDown_.load(std::memory_order_acquire))
        return;

    if (auto* device = deviceManager_.getCurrentAudioDevice())
        if (device->isPlaying())
        {
            audioRecoveryPending_.store(false, std::memory_order_release);
            audioDeviceRunning_.store(true, std::memory_order_release);
            return;
        }

    const auto result = restartAudio();
    if (result.wasOk())
    {
        audioRecoveryCount_.fetch_add(1, std::memory_order_relaxed);
        audioRecoveryPending_.store(false, std::memory_order_release);
    }
    else
    {
        // Try again on the next watchdog tick.
        audioRecoveryPending_.store(true, std::memory_order_release);
    }
}

void Engine::reconcileAfterDeviceChange()
{
    if (shuttingDown_.load(std::memory_order_acquire))
        return;
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    auto project = std::atomic_load(&project_);
    if (project == nullptr)
        return;
    // Defer heavy plugin loads: a device change must not freeze the UI while
    // every VST3 is re-instantiated at the new rate. The message-loop job pump
    // loads one track at a time; tracks whose old instances are at the wrong
    // rate fall back to the internal synth until their new instance lands.
    transport_->reconcileNodes(*project, pluginHost_,
                               device->getCurrentBufferSizeSamples(), true);
    audioConfigDirty_.store(false, std::memory_order_release);
}

void Engine::checkAudioHealth()
{
    if (shuttingDown_.load(std::memory_order_acquire))
        return;

    if (audioConfigDirty_.exchange(false, std::memory_order_acq_rel))
        reconcileAfterDeviceChange();

    if (audioRecoveryPending_.exchange(false, std::memory_order_acq_rel))
    {
        recoverAudio();
        return;
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
    {
        if (audioDeviceRunning_.exchange(false, std::memory_order_acq_rel)
            && ! intentionalDeviceStop_.load(std::memory_order_acquire))
            scheduleAudioRecovery("audio device disappeared");
        return;
    }

    audioDeviceRunning_.store(true, std::memory_order_release);
    if (const int xruns = device->getXRunCount(); xruns >= 0)
        audioXRunCount_.store(xruns, std::memory_order_relaxed);

    if (! device->isPlaying())
    {
        if (! intentionalDeviceStop_.load(std::memory_order_acquire))
            scheduleAudioRecovery("audio device is not running");
        return;
    }

    // Heartbeat: every audio callback increments this counter. If it stops
    // advancing while the device claims to be playing, the callback thread is
    // stuck or CoreAudio silently disabled the client. Recover automatically
    // instead of making the user restart the whole app.
    const auto count = audioCallbackCount_.load(std::memory_order_relaxed);
    const auto last = audioCallbackHeartbeat_.exchange(
        count, std::memory_order_relaxed);
    if (count == last)
    {
        if (++audioStallTicks_ >= 4)
        {
            audioStallTicks_ = 0;
            scheduleAudioRecovery("audio callback stalled");
        }
    }
    else
    {
        audioStallTicks_ = 0;
    }
}

juce::StringArray Engine::outputDeviceNames()
{
    juce::OwnedArray<juce::AudioIODeviceType> types;
    deviceManager_.createAudioDeviceTypes(types);

    juce::StringArray names;
    for (auto* type : types)
    {
        type->scanForDevices();
        names.addArray(type->getDeviceNames(false));
    }
    names.removeEmptyStrings();
    names.removeDuplicates(true);
    return names;
}

juce::StringArray Engine::inputDeviceNames()
{
    juce::OwnedArray<juce::AudioIODeviceType> types;
    deviceManager_.createAudioDeviceTypes(types);

    juce::StringArray names;
    for (auto* type : types)
    {
        type->scanForDevices();
        names.addArray(type->getDeviceNames(true));
    }
    names.removeEmptyStrings();
    names.removeDuplicates(true);
    return names;
}

juce::String Engine::currentInputDeviceName() const
{
    // Inputs are not opened at launch; show the remembered choice.
    if (preferredInputDeviceName_.isNotEmpty())
        return preferredInputDeviceName_;
    return currentOutputDeviceName(); // one CoreAudio device covers in+out
}

const char* Engine::outputDeviceNameAtUtf8(int index)
{
    stringOut_.clear();
    const auto names = outputDeviceNames();
    if (index >= 0 && index < names.size())
        stringOut_ = names[index].toStdString();
    return stringOut_.c_str();
}

const char* Engine::inputDeviceNameAtUtf8(int index)
{
    stringOut_.clear();
    const auto names = inputDeviceNames();
    if (index >= 0 && index < names.size())
        stringOut_ = names[index].toStdString();
    return stringOut_.c_str();
}

const char* Engine::midiInputNameAtUtf8(int index)
{
    stringOut_.clear();
    const auto names = midiInputNames();
    if (index >= 0 && index < names.size())
        stringOut_ = names[index].toStdString();
    return stringOut_.c_str();
}

const char* Engine::currentOutputDeviceNameUtf8()
{
    stringOut_ = currentOutputDeviceName().toStdString();
    return stringOut_.c_str();
}

const char* Engine::currentInputDeviceNameUtf8()
{
    stringOut_ = currentInputDeviceName().toStdString();
    return stringOut_.c_str();
}

juce::Result Engine::setDeviceConfig(const juce::String& outputDevice,
                                     const juce::String& inputDevice,
                                     double sampleRate, int bufferSize)
{
    // Device reopen must happen on the JUCE message thread.
    // If the new setup fails, revert to the previous working one — a broken
    // device state leaves the app silent until the process restarts.
    juce::Result result = juce::Result::ok();
    runOnMessageThread([&] {
        const auto previous = deviceManager_.getAudioDeviceSetup();
        if (isRecording())
            finalizeRecording();
        const bool wasPlaying = transport_->isPlaying();
        const double position = transport_->positionSeconds();
        transport_->pause();
        transport_->requestPanic();

        auto setup = previous;
        if (outputDevice.isNotEmpty())
            setup.outputDeviceName = outputDevice;
        if (inputDevice.isNotEmpty())
        {
            setup.inputDeviceName = inputDevice;
            // The live device is normally output-only, so inputChannels is
            // empty and numInputChansNeeded is 0. JUCE's
            // useDefaultInputChannels keeps that stored 0, so set the first
            // input pair explicitly; setAudioDeviceSetup then derives the
            // needed channel count from the bitset.
            setup.useDefaultInputChannels = false;
            setup.inputChannels.clear();
            setup.inputChannels.setBit(0);
            setup.inputChannels.setBit(1);
            preferredInputDeviceName_ = inputDevice;
        }
        if (sampleRate > 0)
            setup.sampleRate = sampleRate;
        if (bufferSize > 0)
            setup.bufferSize = bufferSize;

        intentionalDeviceStop_.store(true, std::memory_order_release);
        const auto error = deviceManager_.setAudioDeviceSetup(setup, true);
        if (error.isNotEmpty())
        {
            // Revert: the new setup didn't work.
            deviceManager_.setAudioDeviceSetup(previous, true);
            result = juce::Result::fail("device config: " + error);
        }
        else
        {
            result = juce::Result::ok();
        }
        intentionalDeviceStop_.store(false, std::memory_order_release);

        // The device reopened at a new rate/block. Rebuild the plugin graph
        // before resuming so the first blocks are not silently skipped.
        reconcileAfterDeviceChange();
        audioReady_.store(deviceManager_.getCurrentAudioDevice() != nullptr,
                          std::memory_order_release);
        transport_->seek(position);
        if (wasPlaying)
            transport_->play();
    });
    return result;
}

juce::Result Engine::openInputChannels()
{
    if (deviceInputChannelCount() > 0)
        return juce::Result::ok();

    juce::String inputName = preferredInputDeviceName_;
    if (inputName.isEmpty())
    {
        const auto inputs = inputDeviceNames();
        const auto outputName = currentOutputDeviceName();
        if (inputs.contains(outputName))
            inputName = outputName;
        else if (! inputs.isEmpty())
            inputName = inputs[0];
    }
    if (inputName.isEmpty())
        return juce::Result::fail("no audio input device available");

    double rate = 0.0;
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        rate = device->getCurrentSampleRate();

    const auto result = setDeviceConfig(currentOutputDeviceName(), inputName,
                                        rate, currentBufferSizeSamples());
    if (result.failed())
        return result;
    preferredInputDeviceName_ = inputName;
    if (deviceInputChannelCount() == 0)
        return juce::Result::fail("audio input did not open: " + inputName);
    return juce::Result::ok();
}

juce::Array<double> Engine::availableSampleRates() const
{
    juce::Array<double> rates;
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        rates = device->getAvailableSampleRates();
    if (rates.isEmpty())
        for (double r : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
            rates.add(r);
    return rates;
}

juce::Array<int> Engine::availableBufferSizes() const
{
    juce::Array<int> sizes;
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        sizes = device->getAvailableBufferSizes();
    if (sizes.isEmpty())
        for (int s : {32, 64, 128, 256, 512, 1024, 2048})
            sizes.add(s);
    return sizes;
}

juce::StringArray Engine::midiInputNames() const
{
    juce::StringArray names;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        names.add(device.name);
    return names;
}

void Engine::selectMidiInput(int index)
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    if (index < 0 || index >= devices.size())
        return activateMidiInput({});
    activateMidiInput(devices[index].identifier);
}

void Engine::activateMidiInput(const juce::String& identifier)
{
    if (selectedMidiInputId_.isNotEmpty())
        deviceManager_.removeMidiInputDeviceCallback(selectedMidiInputId_,
                                                     midiInCallback_.get());
    selectedMidiInputId_ = identifier;
    if (identifier.isNotEmpty())
    {
        deviceManager_.setMidiInputDeviceEnabled(identifier, true);
        deviceManager_.addMidiInputDeviceCallback(identifier, midiInCallback_.get());
    }
}

void Engine::onMidiMessage(const juce::MidiMessage& message)
{
    if (message.isMidiClock() || message.isMidiStart() || message.isMidiStop()
        || message.isSongPositionPointer() || message.isActiveSense())
        return;

    // Live monitoring routes notes to armed instrument tracks regardless of
    // whether we are recording.
    transport_->pushExternalMidi(message);

    if (! recording_.load(std::memory_order_relaxed))
        return;

    auto captures = std::atomic_load(&captures_);
    if (captures == nullptr)
        return;

    const double rate = sampleRate();
    const std::int64_t rel = (std::int64_t) (transport_->positionSeconds() * rate)
                             - recordStartSamples_.load();
    if (rel < 0)
        return;

    const auto* raw = message.getRawData();
    const int rawSize = message.getRawDataSize();
    if (raw == nullptr || rawSize <= 0)
        return;

    for (auto& capture : *captures)
    {
        if (! capture->isMidi)
            continue;
        const std::scoped_lock lock(capture->midiMutex);
        capture->midiEvents.push_back({ (int) std::min<std::int64_t>(rel, INT32_MAX),
                                        raw[0],
                                        rawSize > 1 ? raw[1] : (std::uint8_t) 0,
                                        rawSize > 2 ? raw[2] : (std::uint8_t) 0 });
    }
}

int Engine::selectedMidiInputIndex() const
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i)
        if (devices[i].identifier == selectedMidiInputId_)
            return i;
    return -1;
}

int Engine::deviceInputChannelCount() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

juce::Result Engine::saveDeviceSettings(const juce::File& file) const
{
    auto* root = new juce::DynamicObject();
    // The device may not have been opened yet (lazy open). Fall back to the
    // XML loaded at startup so saving settings does not erase it.
    auto state = deviceManager_.createStateXml();
    if (state == nullptr && savedDeviceState_ != nullptr)
        state = std::make_unique<juce::XmlElement>(*savedDeviceState_);
    if (state != nullptr)
    {
        // The live device is normally output-only (inputs open on demand),
        // so put the remembered input device back into the saved XML.
        if (preferredInputDeviceName_.isNotEmpty())
            state->setAttribute("audioInputDeviceName",
                                preferredInputDeviceName_);
        // Do not persist the input channel mask: loadDeviceSettings() would
        // use it to open inputs at launch, which is exactly what we avoid.
        state->removeAttribute("audioDeviceInChans");
        root->setProperty("deviceState", state->createDocument(juce::String()));
    }
    else
    {
        root->setProperty("deviceState", "");
    }

    juce::Array<juce::var> midiInputs;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager_.isMidiInputDeviceEnabled(device.identifier))
            midiInputs.add(device.identifier);
    root->setProperty("midiInputs", midiInputs);
    root->setProperty("selectedMidiInput", selectedMidiInputId_);
    root->setProperty("midiInputLatencyMs", midiInputLatencyMs_);

    if (! file.replaceWithText(juce::JSON::toString(juce::var(root), true)))
        return juce::Result::fail("cannot write " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result Engine::loadDeviceSettings(const juce::File& file)
{
    auto parsed = juce::JSON::parse(file);
    if (parsed.isVoid())
        return juce::Result::fail("no settings file: " + file.getFullPathName());

    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return juce::Result::fail("invalid settings file");

    const auto stateText = root->getProperty("deviceState").toString();
    savedDeviceState_.reset();
    savedOutputDeviceName_.clear();
    savedSampleRate_ = 0.0;
    savedBufferSize_ = 0;
    preferredInputDeviceName_.clear();
    if (stateText.isNotEmpty())
    {
        if (auto xml = juce::parseXML(stateText))
        {
            // Remember the user's choices, but do NOT open the device here.
            // Starting CoreAudio during Flutter boot can deadlock
            // AudioDeviceStart on the main thread, and a wedged UR22 input
            // can block forever. initialise() opens it on first play/record.
            preferredInputDeviceName_ =
                xml->getStringAttribute("audioInputDeviceName");
            savedOutputDeviceName_ =
                xml->getStringAttribute("audioOutputDeviceName");
            savedSampleRate_ =
                xml->getDoubleAttribute("audioDeviceRate", 0.0);
            savedBufferSize_ =
                xml->getIntAttribute("audioDeviceBufferSize", 0);
            xml->removeAttribute("audioInputDeviceName");
            xml->removeAttribute("audioDeviceInChans");
            savedDeviceState_ = std::move(xml);
        }
    }

    auto midiVar = root->getProperty("midiInputs");
    if (! midiVar.isVoid())
        if (auto* midi = midiVar.getArray())
            for (const auto& item : *midi)
                deviceManager_.setMidiInputDeviceEnabled(item.toString(), true);

    const auto selected = root->getProperty("selectedMidiInput").toString();
    if (selected.isNotEmpty())
        activateMidiInput(selected);

    const auto latency = root->getProperty("midiInputLatencyMs");
    if (latency.isDouble() || latency.isInt())
        midiInputLatencyMs_ = (double) latency;

    return juce::Result::ok();
}

juce::String Engine::currentOutputDeviceName() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getName();
    return savedOutputDeviceName_;
}

int Engine::currentBufferSizeSamples() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();
    return savedBufferSize_;
}

double Engine::sampleRate() const
{
    if (auto project = std::atomic_load(&project_))
        return project->sampleRate;
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return 48000.0;
}

double Engine::defaultRate() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        if (device->getCurrentSampleRate() > 0)
            return device->getCurrentSampleRate();
    return 48000.0;
}

juce::Result Engine::fail(const juce::String& message) const
{
    lastErrorUtf8_ = message.toStdString();
    return juce::Result::fail(message);
}

std::shared_ptr<const Project> Engine::makeEmptyProjectLocked(double sampleRate)
{
    auto project = std::make_shared<Project>();
    project->sampleRate = sampleRate;
    return project;
}

void Engine::publish(std::shared_ptr<const Project> project,
                     bool deferHeavyPlugins)
{
    std::atomic_store(&project_, project);
    transport_->setProject(project);

    // Rebuild fx/instrument plugin nodes whose config changed. Plugin
    // instantiation must happen off the audio thread.
    if (pluginHost_.pluginCount() >= 0)
    {
        std::shared_ptr<const Project> snapshot = project;
        runOnMessageThread([&snapshot, this, deferHeavyPlugins] {
            if (transport_ != nullptr)
                transport_->reconcileNodes(*snapshot, pluginHost_,
                                           currentBufferSizeSamples(),
                                           deferHeavyPlugins);
        });
    }
}

std::shared_ptr<Project> Engine::copyForEdit() const
{
    if (auto current = std::atomic_load(&project_))
        return std::make_shared<Project>(*current);
    return std::make_shared<Project>();
}

juce::Result Engine::newProject(double sampleRate)
{
    const std::scoped_lock lock(editMutex_);

    if (! (sampleRate >= 8000.0 && sampleRate <= 192000.0))
        return fail("invalid sample rate");

    auto project = makeEmptyProjectLocked(sampleRate);
    undoStack_.clear();
    redoStack_.clear();
    clipboardValid_ = false;
    publish(project);
    transport_->stop();
    return juce::Result::ok();
}

TrackId Engine::addTrack(const juce::String& name, int colorIndex, Track::Type type)
{
    const std::scoped_lock lock(editMutex_);

    if (std::atomic_load(&project_) == nullptr)
        publish(makeEmptyProjectLocked(defaultRate()));

    auto project = copyForEdit();

    Track track;
    track.id = project->nextTrackId++;
    track.name = name.isEmpty() ? "Track " + juce::String((int) track.id) : name;
    track.colorIndex = colorIndex;
    track.type = type;
    const TrackId newId = track.id;
    project->tracks.push_back(std::move(track));

    pushUndoLocked();
    publish(std::move(project));
    return newId;
}

SourceId Engine::loadSourceInto(Project& project, const juce::File& file)
{
    const auto key = file.getFullPathName();

    for (const auto& [id, path] : project.sourceFiles)
        if (path == key)
            return id; // decoded data travels with the snapshot copy

    auto data = std::make_shared<AudioSourceData>();
    if (loadAudioSourceData(formatManager_, file, project.sampleRate, *data).failed())
    {
        fail("cannot read audio file: " + key);
        return -1;
    }

    const SourceId id = project.nextSourceId++;
    project.sourceFiles[id] = key;
    project.sources[id] = std::move(data);
    return id;
}

ClipId Engine::addClip(TrackId trackId, const juce::File& sourceFile,
                       double startSeconds, float gain)
{
    const std::scoped_lock lock(editMutex_);

    if (std::atomic_load(&project_) == nullptr)
        publish(makeEmptyProjectLocked(defaultRate()));

    auto project = copyForEdit();

    Track* track = project->findTrack(trackId);
    if (track == nullptr)
    {
        fail("track not found: " + juce::String((int) trackId));
        return -1;
    }

    if (! sourceFile.existsAsFile())
    {
        fail("file not found: " + sourceFile.getFullPathName());
        return -1;
    }

    const SourceId sourceId = loadSourceInto(*project, sourceFile);
    if (sourceId < 0)
        return -1;

    Clip clip;
    clip.id = project->nextClipId++;
    clip.sourceId = sourceId;
    clip.startSeconds = std::max(0.0, startSeconds);
    clip.lengthSeconds = std::max(0.0, project->sources[sourceId]->durationSeconds
                                              - clip.sourceOffsetSeconds);
    clip.gain = juce::jlimit(0.0f, 4.0f, gain);
    const ClipId newId = clip.id;
    track->clips.push_back(std::move(clip));

    pushUndoLocked();
    publish(std::move(project));
    return newId;
}

void Engine::pushUndoLocked()
{
    if (auto current = std::atomic_load(&project_))
    {
        undoStack_.push_back(std::move(current));
        if (undoStack_.size() > 200)
            undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

juce::Result Engine::editProject(const std::function<bool(Project&)>& mutate)
{
    const std::scoped_lock lock(editMutex_);

    if (std::atomic_load(&project_) == nullptr)
        publish(makeEmptyProjectLocked(defaultRate()));

    auto project = copyForEdit();
    if (! mutate(*project))
        return juce::Result::fail(juce::String(lastErrorUtf8_));

    pushUndoLocked();
    publish(std::move(project));
    return juce::Result::ok();
}

bool Engine::canUndo() const
{
    const std::scoped_lock lock(editMutex_);
    return ! undoStack_.empty();
}

bool Engine::canRedo() const
{
    const std::scoped_lock lock(editMutex_);
    return ! redoStack_.empty();
}

bool Engine::undo()
{
    const std::scoped_lock lock(editMutex_);
    if (undoStack_.empty())
        return false;

    if (auto current = std::atomic_load(&project_))
        redoStack_.push_back(std::move(current));

    auto target = std::move(undoStack_.back());
    undoStack_.pop_back();
    publish(std::move(target));
    return true;
}

bool Engine::redo()
{
    const std::scoped_lock lock(editMutex_);
    if (redoStack_.empty())
        return false;

    if (auto current = std::atomic_load(&project_))
        undoStack_.push_back(std::move(current));

    auto target = std::move(redoStack_.back());
    redoStack_.pop_back();
    publish(std::move(target));
    return true;
}

juce::Result Engine::removeTrack(TrackId trackId)
{
    return editProject([&] (Project& project) {
        auto& tracks = project.tracks;
        for (auto it = tracks.begin(); it != tracks.end(); ++it)
        {
            if (it->id == trackId)
            {
                tracks.erase(it);
                return true;
            }
        }
        fail("track not found: " + juce::String((int) trackId));
        return false;
    });
}

juce::Result Engine::removeClip(ClipId clipId)
{
    return editProject([&] (Project& project) {
        for (auto& track : project.tracks)
        {
            auto& clips = track.clips;
            for (auto it = clips.begin(); it != clips.end(); ++it)
            {
                if (it->id == clipId)
                {
                    clips.erase(it);
                    return true;
                }
            }
        }
        fail("clip not found: " + juce::String((int) clipId));
        return false;
    });
}

juce::Result Engine::removeClips(const std::vector<ClipId>& clipIds)
{
    if (clipIds.empty())
        return juce::Result::ok();

    return editProject([&] (Project& project) {
        // Validate first: a missing id must not leave a half-applied edit.
        for (const auto clipId : clipIds)
        {
            if (project.findClip(clipId) == nullptr)
            {
                fail("clip not found: " + juce::String((int) clipId));
                return false;
            }
        }

        for (const auto clipId : clipIds)
        {
            for (auto& track : project.tracks)
            {
                auto& clips = track.clips;
                const auto it = std::find_if(clips.begin(), clips.end(),
                                             [clipId] (const Clip& c)
                                             { return c.id == clipId; });
                if (it != clips.end())
                {
                    clips.erase(it);
                    break;
                }
            }
        }
        return true;
    });
}

juce::Result Engine::setClipStart(ClipId clipId, double seconds)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr)
        {
            fail("clip not found: " + juce::String((int) clipId));
            return false;
        }
        clip->startSeconds = std::max(0.0, seconds);
        return true;
    });
}

juce::Result Engine::setClipGain(ClipId clipId, float gain)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr)
        {
            fail("clip not found: " + juce::String((int) clipId));
            return false;
        }
        clip->gain = juce::jlimit(0.0f, 4.0f, gain);
        return true;
    });
}

juce::Result Engine::setClipBounds(ClipId clipId, double startSeconds,
                                   double sourceOffsetSeconds, double lengthSeconds)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr)
        {
            fail("clip not found: " + juce::String((int) clipId));
            return false;
        }

        double start = std::max(0.0, startSeconds);
        double offset = std::max(0.0, sourceOffsetSeconds);
        double length = std::max(kMinClipLength, lengthSeconds);

        // MIDI clips: the content is the notes. A border change must keep
        // every note at its ABSOLUTE timeline position (Cubase semantics):
        // moving the start by Δ shifts the clip-relative note positions by
        // −Δ, so a left trim hides the early notes (they come back when the
        // border is extended again — nothing is destroyed).
        if (clip->midi != nullptr)
        {
            const double delta = start - clip->startSeconds;
            clip->sourceOffsetSeconds = 0.0;
            clip->startSeconds = start;
            clip->lengthSeconds = length;
            if (std::abs(delta) > 1e-9 && ! clip->midi->notes.empty())
            {
                auto midi = std::make_shared<MidiClipData>(*clip->midi);
                for (auto& note : midi->notes)
                {
                    note.startSeconds -= delta;
                    note.origStartSeconds -= delta;
                }
                clip->midi = std::move(midi);
            }
            return true;
        }

        double srcDuration = 0.0;
        if (auto srcIt = project.sources.find(clip->sourceId); srcIt != project.sources.end())
            srcDuration = srcIt->second->durationSeconds;
        if (srcDuration > 0.0 && offset + length > srcDuration)
            length = std::max(kMinClipLength, srcDuration - offset);

        clip->startSeconds = start;
        clip->sourceOffsetSeconds = offset;
        clip->lengthSeconds = length;
        return true;
    });
}

juce::Result Engine::setClipFades(ClipId clipId, double fadeInSeconds, double fadeOutSeconds)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr)
        {
            fail("clip not found: " + juce::String((int) clipId));
            return false;
        }
        clip->fadeInSeconds = (float) juce::jlimit(0.0, clip->lengthSeconds,
                                                   std::max(0.0, fadeInSeconds));
        clip->fadeOutSeconds = (float) juce::jlimit(0.0, clip->lengthSeconds,
                                                    std::max(0.0, fadeOutSeconds));
        return true;
    });
}

juce::Result Engine::moveClipToTrack(ClipId clipId, TrackId targetTrackId,
                                     double startSeconds)
{
    return editProject([&] (Project& project) {
        const Track* sourceTrack = nullptr;
        const Clip* source = project.findClip(clipId, &sourceTrack);
        Track* target = project.findTrack(targetTrackId);
        if (source == nullptr || sourceTrack == nullptr || target == nullptr)
        {
            fail("clip or target track not found");
            return false;
        }
        if (target->id == sourceTrack->id)
        {
            // Same-track move: just reposition.
            Clip* mutableClip = project.findClip(clipId);
            mutableClip->startSeconds = std::max(0.0, startSeconds);
            return true;
        }
        if (target->type != sourceTrack->type)
        {
            fail("cannot move a clip between different track types");
            return false;
        }
        Clip moved = *source;
        moved.startSeconds = std::max(0.0, startSeconds);
        Track* mutableSource = project.findTrack(sourceTrack->id);
        auto& clips = mutableSource->clips;
        clips.erase(std::remove_if(clips.begin(), clips.end(),
                                   [clipId](const Clip& c) { return c.id == clipId; }),
                    clips.end());
        target->clips.push_back(std::move(moved));
        return true;
    });
}

juce::Result Engine::moveClips(const std::vector<ClipId>& clipIds,
                               const std::vector<TrackId>& targetTrackIds,
                               const std::vector<double>& startSeconds)
{
    if (clipIds.size() != targetTrackIds.size()
        || clipIds.size() != startSeconds.size())
        return juce::Result::fail("moveClips: array size mismatch");

    if (clipIds.empty())
        return juce::Result::ok();

    return editProject([&] (Project& project) {
        struct Move
        {
            Clip clip;
            TrackId sourceTrackId = 0;
            TrackId targetTrackId = 0;
            double startSeconds = 0.0;
        };

        std::vector<Move> moves;
        moves.reserve(clipIds.size());

        // Validate the whole group before touching the project.
        for (size_t i = 0; i < clipIds.size(); ++i)
        {
            const Track* sourceTrack = nullptr;
            const Clip* source = project.findClip(clipIds[i], &sourceTrack);
            Track* target = project.findTrack(targetTrackIds[i]);
            if (source == nullptr || sourceTrack == nullptr || target == nullptr)
            {
                fail("clip or target track not found");
                return false;
            }
            if (! std::isfinite(startSeconds[i]))
            {
                fail("invalid clip start");
                return false;
            }
            if (target->id != sourceTrack->id && target->type != sourceTrack->type)
            {
                fail("cannot move a clip between different track types");
                return false;
            }
            moves.push_back({ *source, sourceTrack->id, target->id,
                              std::max(0.0, startSeconds[i]) });
        }

        // Same-track moves only change the start; cross-track moves are
        // applied afterwards so the source vectors stay valid while we read
        // the validated copies above.
        for (auto& move : moves)
        {
            if (move.sourceTrackId != move.targetTrackId)
                continue;
            if (Clip* clip = project.findClip(move.clip.id))
                clip->startSeconds = move.startSeconds;
        }

        for (auto& move : moves)
        {
            if (move.sourceTrackId == move.targetTrackId)
                continue;

            Track* sourceTrack = project.findTrack(move.sourceTrackId);
            if (sourceTrack == nullptr)
                continue;
            auto& clips = sourceTrack->clips;
            clips.erase(std::remove_if(clips.begin(), clips.end(),
                                       [&move] (const Clip& c)
                                       { return c.id == move.clip.id; }),
                        clips.end());

            Clip moved = move.clip;
            moved.startSeconds = move.startSeconds;
            if (Track* target = project.findTrack(move.targetTrackId))
                target->clips.push_back(std::move(moved));
        }
        return true;
    });
}

juce::Result Engine::splitClipAt(ClipId clipId, double timeSeconds)
{
    return editProject([&] (Project& project) {
        const Track* ownerTrack = nullptr;
        const Clip* source = project.findClip(clipId, &ownerTrack);
        if (source == nullptr || ownerTrack == nullptr)
        {
            fail("clip not found: " + juce::String((int) clipId));
            return false;
        }
        const double rel = timeSeconds - source->startSeconds;
        if (rel <= kMinClipLength || rel >= source->lengthSeconds - kMinClipLength)
        {
            fail("split position is outside the clip");
            return false;
        }

        Track* mutableTrack = project.findTrack(ownerTrack->id);
        Clip left = *source;
        Clip right = *source;
        right.id = project.nextClipId++;

        if (source->isMidi())
        {
            auto leftMidi = std::make_shared<MidiClipData>();
            auto rightMidi = std::make_shared<MidiClipData>();
            for (const auto& note : source->midi->notes)
            {
                if (note.startSeconds >= rel)
                {
                    rightMidi->notes.push_back(
                        { note.startSeconds - rel, note.lengthSeconds,
                          note.pitch, note.velocity, note.startSeconds - rel });
                }
                else
                {
                    // Crossing notes are truncated on the left side; the
                    // sustaining tail lives on in the right clip.
                    const double len = std::min(note.lengthSeconds, rel - note.startSeconds);
                    leftMidi->notes.push_back(
                        { note.startSeconds, len, note.pitch, note.velocity,
                          note.startSeconds });
                    rightMidi->notes.push_back(
                        { 0.0, note.lengthSeconds - (rel - note.startSeconds),
                          note.pitch, note.velocity, 0.0 });
                }
            }
            left.midi = std::move(leftMidi);
            right.midi = std::move(rightMidi);
        }
        else
        {
            right.sourceOffsetSeconds = source->sourceOffsetSeconds + rel;
        }

        left.lengthSeconds = rel;
        left.fadeOutSeconds = 0.0f;
        right.startSeconds = source->startSeconds + rel;
        right.lengthSeconds = source->lengthSeconds - rel;
        right.fadeInSeconds = 0.0f;

        for (auto& track : project.tracks)
        {
            if (track.id != ownerTrack->id)
                continue;
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                    clip = left;
            track.clips.push_back(right);
            break;
        }
        return true;
    });
}

juce::Result Engine::setTrackName(TrackId trackId, const juce::String& name)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->name = name;
        return true;
    });
}

juce::Result Engine::setTrackColor(TrackId trackId, int colorIndex)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->colorIndex = juce::jlimit(0, 15, colorIndex);
        return true;
    });
}

ClipId Engine::addMidiClip(TrackId trackId, double startSeconds, double lengthSeconds)
{
    const std::scoped_lock lock(editMutex_);
    if (std::atomic_load(&project_) == nullptr)
        publish(makeEmptyProjectLocked(defaultRate()));

    auto project = copyForEdit();
    Track* track = project->findTrack(trackId);
    if (track == nullptr)
    {
        fail("track not found: " + juce::String((int) trackId));
        return -1;
    }

    Clip clip;
    clip.id = project->nextClipId++;
    clip.startSeconds = std::max(0.0, startSeconds);
    clip.lengthSeconds = std::max(kMinClipLength, lengthSeconds);
    clip.midi = std::make_shared<MidiClipData>();
    const ClipId newId = clip.id;
    track->clips.push_back(std::move(clip));

    pushUndoLocked();
    publish(std::move(project));
    return newId;
}

juce::Result Engine::midiClipAddNote(ClipId clipId, int pitch, float velocity,
                                     double startInClipSeconds, double lengthSeconds)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr)
        {
            fail("clip not found: " + juce::String((int) clipId));
            return false;
        }
        if (! clip->isMidi())
        {
            fail("not a MIDI clip");
            return false;
        }
        auto midi = std::make_shared<MidiClipData>(*clip->midi); // copy-on-write
        MidiNote note;
        note.pitch = juce::jlimit(0, 127, pitch);
        note.velocity = juce::jlimit(0.0f, 1.0f, velocity);
        note.startSeconds = std::max(0.0, startInClipSeconds);
        note.lengthSeconds = std::max(0.01, lengthSeconds);
        note.origStartSeconds = note.startSeconds;
        midi->notes.push_back(note);
        clip->midi = std::move(midi);
        return true;
    });
}

juce::Result Engine::midiClipRemoveNote(ClipId clipId, int noteIndex)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr || ! clip->isMidi())
        {
            fail("midi clip not found: " + juce::String((int) clipId));
            return false;
        }
        auto midi = std::make_shared<MidiClipData>(*clip->midi);
        if (noteIndex < 0 || noteIndex >= (int) midi->notes.size())
        {
            fail("note index out of range");
            return false;
        }
        midi->notes.erase(midi->notes.begin() + noteIndex);
        clip->midi = std::move(midi);
        return true;
    });
}

juce::Result Engine::midiClipSetNote(ClipId clipId, int noteIndex, int pitch,
                                     double startInClipSeconds, double lengthSeconds)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr || ! clip->isMidi())
        {
            fail("midi clip not found: " + juce::String((int) clipId));
            return false;
        }
        auto midi = std::make_shared<MidiClipData>(*clip->midi);
        if (noteIndex < 0 || noteIndex >= (int) midi->notes.size())
        {
            fail("note index out of range");
            return false;
        }
        auto& note = midi->notes[(size_t) noteIndex];
        note.pitch = juce::jlimit(0, 127, pitch);
        note.startSeconds = std::max(0.0, startInClipSeconds);
        note.lengthSeconds = std::max(0.01, lengthSeconds);
        note.origStartSeconds = note.startSeconds; // manual move bakes the reference
        clip->midi = std::move(midi);
        return true;
    });
}

juce::Result Engine::midiClipSetNotes(ClipId clipId,
                                      const std::vector<MidiNoteEdit>& edits)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr || ! clip->isMidi() || clip->midi == nullptr)
        {
            fail("midi clip not found");
            return false;
        }
        for (const auto& edit : edits)
        {
            if (edit.index < 0
                || edit.index >= (int) clip->midi->notes.size())
            {
                fail("note index out of range");
                return false;
            }
            if (! std::isfinite(edit.startSeconds)
                || ! std::isfinite(edit.lengthSeconds))
            {
                fail("invalid note position");
                return false;
            }
        }
        auto midi = std::make_shared<MidiClipData>(*clip->midi); // copy-on-write
        for (const auto& edit : edits)
        {
            auto& note = midi->notes[(size_t) edit.index];
            note.pitch = juce::jlimit(0, 127, edit.pitch);
            note.startSeconds = std::max(0.0, edit.startSeconds);
            note.lengthSeconds = std::max(0.01, edit.lengthSeconds);
            note.origStartSeconds = note.startSeconds; // manual move bakes the reference
        }
        clip->midi = std::move(midi);
        return true;
    });
}

juce::Result Engine::quantizeClipNotes(ClipId clipId, double gridSeconds,
                                       double swing, double catchRange,
                                       double maxMove, bool nonGridProtect,
                                       bool iterative, double iterStrength)
{
    if (gridSeconds <= 0.0)
        return fail("quantize grid must be positive");
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr || ! clip->isMidi() || clip->midi == nullptr)
        {
            fail("midi clip not found: " + juce::String((int) clipId));
            return false;
        }
        auto midi = std::make_shared<MidiClipData>(*clip->midi); // copy-on-write
        for (auto& note : midi->notes)
        {
            const double orig = (std::abs(note.origStartSeconds
                                       - note.startSeconds) > 1e-9)
                ? note.origStartSeconds   // re-quantize from the reference
                : note.startSeconds;

            const long idx = std::lround(orig / gridSeconds);
            double target = (double) idx * gridSeconds;
            if (swing > 0.0 && (idx % 2) != 0)
                target += gridSeconds * juce::jlimit(0.0, 1.0, swing);
            target = std::max(0.0, target);

            const double delta = target - orig;
            if (nonGridProtect
                && std::abs(std::abs(delta) - gridSeconds * 0.5) < 1e-6)
                continue; // sits exactly between two grid lines
            if (std::abs(delta) <= gridSeconds * juce::jlimit(0.0, 1.0, catchRange))
                continue; // already close enough to the grid
            if (std::abs(delta) > gridSeconds * juce::jlimit(0.0, 1.0, maxMove))
                continue; // too far from the grid to be pulled

            note.startSeconds = iterative
                ? std::max(0.0, note.startSeconds + delta
                                    * juce::jlimit(0.0, 1.0, iterStrength))
                : std::max(0.0, target);
        }
        clip->midi = std::move(midi);
        return true;
    });
}

juce::Result Engine::resetClipQuantize(ClipId clipId)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr || ! clip->isMidi() || clip->midi == nullptr)
        {
            fail("midi clip not found: " + juce::String((int) clipId));
            return false;
        }
        auto midi = std::make_shared<MidiClipData>(*clip->midi);
        for (auto& note : midi->notes)
            note.startSeconds = note.origStartSeconds;
        clip->midi = std::move(midi);
        return true;
    });
}

juce::Result Engine::freezeClipQuantize(ClipId clipId)
{
    return editProject([&] (Project& project) {
        Clip* clip = project.findClip(clipId);
        if (clip == nullptr || ! clip->isMidi() || clip->midi == nullptr)
        {
            fail("midi clip not found: " + juce::String((int) clipId));
            return false;
        }
        auto midi = std::make_shared<MidiClipData>(*clip->midi);
        for (auto& note : midi->notes)
            note.origStartSeconds = note.startSeconds;
        clip->midi = std::move(midi);
        return true;
    });
}

// ---------- AI clip generation ----------

void Engine::setAiModelDir(const juce::File& dir)
{
    const std::scoped_lock lock(aiMutex_);
    if (aiState_.load(std::memory_order_acquire) == kAiRunning)
        return; // a model swap would pull the rug from under the worker
    if (aiService_ == nullptr)
        aiService_ = std::make_unique<ai::Service>();
    aiService_->setModelDir(dir);
}

juce::File Engine::aiModelDir() const
{
    const std::scoped_lock lock(aiMutex_);
    return aiService_ != nullptr ? aiService_->modelDir() : ai::defaultModelDir();
}

bool Engine::aiModelAvailable(AiMode mode) const
{
    const std::scoped_lock lock(aiMutex_);
    return aiService_ != nullptr
        && aiService_->modelAvailable(static_cast<ai::Mode>((int) mode));
}

const char* Engine::aiModelDirUtf8() const
{
    const std::scoped_lock lock(aiMutex_);
    aiStringOut_ = (aiService_ != nullptr ? aiService_->modelDir()
                                          : ai::defaultModelDir())
                       .getFullPathName()
                       .toStdString();
    return aiStringOut_.c_str();
}

bool Engine::startAiGeneration(const AiGenerationRequest& request)
{
    auto project = std::atomic_load(&project_);
    if (project == nullptr)
        return false;

    {
        const std::scoped_lock lock(aiMutex_);
        if (aiState_.load(std::memory_order_acquire) == kAiRunning)
            return false;
    }
    // Join the previous worker OUTSIDE the lock: it takes the same lock when
    // it publishes its result, so joining while holding it would deadlock.
    if (aiThread_.joinable())
        aiThread_.join();

    {
        const std::scoped_lock lock(aiMutex_);
        aiResult_ = {};
        aiCancel_.store(false, std::memory_order_release);
        aiState_.store(kAiRunning, std::memory_order_release);
    }

    aiThread_ = std::thread([this, project, request] {
        runAiGeneration(project, request);
    });
    return true;
}

void Engine::runAiGeneration(std::shared_ptr<const Project> project,
                             AiGenerationRequest request)
{
    AiGenerationResult result;
    result.trackId = request.trackId;
    result.clipId = request.clipId;
    result.replaceClip = request.replaceClip;

    try
    {
        if (request.trackId < 0)
        {
            result.error = "no track selected";
        }
        else if (auto* track = project->findTrack(request.trackId);
                 track == nullptr || track->type != Track::Type::Instrument)
        {
            result.error = "AI generation needs an instrument track";
        }
        else
        {
            ai::Request ar;
            ar.trackId = request.trackId;
            ar.clipId = request.clipId;
            ar.anchorSeconds = request.anchorSeconds;
            ar.mode = static_cast<ai::Mode>((int) request.mode);
            ar.bars = request.bars;
            ar.lengthSeconds = request.lengthSeconds;
            ar.temperature = request.temperature;
            ar.variations = request.variations;
            ar.seed = request.seed;
            ar.prompt = request.prompt;
            ar.replaceClip = request.replaceClip;
            ar.harmonize = request.harmonize;
            ar.useModel = request.useModel;

            auto r = aiService_->generate(*project, ar);
            if (! aiCancel_.load(std::memory_order_acquire))
            {
                result.ok = r.ok;
                result.error = r.error;
                result.provider = r.provider;
                result.key = r.key;
                result.chords = r.chords;
                result.promptNote = r.promptNote;
                result.contextNote = r.contextNote;
                result.seed = r.seed;
                result.clipLengthSeconds = r.clipLengthSeconds;
                result.startSeconds = r.startSeconds;
                if (request.replaceClip && request.clipId > 0)
                    if (const auto* clip = project->findClip(request.clipId))
                        result.startSeconds = clip->startSeconds;

                result.notes.reserve(r.notes.size());
                for (const auto& n : r.notes)
                {
                    MidiNote note;
                    note.pitch = juce::jlimit(0, 127, n.pitch);
                    note.velocity = juce::jlimit(0.01f, 1.0f, n.velocity);
                    note.startSeconds = juce::jmax(0.0, n.startSeconds);
                    note.lengthSeconds = juce::jmax(0.01, n.lengthSeconds);
                    note.origStartSeconds = note.startSeconds;
                    result.notes.push_back(note);
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        result.ok = false;
        result.error = juce::String("generation failed: ") + e.what();
    }
    catch (...)
    {
        result.ok = false;
        result.error = "generation failed";
    }

    const std::scoped_lock lock(aiMutex_);
    if (aiCancel_.load(std::memory_order_acquire))
        aiResult_ = {};
    else
        aiResult_ = std::move(result);

    const int state = aiResult_.ok ? kAiReady
                    : (aiResult_.error.isNotEmpty() ? kAiFailed : kAiIdle);
    aiState_.store(state, std::memory_order_release);
}

AiGenerationResult Engine::aiGenerationResult() const
{
    const std::scoped_lock lock(aiMutex_);
    return aiResult_;
}

void Engine::clearAiGenerationResult()
{
    const std::scoped_lock lock(aiMutex_);
    if (aiState_.load(std::memory_order_acquire) == kAiRunning)
        return;
    aiResult_ = {};
    aiState_.store(kAiIdle, std::memory_order_release);
}

void Engine::cancelAiGeneration()
{
    aiCancel_.store(true, std::memory_order_release);
}

const char* Engine::aiResultString(int field) const
{
    const std::scoped_lock lock(aiMutex_);
    switch (field)
    {
        case 0: aiStringOut_ = aiResult_.error.toStdString(); break;
        case 1: aiStringOut_ = aiResult_.provider.toStdString(); break;
        case 2: aiStringOut_ = aiResult_.key.toStdString(); break;
        case 3: aiStringOut_ = aiResult_.chords.toStdString(); break;
        case 4: aiStringOut_ = aiResult_.promptNote.toStdString(); break;
        case 5: aiStringOut_ = aiResult_.contextNote.toStdString(); break;
        default: aiStringOut_.clear(); break;
    }
    return aiStringOut_.c_str();
}

juce::Result Engine::applyAiGeneration(ClipId* outClipId)
{
    const auto result = aiGenerationResult();
    if (! result.ok)
        return fail(result.error.isNotEmpty() ? result.error
                                              : juce::String("no AI result to apply"));

    ClipId newClipId = -1;
    const auto edit = editProject([&] (Project& project) {
        Track* track = project.findTrack(result.trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) result.trackId));
            return false;
        }
        if (track->type != Track::Type::Instrument)
        {
            fail("AI generation needs an instrument track");
            return false;
        }

        double length = result.clipLengthSeconds;
        for (const auto& n : result.notes)
            length = juce::jmax(length, n.startSeconds + n.lengthSeconds);
        length = juce::jmax(kMinClipLength, length);

        auto midi = std::make_shared<MidiClipData>();
        midi->notes.reserve(result.notes.size());
        for (const auto& n : result.notes)
        {
            MidiNote note;
            note.pitch = juce::jlimit(0, 127, n.pitch);
            note.velocity = juce::jlimit(0.01f, 1.0f, n.velocity);
            note.startSeconds = juce::jlimit(0.0, juce::jmax(0.0, length - 0.01),
                                             n.startSeconds);
            note.lengthSeconds = juce::jlimit(
                0.01, juce::jmax(0.01, length - note.startSeconds),
                n.lengthSeconds);
            note.origStartSeconds = note.startSeconds;
            midi->notes.push_back(note);
        }

        if (result.replaceClip && result.clipId > 0)
        {
            Clip* clip = project.findClip(result.clipId);
            if (clip == nullptr || ! clip->isMidi())
            {
                fail("MIDI clip not found");
                return false;
            }
            clip->midi = std::move(midi);
            clip->lengthSeconds = juce::jmax(clip->lengthSeconds, length);
            newClipId = clip->id;
        }
        else
        {
            Clip clip;
            clip.id = project.nextClipId++;
            clip.startSeconds = juce::jmax(0.0, result.startSeconds);
            clip.lengthSeconds = length;
            clip.midi = std::move(midi);
            newClipId = clip.id;
            track->clips.push_back(std::move(clip));
        }
        return true;
    });

    if (edit.failed())
        return edit;

    if (outClipId != nullptr)
        *outClipId = newClipId;

    // The applied result is consumed: the next generation starts clean and a
    // second Apply cannot duplicate the clip.
    const std::scoped_lock lock(aiMutex_);
    aiResult_ = {};
    aiState_.store(kAiIdle, std::memory_order_release);
    return juce::Result::ok();
}

double Engine::clipNoteOrigStartAt(ClipId clipId, int index) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            if (clip->midi != nullptr
                && index >= 0 && index < (int) clip->midi->notes.size())
                return clip->midi->notes[(size_t) index].origStartSeconds;
    return 0.0;
}

bool Engine::clipIsMidi(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            return clip->isMidi();
    return false;
}

int Engine::clipNoteCount(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            if (clip->midi != nullptr)
                return (int) clip->midi->notes.size();
    return 0;
}

void Engine::clipNoteAt(ClipId clipId, int index, int* pitch, float* velocity,
                        double* startSeconds, double* lengthSeconds) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            if (clip->midi != nullptr && index >= 0
                && index < (int) clip->midi->notes.size())
            {
                const auto& note = clip->midi->notes[(size_t) index];
                if (pitch != nullptr) *pitch = note.pitch;
                if (velocity != nullptr) *velocity = note.velocity;
                if (startSeconds != nullptr) *startSeconds = note.startSeconds;
                if (lengthSeconds != nullptr) *lengthSeconds = note.lengthSeconds;
            }
}

juce::Result Engine::setTrackVolume(TrackId trackId, float linearGain)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->volume = juce::jlimit(0.0f, 2.0f, linearGain);
        return true;
    });
}

AutomationCurvePtr Engine::trackLaneAutomation(TrackId trackId,
                                               int lane) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
        {
            switch (lane)
            {
                case 0: return track->volumeAutomation;
                case 1: return track->panAutomation;
                case 2: return track->sustainAutomation;
            }
        }
    return nullptr;
}

juce::Result Engine::setTrackLaneAutomation(
    TrackId trackId, int lane, const std::vector<AutomationPoint>& points)
{
    if (lane < 0 || lane > 2)
        return fail("automation lane no válida");
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        auto curve = std::make_shared<AutomationCurve>();
        curve->points.reserve(points.size());
        for (const auto& point : points)
        {
            if (! std::isfinite(point.timeSeconds)
                || ! std::isfinite(point.value))
                continue;
            curve->points.push_back(
                { std::max(0.0, point.timeSeconds),
                  juce::jlimit(0.0, 1.0, point.value) });
        }
        std::sort(curve->points.begin(), curve->points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) {
                      return a.timeSeconds < b.timeSeconds;
                  });
        switch (lane)
        {
            case 0: track->volumeAutomation = std::move(curve); break;
            case 1: track->panAutomation = std::move(curve); break;
            case 2: track->sustainAutomation = std::move(curve); break;
        }
        return true;
    });
}

juce::Result Engine::setTrackVolumeAutomation(
    TrackId trackId, const std::vector<AutomationPoint>& points)
{
    return setTrackLaneAutomation(trackId, 0, points);
}

int Engine::trackVolumeAutoCount(TrackId trackId) const
{
    return trackLaneAutomationCount(trackId, 0);
}

AutomationPoint Engine::trackVolumeAutoAt(TrackId trackId, int index) const
{
    return trackLaneAutomationAt(trackId, 0, index);
}

int Engine::trackLaneAutomationCount(TrackId trackId, int lane) const
{
    if (auto curve = trackLaneAutomation(trackId, lane))
        return (int) curve->points.size();
    return 0;
}

AutomationPoint Engine::trackLaneAutomationAt(TrackId trackId, int lane,
                                              int index) const
{
    if (auto curve = trackLaneAutomation(trackId, lane))
        if (index >= 0 && index < (int) curve->points.size())
            return curve->points[(size_t) index];
    return {};
}

juce::Result Engine::setTrackParamAutomation(TrackId trackId, int slotKind,
                                             int slotIndex, int paramIndex,
                                             const juce::String& name,
                                             const std::vector<AutomationPoint>& points)
{
    if (slotKind != 0 && slotKind != 1)
        return fail("slot no válido");
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        auto curve = std::make_shared<AutomationCurve>();
        for (const auto& point : points)
        {
            if (! std::isfinite(point.timeSeconds)
                || ! std::isfinite(point.value))
                continue;
            curve->points.push_back(
                { std::max(0.0, point.timeSeconds),
                  juce::jlimit(0.0, 1.0, point.value) });
        }
        std::sort(curve->points.begin(), curve->points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) {
                      return a.timeSeconds < b.timeSeconds;
                  });

        for (auto& lane : track->paramLanes)
        {
            if (lane.slotKind == slotKind && lane.slotIndex == slotIndex
                && lane.paramIndex == paramIndex)
            {
                lane.curve = std::move(curve);
                lane.name = name;
                return true;
            }
        }
        track->paramLanes.push_back(
            { slotKind, slotIndex, paramIndex, name, std::move(curve) });
        return true;
    });
}

int Engine::trackParamLaneCount(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return (int) track->paramLanes.size();
    return 0;
}

juce::Result Engine::trackParamLaneInfo(TrackId trackId, int laneIndex,
                                        int* slotKind, int* slotIndex,
                                        int* paramIndex) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            if (laneIndex >= 0 && laneIndex < (int) track->paramLanes.size())
            {
                const auto& lane = track->paramLanes[(size_t) laneIndex];
                if (slotKind != nullptr) *slotKind = lane.slotKind;
                if (slotIndex != nullptr) *slotIndex = lane.slotIndex;
                if (paramIndex != nullptr) *paramIndex = lane.paramIndex;
                return juce::Result::ok();
            }
    return fail("param lane not found");
}

const char* Engine::trackParamLaneName(TrackId trackId, int laneIndex) const
{
    stringOut_.clear();
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            if (laneIndex >= 0 && laneIndex < (int) track->paramLanes.size())
                stringOut_ = track->paramLanes[(size_t) laneIndex]
                                 .name.toStdString();
    return stringOut_.c_str();
}

int Engine::trackParamLanePointCount(TrackId trackId, int laneIndex) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            if (laneIndex >= 0 && laneIndex < (int) track->paramLanes.size())
                if (track->paramLanes[(size_t) laneIndex].curve != nullptr)
                    return (int) track->paramLanes[(size_t) laneIndex]
                               .curve->points.size();
    return 0;
}

AutomationPoint Engine::trackParamLanePointAt(TrackId trackId, int laneIndex,
                                              int pointIndex) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            if (laneIndex >= 0 && laneIndex < (int) track->paramLanes.size())
            {
                const auto& curve = track->paramLanes[(size_t) laneIndex].curve;
                if (curve != nullptr && pointIndex >= 0
                    && pointIndex < (int) curve->points.size())
                    return curve->points[(size_t) pointIndex];
            }
    return {};
}

juce::Result Engine::removeTrackParamLane(TrackId trackId, int laneIndex)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr
            || laneIndex < 0
            || laneIndex >= (int) track->paramLanes.size())
        {
            fail("param lane not found");
            return false;
        }
        track->paramLanes.erase(track->paramLanes.begin() + laneIndex);
        return true;
    });
}

int Engine::pluginParamCount(TrackId trackId, int slotKind, int slotIndex) const
{
    std::shared_ptr<juce::AudioProcessor> proc;
    if (slotKind == 0)
        proc = transport_->fxProcessorFor(trackId, slotIndex);
    else if (slotKind == 1)
        proc = transport_->instrumentProcessorFor(trackId);
    if (proc == nullptr)
        return 0;
    return proc->getParameters().size();
}

const char* Engine::pluginParamName(TrackId trackId, int slotKind,
                                    int slotIndex, int paramIndex) const
{
    stringOut_.clear();
    std::shared_ptr<juce::AudioProcessor> proc;
    if (slotKind == 0)
        proc = transport_->fxProcessorFor(trackId, slotIndex);
    else if (slotKind == 1)
        proc = transport_->instrumentProcessorFor(trackId);
    if (proc != nullptr)
        if (const auto& params = proc->getParameters();
            paramIndex >= 0 && paramIndex < (int) params.size())
            if (auto* p = params[(size_t) paramIndex])
                stringOut_ = p->getName(64).toStdString();
    return stringOut_.c_str();
}

float Engine::trackVolumeEffectiveAt(TrackId trackId,
                                     double timeSeconds) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return volumeAt(*track, track->volumeAutomation.get(),
                            (std::int64_t) std::llround(timeSeconds
                                                        * project->sampleRate),
                            project->sampleRate);
    return 0.0f;
}

juce::Result Engine::setTrackVolumeAutomationPointAt(TrackId trackId,
                                                     double timeSeconds,
                                                     double value)
{
    if (! std::isfinite(timeSeconds) || ! std::isfinite(value))
        return fail("valores de automatización no válidos");
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        auto curve = track->volumeAutomation != nullptr
            ? std::make_shared<AutomationCurve>(*track->volumeAutomation)
            : std::make_shared<AutomationCurve>();
        constexpr double matchEps = 0.001; // 1 ms replaces the same point
        bool replaced = false;
        for (auto& point : curve->points)
        {
            if (std::abs(point.timeSeconds - timeSeconds) < matchEps)
            {
                point.value = juce::jlimit(0.0, 1.0, value);
                replaced = true;
                break;
            }
        }
        if (! replaced)
            curve->points.push_back({ std::max(0.0, timeSeconds),
                                      juce::jlimit(0.0, 1.0, value) });
        std::sort(curve->points.begin(), curve->points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) {
                      return a.timeSeconds < b.timeSeconds;
                  });
        track->volumeAutomation = std::move(curve);
        return true;
    });
}

double Engine::inputQuantizeSeconds() const
{
    if (auto project = std::atomic_load(&project_))
        return project->inputQuantizeSeconds;
    return 0.0;
}

juce::Result Engine::setInputQuantize(double gridSeconds)
{
    if (! std::isfinite(gridSeconds) || gridSeconds < 0.0)
        return fail("grid de input quantize no válido");
    return editProject([&] (Project& project) {
        project.inputQuantizeSeconds = gridSeconds;
        return true;
    });
}

juce::Result Engine::setTrackPan(TrackId trackId, float pan)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->pan = juce::jlimit(-1.0f, 1.0f, pan);
        return true;
    });
}

juce::Result Engine::setTrackMute(TrackId trackId, bool muted)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->mute = muted;
        return true;
    });
}

juce::Result Engine::setTrackSolo(TrackId trackId, bool soloed)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->solo = soloed;
        return true;
    });
}

juce::Result Engine::setMasterGain(float linearGain)
{
    return editProject([&] (Project& project) {
        project.masterGain = juce::jlimit(0.0f, 4.0f, linearGain);
        return true;
    });
}

void Engine::setMetronome(bool on)
{
    transport_->setMetronome(on);
}

juce::Result Engine::setTrackArmed(TrackId trackId, bool armed)
{
    // Arming is workflow state, not an edit: no undo entry.
    const std::scoped_lock lock(editMutex_);
    auto project = copyForEdit();
    Track* track = project->findTrack(trackId);
    if (track == nullptr)
        return fail("track not found: " + juce::String((int) trackId));
    track->armed = armed;
    publish(std::move(project));
    return juce::Result::ok();
}

int Engine::trackCount() const
{
    if (auto project = std::atomic_load(&project_))
        return (int) project->tracks.size();
    return 0;
}

juce::Result Engine::setTrackInputChannel(TrackId trackId, int channel)
{
    const std::scoped_lock lock(editMutex_);
    auto project = copyForEdit();
    Track* track = project->findTrack(trackId);
    if (track == nullptr)
        return fail("track not found: " + juce::String((int) trackId));
    track->inputChannel = juce::jlimit(0, 1, channel);
    publish(std::move(project));
    return juce::Result::ok();
}

bool Engine::trackArmed(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->armed;
    return false;
}

int Engine::trackInputChannel(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->inputChannel;
    return 0;
}

juce::Result Engine::startRecord()
{
    const std::scoped_lock lock(editMutex_);

    if (recording_.load())
        return juce::Result::ok();

    if (! isInitialised())
        return fail("audio device not initialised");

    auto project = std::atomic_load(&project_);
    if (project == nullptr)
        return fail("no project");

    // The app launches output-only so a wedged input driver cannot freeze
    // startup. Open the remembered input only when the user actually records
    // an audio track.
    bool anyAudioArmed = false;
    for (const auto& track : project->tracks)
        if (track.armed && track.type == Track::Type::Audio)
        {
            anyAudioArmed = true;
            break;
        }
    if (anyAudioArmed)
    {
        const auto inputResult = openInputChannels();
        if (inputResult.failed())
            return inputResult;
    }

    auto list = std::make_shared<CaptureList>();
    const int capacity = (int) (300.0 * project->sampleRate); // 5 min per track
    const double captureRate = deviceManager_.getCurrentAudioDevice() != nullptr
                                   ? deviceManager_.getCurrentAudioDevice()->getCurrentSampleRate()
                                   : project->sampleRate;

    bool anyMidiArmed = false;
    bool anyGeneratorArmed = false;
    for (const auto& track : project->tracks)
    {
        if (! track.armed)
            continue;
        if (track.type == Track::Type::Instrument)
        {
            anyMidiArmed = true;
            // Self-generating instruments (EZDrums) record WITHOUT a MIDI
            // keyboard: the plugin performs its pattern into the take.
            if (track.instrument.pluginUid.containsIgnoreCase("ezdrums"))
                anyGeneratorArmed = true;
        }
        else if (deviceInputChannelCount() == 0)
            return fail("el dispositivo activo no tiene entradas — elige uno con "
                        "micrófono en Preferencias (engranaje)");
        list->push_back(std::make_shared<TrackCapture>(
            track.id, juce::jlimit(0, 1, track.inputChannel), captureRate, capacity,
            track.type == Track::Type::Instrument));
    }
    if (list->empty())
        return fail("no armed tracks to record");
    if (anyMidiArmed && ! anyGeneratorArmed
        && selectedMidiInputId_.isEmpty()
        && ! debugLoopback_.load())
        return fail("no hay entrada MIDI seleccionada — elige una en "
                    "Preferencias (engranaje)");

    recordStartSeconds_.store(transport_->positionSeconds());
    recordStartSamples_.store(transport_->positionSamples());
    std::atomic_store(&captures_, std::static_pointer_cast<const CaptureList>(list));
    recording_.store(true);
    transport_->setRecording(true, recordStartSamples_.load(), captures_);
    transport_->play();
    return juce::Result::ok();
}

juce::Result Engine::stopRecord()
{
    finalizeRecording();
    transport_->pause();
    // Every DAW returns the playhead to the take's start on stop: playing
    // from the post-record position would sit PAST the new clip and the
    // next play auto-stops instantly at the (old) project end.
    seek(recordStartSeconds_.load());
    return juce::Result::ok();
}

void Engine::debugPushLiveMidi(int pitch, bool noteOn)
{
    // Through onMidiMessage — the same path real CoreMIDI input takes —
    // so tests exercise monitoring AND capture, not just monitoring.
    onMidiMessage(noteOn
        ? juce::MidiMessage::noteOn(1, pitch, 0.8f)
        : juce::MidiMessage::noteOff(1, pitch));
}

double Engine::tempoBpm() const
{
    if (auto project = std::atomic_load(&project_))
        return project->tempoBpm;
    return 120.0;
}

juce::Result Engine::setTempo(double bpm)
{
    if (! std::isfinite(bpm))
        return fail("tempo no válido");
    return editProject([&] (Project& project) {
        project.tempoBpm = juce::jlimit(20.0, 300.0, bpm);
        return true;
    });
}

int Engine::timeSigNumerator() const
{
    if (auto project = std::atomic_load(&project_))
        return project->timeSigNumerator;
    return 4;
}

int Engine::timeSigDenominator() const
{
    if (auto project = std::atomic_load(&project_))
        return project->timeSigDenominator;
    return 4;
}

juce::Result Engine::setTimeSignature(int numerator, int denominator)
{
    const int allowed[] = { 1, 2, 4, 8, 16, 32 };
    bool validDen = false;
    for (int d : allowed)
        validDen = validDen || d == denominator;
    if (! validDen)
        return fail("denominador de compás no válido");
    return editProject([&] (Project& project) {
        project.timeSigNumerator = juce::jlimit(1, 12, numerator);
        project.timeSigDenominator = denominator;
        return true;
    });
}

void Engine::finalizeRecording()
{
    const std::scoped_lock lock(editMutex_);
    if (! recording_.exchange(false))
        return;
    transport_->setRecording(false, 0, nullptr);

    auto captures = std::atomic_exchange(
        &captures_, std::make_shared<const CaptureList>());
    if (captures == nullptr || captures->empty())
        return;

    auto current = std::atomic_load(&project_);
    if (current == nullptr)
        return;

    auto project = std::make_shared<Project>(*current);
    const double rate = project->sampleRate;
    const double startSeconds = recordStartSeconds_.load();
    bool anyClip = false;

    // Recordings land next to the project (or in ~/Music/Cadence/Recordings).
    juce::File recordingsDir = projectFile_.getFullPathName().isNotEmpty()
                                   ? projectFile_.getParentDirectory().getChildFile("Recordings")
                                   : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                         .getChildFile("Cadence/Recordings");
    recordingsDir.createDirectory();

    const auto now = juce::Time::getCurrentTime().toMilliseconds();

    for (const auto& capture : *captures)
    {
        if (capture->isMidi)
            continue; // finalized below as a MIDI clip

        const int written = capture->written.load();
        if (written <= 0)
            continue;

        // 1) Write the take to disk (24-bit mono WAV) at the DEVICE rate it
        // was captured at; loading it back resamples to the project rate.
        juce::File wavFile = recordingsDir.getChildFile(
            "rec_" + juce::String((int) now) + "_track" + juce::String((int) capture->trackId) + ".wav");
        wavFile.deleteFile();
        auto fileStream = wavFile.createOutputStream();
        if (fileStream == nullptr)
        {
            fail("cannot write recording: " + wavFile.getFullPathName());
            continue;
        }

        juce::AudioFormatWriterOptions options;
        options = options.withSampleRate(capture->sampleRate)
                         .withNumChannels(1)
                         .withBitsPerSample(24);
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
        auto writer = wavFormat.createWriterFor(stream, options);
        if (writer == nullptr)
        {
            fail("cannot create recording wav writer");
            continue;
        }
        writer->writeFromAudioSampleBuffer(capture->buffer, 0, written);
        writer.reset();
        if (stream != nullptr)
            stream->flush();

        // 2) Load it back as a regular source (same rate, straight read).
        auto data = std::make_shared<AudioSourceData>();
        if (loadAudioSourceData(formatManager_, wavFile, rate, *data).failed())
        {
            fail("cannot read back recording: " + wavFile.getFullPathName());
            continue;
        }

        // 3) Place the clip on the armed track, one undo entry for the take set.
        Track* track = project->findTrack(capture->trackId);
        if (track == nullptr)
            continue;

        const SourceId sourceId = project->nextSourceId++;
        project->sourceFiles[sourceId] = wavFile.getFullPathName();
        project->sources[sourceId] = std::move(data);

        Clip clip;
        clip.id = project->nextClipId++;
        clip.sourceId = sourceId;
        clip.startSeconds = startSeconds;
        clip.lengthSeconds = (double) written / rate;
        track->clips.push_back(std::move(clip));
        anyClip = true;
    }

    // MIDI takes: pair note on/off events into notes (timeline seconds).
    const double elapsed = std::max(0.0, transport_->positionSeconds()
                                            - recordStartSeconds_.load());

    for (const auto& capture : *captures)
    {
        if (! capture->isMidi)
            continue;
        Track* track = project->findTrack(capture->trackId);
        if (track == nullptr)
            continue;

        std::vector<TrackCapture::RecordedMidiEvent> events;
        {
            const std::scoped_lock lock(capture->midiMutex);
            events = capture->midiEvents;
        }
        std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            return a.sampleFromRecordStart < b.sampleFromRecordStart;
        });

        // Compensate MIDI input latency (keyboard + USB + CoreMIDI): events
        // are timestamped when they ARRIVE, so every note is written that
        // many samples late. Shift the whole take earlier.
        const std::int64_t latSamples = (std::int64_t) std::max(
            0.0, midiInputLatencyMs_ * rate / 1000.0);

        // Sustain pedal (CC64) capture → track automation lane (§7.12).
        std::vector<AutomationPoint> sustainPoints;
        double lastSustainVal = -1.0;
        for (const auto& e : events)
        {
            if ((e.status & 0xF0) != 0xB0 || e.d1 != 64)
                continue;
            const double t = recordStartSeconds_.load()
                + std::max<std::int64_t>(
                    0, (std::int64_t) e.sampleFromRecordStart - latSamples)
                / (double) rate;
            const double v = juce::jlimit(0.0, 1.0, e.d2 / 127.0);
            if (std::abs(v - lastSustainVal) < 1e-6)
                continue;
            sustainPoints.push_back({ t, v });
            lastSustainVal = v;
        }
        std::sort(sustainPoints.begin(), sustainPoints.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) {
                      return a.timeSeconds < b.timeSeconds;
                  });
        // A take that ends with the pedal still down must not sustain
        // forever: close the lane with an explicit pedal-up at the take end.
        if (! sustainPoints.empty() && lastSustainVal > 0.0)
            sustainPoints.push_back(
                { recordStartSeconds_.load() + elapsed, 0.0 });
        if (! sustainPoints.empty())
            track->sustainAutomation =
                std::make_shared<AutomationCurve>(
                    AutomationCurve{ sustainPoints });

        // Notes: the very same pairing the timeline uses to draw the take
        // live while recording (§7.11) — so what you see is what is written.
        auto midi = std::make_shared<MidiClipData>();
        midi->notes = pairCapturedNotes(*capture, elapsed);
        if (midi->notes.empty())
            continue;

        Clip clip;
        clip.id = project->nextClipId++;
        clip.startSeconds = recordStartSeconds_.load();
        clip.lengthSeconds = std::max(kMinClipLength, elapsed);
        clip.midi = std::move(midi);
        track->clips.push_back(std::move(clip));
        anyClip = true;
    }

    if (anyClip)
    {
        pushUndoLocked();
        publish(std::move(project));
    }
}

std::vector<MidiNote> Engine::pairCapturedNotes(const TrackCapture& capture,
                                                double elapsed) const
{
    auto project = std::atomic_load(&project_);
    const double rate = project != nullptr ? project->sampleRate : 48000.0;
    const double recStart = recordStartSeconds_.load();
    const std::int64_t elapsedSamples = (std::int64_t) (elapsed * rate);
    const std::int64_t minNoteSamples = (std::int64_t) (0.05 * rate);
    // Events are timestamped when they ARRIVE, so every note is written the
    // MIDI input latency late; shift the whole take earlier.
    const std::int64_t latSamples = (std::int64_t) std::max(
        0.0, midiInputLatencyMs_ * rate / 1000.0);
    // Input quantize (§7.5.2 v1.5): note starts snap to the grid, measured
    // against the project timeline (not the record start).
    const double iqGrid = project != nullptr ? project->inputQuantizeSeconds
                                             : 0.0;
    auto quantizeOn = [&](std::int64_t onSamples) -> std::int64_t {
        if (iqGrid <= 0.0)
            return onSamples;
        const double absSec = recStart + onSamples / (double) rate;
        const double quantized = std::round(absSec / iqGrid) * iqGrid;
        return std::max<std::int64_t>(
            0, (std::int64_t) std::llround((quantized - recStart) * rate));
    };

    std::vector<TrackCapture::RecordedMidiEvent> events;
    {
        const std::scoped_lock lock(capture.midiMutex);
        events = capture.midiEvents;
    }
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a.sampleFromRecordStart < b.sampleFromRecordStart;
    });

    std::vector<MidiNote> notes;
    // Open notes per pitch, FIFO: fast legato repeats press the same key
    // again before its note-off arrived — a single-slot map would silently
    // DROP the first note of every overlap.
    std::map<int, std::vector<std::pair<std::int64_t, float>>> openNotes;
    for (const auto& e : events)
    {
        const int type = e.status & 0xF0;
        const std::int64_t at = std::max<std::int64_t>(
            0, (std::int64_t) e.sampleFromRecordStart - latSamples);
        if (type == 0x90 && e.d2 > 0)
        {
            openNotes[e.d1].push_back({ at, e.d2 / 127.0f });
        }
        else if (type == 0x80 || (type == 0x90 && e.d2 == 0))
        {
            auto it = openNotes.find(e.d1);
            if (it == openNotes.end() || it->second.empty())
                continue;
            const auto [onRaw, vel] = it->second.front();
            it->second.erase(it->second.begin());
            const std::int64_t on = quantizeOn(onRaw);
            const std::int64_t off = std::min<std::int64_t>(at, elapsedSamples);
            const std::int64_t len = std::max(minNoteSamples, off - on);
            notes.push_back({ on / (double) rate, len / (double) rate,
                              e.d1, vel, on / (double) rate });
        }
    }
    // Keys still held down: at finalization that means the take ended with
    // them down; live, it means they are down right now and must keep
    // growing. Either way, close them at the current take length.
    for (const auto& [pitch, held] : openNotes)
    {
        for (const auto& [onRaw, vel] : held)
        {
            const std::int64_t onQ = quantizeOn(onRaw);
            const std::int64_t len = std::max(minNoteSamples,
                                              elapsedSamples - onQ);
            notes.push_back({ onQ / (double) rate, len / (double) rate,
                              pitch, vel, onQ / (double) rate });
        }
    }
    std::sort(notes.begin(), notes.end(),
              [](const MidiNote& a, const MidiNote& b) {
                  return a.startSeconds < b.startSeconds;
              });
    return notes;
}

int Engine::recordingNoteCount(TrackId trackId) const
{
    liveTakeTrack_ = trackId;
    liveTakeNotes_.clear();
    if (! recording_.load())
        return 0;
    auto captures = std::atomic_load(&captures_);
    if (captures == nullptr)
        return 0;
    for (const auto& capture : *captures)
    {
        if (! capture->isMidi || capture->trackId != trackId)
            continue;
        const double elapsed = std::max(
            0.0, transport_->positionSeconds() - recordStartSeconds_.load());
        liveTakeNotes_ = pairCapturedNotes(*capture, elapsed);
        break;
    }
    return (int) liveTakeNotes_.size();
}

void Engine::recordingNoteAt(TrackId trackId, int index, int* pitch,
                             float* velocity, double* startSeconds,
                             double* lengthSeconds) const
{
    if (trackId != liveTakeTrack_ || index < 0
        || index >= (int) liveTakeNotes_.size())
        return;
    const auto& note = liveTakeNotes_[(size_t) index];
    if (pitch != nullptr) *pitch = note.pitch;
    if (velocity != nullptr) *velocity = note.velocity;
    if (startSeconds != nullptr) *startSeconds = note.startSeconds;
    if (lengthSeconds != nullptr) *lengthSeconds = note.lengthSeconds;
}

TrackId Engine::trackIdAt(int index) const
{
    auto project = std::atomic_load(&project_);
    if (project == nullptr || index < 0 || index >= (int) project->tracks.size())
        return -1;
    return project->tracks[(size_t) index].id;
}

bool Engine::trackInstrumentLive(TrackId trackId) const
{
    return transport_->trackInstrumentLive(trackId);
}

juce::Result Engine::copyClip(ClipId clipId)
{
    auto project = std::atomic_load(&project_);
    if (project == nullptr)
        return fail("no project");
    const Clip* clip = project->findClip(clipId);
    if (clip == nullptr)
        return fail("clip not found");
    clipboardClip_ = *clip;
    clipboardValid_ = true;
    return juce::Result::ok();
}

juce::Result Engine::pasteClip(TrackId trackId, double startSeconds)
{
    if (! clipboardValid_)
        return fail("el portapapeles está vacío");
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found");
            return false;
        }
        const bool midi = clipboardClip_.isMidi();
        if ((track->type == Track::Type::Instrument) != midi)
        {
            fail(midi ? "los clips MIDI se pegan en pistas de instrumento"
                      : "los clips de audio se pegan en pistas de audio");
            return false;
        }
        Clip clip = clipboardClip_;
        clip.id = project.nextClipId++;
        clip.startSeconds = std::max(0.0, startSeconds);
        track->clips.push_back(std::move(clip));
        return true;
    });
}

const char* Engine::trackName(TrackId trackId) const
{
    stringOut_.clear();
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            stringOut_ = track->name.toStdString();
    return stringOut_.c_str();
}

int Engine::trackColorIndex(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->colorIndex;
    return -1;
}

float Engine::trackVolume(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->volume;
    return 0.0f;
}

float Engine::trackPan(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->pan;
    return 0.0f;
}

int Engine::trackType(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->type == Track::Type::Instrument ? 1 : 0;
    return -1;
}

bool Engine::trackMute(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->mute;
    return false;
}

bool Engine::trackSolo(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return track->solo;
    return false;
}

int Engine::trackClipCount(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return (int) track->clips.size();
    return 0;
}

ClipId Engine::clipIdAt(TrackId trackId, int index) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            if (index >= 0 && index < (int) track->clips.size())
                return track->clips[(size_t) index].id;
    return -1;
}

double Engine::clipStart(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            return clip->startSeconds;
    return -1.0;
}

double Engine::clipLength(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            return clip->lengthSeconds;
    return -1.0;
}

double Engine::clipOffset(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            return clip->sourceOffsetSeconds;
    return -1.0;
}

double Engine::clipLengthMax(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
        {
            // MIDI clips have no source: their length is free (notes beyond
            // the clip stay silent until it is extended again).
            if (clip->midi != nullptr)
                return 3600.0;
            auto srcIt = project->sources.find(clip->sourceId);
            const double srcDuration = srcIt != project->sources.end()
                                           ? srcIt->second->durationSeconds : 0.0;
            return std::max(0.0, srcDuration - clip->sourceOffsetSeconds);
        }
    return -1.0;
}

double Engine::clipFadeIn(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            return clip->fadeInSeconds;
    return -1.0;
}

double Engine::clipFadeOut(ClipId clipId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Clip* clip = project->findClip(clipId))
            return clip->fadeOutSeconds;
    return -1.0;
}

float Engine::meterTrackPeak(int trackIndex)
{
    return transport_->trackPeak(trackIndex);
}

int Engine::clipPeaks(ClipId clipId, double startSeconds, double lengthSeconds,
                      int numPixels, float* outMins, float* outMaxs) const
{
    if (numPixels <= 0 || outMins == nullptr || outMaxs == nullptr || lengthSeconds <= 0.0)
        return -1;

    auto project = std::atomic_load(&project_);
    if (project == nullptr)
        return -1;
    const Clip* clip = project->findClip(clipId);
    if (clip == nullptr)
        return -1;
    auto srcIt = project->sources.find(clip->sourceId);
    if (srcIt == project->sources.end() || srcIt->second == nullptr)
        return -1;
    const auto& source = srcIt->second->samples;

    const double rate = project->sampleRate;
    const double base = (clip->sourceOffsetSeconds + startSeconds) * rate;
    const double step = lengthSeconds * rate / numPixels;
    const int numChannels = std::min(2, source.getNumSamples() > 0 ? source.getNumChannels() : 0);

    for (int p = 0; p < numPixels; ++p)
    {
        const double s0 = base + p * step;
        const double s1 = s0 + step;
        int i0 = (int) std::max(0.0, s0);
        int i1 = (int) std::min<double>((double) source.getNumSamples(), std::max(s1, s0 + 1.0));

        if (numChannels == 0 || i0 >= i1)
        {
            outMins[p] = 0.0f;
            outMaxs[p] = 0.0f;
            continue;
        }

        float mn = 0.0f;
        float mx = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = source.getReadPointer(ch);
            for (int i = i0; i < i1; ++i)
            {
                mn = std::min(mn, data[i]);
                mx = std::max(mx, data[i]);
            }
        }
        outMins[p] = mn;
        outMaxs[p] = mx;
    }
    return 0;
}

float Engine::meterMasterPeak(int channel)
{
    return transport_->masterPeak(channel);
}

juce::Result Engine::loadFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return fail("file not found: " + file.getFullPathName());

    const std::scoped_lock lock(editMutex_);

    auto project = std::make_shared<Project>();
    project->sampleRate = defaultRate();

    const SourceId sourceId = loadSourceInto(*project, file);
    if (sourceId < 0)
        return juce::Result::fail(juce::String(lastErrorUtf8_));

    Track track;
    track.id = project->nextTrackId++;
    track.name = "Track 1";

    Clip clip;
    clip.id = project->nextClipId++;
    clip.sourceId = sourceId;
    clip.lengthSeconds = project->sources[sourceId]->durationSeconds;
    track.clips.push_back(std::move(clip));
    project->tracks.push_back(std::move(track));

    undoStack_.clear();
    redoStack_.clear();
    publish(std::move(project));
    return juce::Result::ok();
}

juce::Result Engine::saveProject(const juce::File& file)
{
    const std::scoped_lock lock(editMutex_);

    auto project = std::atomic_load(&project_);
    if (project == nullptr)
        return fail("no project to save");

    // Capture the LIVE plugin states (chosen patches, knob settings) into
    // the serialized copy, so reopening restores the exact sound. Runs on
    // the message thread: getStateInformation may take plugin locks.
    auto serializable = std::make_shared<Project>(*project);
    runOnMessageThread([&] {
        for (auto& track : serializable->tracks)
        {
            if (auto inst = transport_->instrumentProcessorFor(track.id))
            {
                juce::MemoryBlock mb;
                inst->getStateInformation(mb);
                if (mb.getSize() > 0)
                    track.instrument.state = mb.toBase64Encoding();
            }
            if (auto mfx = transport_->midiFxProcessorFor(track.id))
            {
                juce::MemoryBlock mb;
                mfx->getStateInformation(mb);
                if (mb.getSize() > 0)
                    track.midiFx.state = mb.toBase64Encoding();
            }
            for (int i = 0; i < (int) track.fx.size(); ++i)
                if (auto proc = transport_->fxProcessorFor(track.id, i))
                {
                    juce::MemoryBlock mb;
                    proc->getStateInformation(mb);
                    if (mb.getSize() > 0)
                        track.fx[(size_t) i].state = mb.toBase64Encoding();
                }
        }
        for (int i = 0; i < (int) serializable->masterFx.size(); ++i)
            if (auto proc = transport_->masterFxProcessorFor(i))
            {
                juce::MemoryBlock mb;
                proc->getStateInformation(mb);
                if (mb.getSize() > 0)
                    serializable->masterFx[(size_t) i].state =
                        mb.toBase64Encoding();
            }
    });

    const auto text = juce::JSON::toString(
        projectToVar(*serializable, file.getParentDirectory()), true);
    if (! file.replaceWithText(text))
        return fail("cannot write " + file.getFullPathName());

    projectFile_ = file;
    return juce::Result::ok();
}


// ---- Project import: standard MIDI files (Cubase/Live/Logic export) --------
// The open interop path: Cubase's "Export > MIDI File" is a Type-1 SMF with
// every track, the tempo map and the time signature. Each note-track
// becomes an instrument track with ONE clip spanning the part.
juce::Result Engine::importMidiProject(const juce::File& file)
{
    const std::scoped_lock lock(editMutex_);

    juce::MidiFile mf;
    {
        juce::FileInputStream in(file);
        if (in.failedToOpen())
            return fail("cannot open " + file.getFullPathName());
        if (! mf.readFrom(in))
            return fail("not a valid MIDI file: " + file.getFullPathName());
    }
    const int ppq = mf.getTimeFormat();
    if (ppq <= 0)
        return fail("SMPTE-timed MIDI files are not supported");

    mf.convertTimestampTicksToSeconds();
    // NOTE: juce converts with the WHOLE tempo map? It walks the tempo
    // events — but only when the file has them. We additionally harvest the
    // tempo/signature below for the project settings.

    double tempoBpm = 120.0;
    int sigNum = 4, sigDen = 4;
    bool haveTempo = false, haveSig = false;
    for (int t = 0; t < mf.getNumTracks() && (! haveTempo || ! haveSig); ++t)
        for (const auto* evHolder : *mf.getTrack(t))
        {
            const auto& m = evHolder->message;
            if (! haveTempo && m.isTempoMetaEvent())
            {
                tempoBpm = 60.0 / m.getTempoSecondsPerQuarterNote();
                haveTempo = true;
            }
            if (! haveSig && m.isTimeSignatureMetaEvent())
            {
                m.getTimeSignatureInfo(sigNum, sigDen);
                haveSig = true;
            }
        }

    Project project;
    project.name = file.getFileNameWithoutExtension();
    project.sampleRate = defaultRate();
    project.tempoBpm = juce::jlimit(20.0, 300.0, tempoBpm);
    project.timeSigNumerator = juce::jlimit(1, 32, sigNum);
    project.timeSigDenominator = juce::jlimit(1, 32, sigDen);

    int imported = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto& track = *mf.getTrack(t);
        juce::String name;
        double clipStart = -1.0, clipEnd = 0.0;

        struct Paired
        {
            double start, len;
            int pitch;
            float vel;
        };
        std::vector<Paired> notes;
        std::map<int, std::vector<std::pair<double, float>>> open_; // FIFO per pitch
        bool isDrums = false;

        for (const auto* evHolder : track)
        {
            const auto& m = evHolder->message;
            if (m.isTrackNameEvent() && name.isEmpty())
                name = m.getTextFromTextMetaEvent();
            if (m.getChannel() == 10)
                isDrums = true;
            const double tSec = m.getTimeStamp(); // seconds (converted)
            if (m.isNoteOn())
            {
                open_[m.getNoteNumber()].push_back({ tSec, (float) m.getVelocity() });
                if (clipStart < 0 || tSec < clipStart)
                    clipStart = tSec;
                clipEnd = juce::jmax(clipEnd, tSec);
            }
            else if (m.isNoteOff() || (m.isNoteOn() && m.getVelocity() <= 0.001f))
            {
                auto it = open_.find(m.getNoteNumber());
                if (it != open_.end() && ! it->second.empty())
                {
                    auto on = it->second.front();
                    it->second.erase(it->second.begin());
                    notes.push_back({ on.first,
                                      juce::jmax(0.05, tSec - on.first),
                                      m.getNoteNumber(), on.second });
                    clipEnd = juce::jmax(clipEnd, tSec);
                }
            }
        }
        if (notes.empty())
            continue;
        for (auto& [pitch, vec] : open_)
            for (const auto& on : vec) // unterminated: 0.5 s tail
            {
                notes.push_back({ on.first, 0.5, pitch, on.second });
                clipEnd = juce::jmax(clipEnd, on.first + 0.5);
            }
        if (clipStart < 0)
            clipStart = 0.0;
        clipStart = juce::jmax(0.0, clipStart - 0.02);

        Clip clip;
        clip.id = project.nextClipId++;
        clip.startSeconds = clipStart;
        clip.lengthSeconds = juce::jmax(kMinClipLength, clipEnd - clipStart + 0.1);
        auto data = std::make_shared<MidiClipData>();
        for (const auto& n : notes)
        {
            if (n.pitch < 0 || n.pitch > 127)
                continue;
            MidiNote mn;
            mn.startSeconds = juce::jmax(0.0, n.start - clipStart);
            mn.lengthSeconds = n.len;
            mn.pitch = n.pitch;
            mn.velocity = juce::jlimit(0.05f, 1.f, n.vel / 127.f);
            mn.origStartSeconds = mn.startSeconds;
            data->notes.push_back(mn);
        }
        clip.midi = std::move(data);

        Track newTrack;
        newTrack.id = project.nextTrackId++;
        newTrack.name = name.isEmpty()
                            ? "MIDI " + juce::String(t + 1)
                            : name;
        if (isDrums)
            newTrack.name += juce::String::fromUTF8(" (batería)");
        newTrack.type = Track::Type::Instrument;
        newTrack.clips.push_back(std::move(clip));
        project.tracks.push_back(std::move(newTrack));
        ++imported;
    }
    if (imported == 0)
        return fail("the MIDI file has no note tracks");

    auto shared = std::make_shared<Project>(std::move(project));
    undoStack_.clear();
    redoStack_.clear();
    transport_->stop();
    publish(shared, /*deferHeavyPlugins=*/false);
    std::fprintf(stderr, "[import] %s: %d pistas, %.1f bpm, %d/%d\n",
                 file.getFileName().toRawUTF8(), imported, project.tempoBpm,
                 project.timeSigNumerator, project.timeSigDenominator);
    return juce::Result::ok();
}

// Stems: one AUDIO track per wav/aiff in the folder, named by the file
// stem, clip at 0. Appends to the current project (after the imported MIDI
// tracks) via the public single-edit APIs.
juce::Result Engine::importStemsFolder(const juce::File& folder)
{
    if (! folder.isDirectory())
        return fail("not a folder: " + folder.getFullPathName());
    auto files = folder.findChildFiles(juce::File::findFiles, false,
                                       "*.wav;*.aiff;*.aif");
    files.sort();
    if (files.isEmpty())
        return fail("no audio files in " + folder.getFullPathName());

    for (const auto& f : files)
    {
        const auto name = f.getFileNameWithoutExtension();
        const auto trackId = addTrack(name, (int) (trackCount() % 16),
                                      Track::Type::Audio);
        if (trackId < 0)
            return fail("cannot add track for " + name);
        if (addClip(trackId, f, 0.0, 1.0f) < 0)
            return fail("cannot add clip for " + name);
    }
    return juce::Result::ok();
}

juce::Result Engine::importAudioFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return fail("no such audio file: " + file.getFullPathName());
    const auto name = file.getFileNameWithoutExtension();
    const auto trackId = addTrack(name, (int) (trackCount() % 16),
                                  Track::Type::Audio);
    if (trackId < 0)
        return fail("cannot add track for " + name);
    if (addClip(trackId, file, 0.0, 1.0f) < 0)
        return fail("cannot add clip for " + name);
    return juce::Result::ok();
}

const char* Engine::pickOpenFile(const juce::File& startDir,
                                 const juce::String& filters)
{
    // The FFI caller is the Flutter main thread, which IS the JUCE
    // message thread — the modal NSOpenPanel pumps its own event loop and
    // behaves exactly like a native open dialog.
    stringOut_ = native_dialogs::pickOpenFile("Abrir", startDir.getFullPathName(),
                                              filters).toStdString();
    return stringOut_.c_str();
}

juce::Result Engine::loadProject(const juce::File& file)
{
    const std::scoped_lock lock(editMutex_);

    auto parsed = juce::JSON::parse(file);
    if (parsed.isVoid())
        return fail("invalid project file: " + file.getFullPathName());

    Project project;
    if (projectFromVar(parsed, project).failed())
        return fail("invalid project document: " + file.getFullPathName());

    const juce::File dir = file.getParentDirectory();
    for (const auto& [id, storedPath] : project.sourceFiles)
    {
        juce::File sourceFile(storedPath);
        if (! sourceFile.existsAsFile())
            sourceFile = dir.getChildFile(storedPath);
        if (! sourceFile.existsAsFile())
            return fail("missing audio file: " + storedPath);

        auto data = std::make_shared<AudioSourceData>();
        if (loadAudioSourceData(formatManager_, sourceFile, project.sampleRate, *data).failed())
            return fail("cannot read audio file: " + sourceFile.getFullPathName());

        project.sourceFiles[id] = sourceFile.getFullPathName(); // engine form: absolute
        project.sources[id] = std::move(data);
    }

    auto shared = std::make_shared<Project>(std::move(project));
    undoStack_.clear();
    redoStack_.clear();
    // Heavy plugin loads go through the deferred pump (one per message-loop
    // pass) so the UI paints the project immediately.
    publish(shared, /*deferHeavyPlugins=*/true);
    transport_->stop();
    projectFile_ = file;
    clipboardValid_ = false; // sources may not exist in the loaded project
    return juce::Result::ok();
}

void Engine::play() { transport_->play(); }
void Engine::pause()
{
    finalizeRecording();
    // Stop the transport FIRST: a block that starts after this point must
    // see the panic in its stopped path, where every instrument track is
    // flushed. Panicking before pausing left a window where the audio thread
    // could still schedule notes after the panic.
    transport_->pause();
    // Do NOT silence synth voices from this (message) thread while the audio
    // thread may be inside renderNextBlock: the panic is delivered by the
    // next callback, where silenceSynthForTrack() runs on the audio thread.
    // If the device is already dead, flushAllNotes() handles it on restart.
    transport_->requestPanic();
}
void Engine::stop()
{
    finalizeRecording();
    transport_->stop();
    transport_->requestPanic();
}
void Engine::seek(double seconds)
{
    // Move first, then panic: the generation counter is sampled at the
    // start of a block, so the panic is delivered with the new position
    // (or by the next block) instead of racing the old one.
    transport_->seek(seconds);
    transport_->requestPanic();
}

double Engine::durationSeconds() const
{
    if (auto project = std::atomic_load(&project_))
        return project->durationSeconds();
    return 0.0;
}

// ---- plugin management (§7.7) ----

void Engine::rescanPlugins()
{
    runOnMessageThread([this] { pluginHost_.beginScan(); });
}

void Engine::scanPluginsNow()
{
    runOnMessageThread([this] { pluginHost_.scanNow(); });
}

juce::Result Engine::openPluginEditor(TrackId trackId, int slotIndex)
{
    juce::Result result = juce::Result::ok();
    runOnMessageThread([&] {
        auto proc = transport_->fxProcessorFor(trackId, slotIndex);
        if (proc == nullptr)
        {
            result = fail("no plugin in that slot");
            return;
        }
        const auto key = ((std::uint64_t) trackId << 16)
                         | (std::uint64_t) (slotIndex + 1);
        auto it = editorWindows_.find(key);
        if (it != editorWindows_.end() && it->second.proc != proc)
        {
            // The slot's plugin changed (remove/reorder shifts slots): the
            // cached window shows the WRONG plugin's editor and keeps the
            // removed instance alive. Recreate for the current processor.
            retireEditorWindow(std::move(it->second.window));
            it = editorWindows_.erase(it);
        }
        if (it == editorWindows_.end())
            it = editorWindows_
                     .emplace(key,
                              EditorEntry{std::make_shared<PluginEditorWindow>(
                                              proc),
                                          proc})
                     .first;
        it->second.window->setVisible(true);
        it->second.window->toFront(true);
    });
    return result;
}

juce::Result Engine::openInstrumentEditor(TrackId trackId, ClipId clipId)
{
    juce::Result result = juce::Result::ok();
    runOnMessageThread([&] {
        auto proc = transport_->instrumentProcessorFor(trackId);
        if (proc == nullptr)
        {
            result = fail("no instrument plugin on that track");
            return;
        }
        // No explicit clip (piano-roll selection)? Fall back to the track's
        // first MIDI clip so the editor binds to something playable.
        ClipId bound = clipId;
        if (bound == 0)
        {
            const int n = trackClipCount(trackId);
            for (int i = 0; i < n && bound == 0; ++i)
            {
                const auto candidate = clipIdAt(trackId, i);
                if (candidate != 0 && clipIsMidi(candidate))
                    bound = candidate;
            }
        }
        // EZSynth edits its own track's clips through this context (clip 0
        // = unbound; it can create one at the playhead from its editor).
        ezsynth::bindHostContext(proc.get(), this, trackId, bound);
        const auto key = (std::uint64_t) trackId << 16;
        auto it = editorWindows_.find(key);
        if (it != editorWindows_.end() && it->second.proc != proc)
        {
            // Never destroy a live VST3 editor synchronously here: this can
            // run from the plugin's own window callback. Hide it now and
            // release it on the next message-loop turn.
            retireEditorWindow(std::move(it->second.window));
            it = editorWindows_.erase(it);
        }
        if (it == editorWindows_.end())
            it = editorWindows_
                     .emplace(key,
                              EditorEntry{std::make_shared<PluginEditorWindow>(
                                              proc),
                                          proc})
                     .first;
        it->second.window->setVisible(true);
        it->second.window->toFront(true);
    });
    return result;
}

juce::Result Engine::openMasterFxEditor(int slotIndex)
{
    juce::Result result = juce::Result::ok();
    runOnMessageThread([&] {
        auto proc = transport_->masterFxProcessorFor(slotIndex);
        if (proc == nullptr)
        {
            result = fail("no plugin in that master slot");
            return;
        }
        // Master slots use a key namespace disjoint from track keys.
        const auto key = 0x8000000000000000ULL
                         | (std::uint64_t) (slotIndex + 1);
        auto it = editorWindows_.find(key);
        if (it != editorWindows_.end() && it->second.proc != proc)
        {
            // Never destroy a live VST3 editor synchronously here: this can
            // run from the plugin's own window callback. Hide it now and
            // release it on the next message-loop turn.
            retireEditorWindow(std::move(it->second.window));
            it = editorWindows_.erase(it);
        }
        if (it == editorWindows_.end())
            it = editorWindows_
                     .emplace(key,
                              EditorEntry{std::make_shared<PluginEditorWindow>(
                                              proc),
                                          proc})
                     .first;
        it->second.window->setVisible(true);
        it->second.window->toFront(true);
    });
    return result;
}

void Engine::retireEditorWindow(std::shared_ptr<juce::DocumentWindow> window)
{
    if (window == nullptr)
        return;
    // Hide immediately so the user cannot interact with a window whose
    // processor is about to leave the graph; destroy it after the current
    // callback returns.
    window->setVisible(false);
    if (retiredEditors_ == nullptr)
        retiredEditors_ = std::make_shared<RetiredEditorList>();
    auto retired = retiredEditors_;
    retired->windows.push_back(std::move(window));
    juce::MessageManager::callAsync([retired] { retired->windows.clear(); });
}

void Engine::closeOrphanedEditorWindows()
{
    if (shuttingDown_.load(std::memory_order_acquire) || transport_ == nullptr)
        return;
    const auto live = transport_->liveProcessors();
    for (auto it = editorWindows_.begin(); it != editorWindows_.end();)
    {
        const auto* proc = it->second.proc.get();
        if (proc == nullptr
            || std::find(live.begin(), live.end(), proc) == live.end())
        {
            retireEditorWindow(std::move(it->second.window));
            it = editorWindows_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Engine::closeAllEditorWindows()
{
    for (auto& entry : editorWindows_)
        if (entry.second.window != nullptr)
            entry.second.window->setVisible(false);
    editorWindows_.clear();
    if (retiredEditors_ != nullptr)
        retiredEditors_->windows.clear();
}

int Engine::pluginCount() const { return pluginHost_.pluginCount(); }

bool Engine::pluginScanActive() const { return pluginHost_.scanActive(); }

const char* Engine::pluginNameAt(int index) const
{
    stringOut_.clear();
    if (auto desc = pluginHost_.descriptionAt(index))
        stringOut_ = desc->name.toStdString();
    return stringOut_.c_str();
}

const char* Engine::pluginUidAt(int index) const
{
    stringOut_.clear();
    if (auto desc = pluginHost_.descriptionAt(index))
        stringOut_ = desc->createIdentifierString().toStdString();
    return stringOut_.c_str();
}

bool Engine::pluginIsInstrumentAt(int index) const
{
    if (auto desc = pluginHost_.descriptionAt(index))
        return desc->isInstrument;
    return false;
}

const char* Engine::pluginFormatAt(int index) const
{
    stringOut_.clear();
    if (auto desc = pluginHost_.descriptionAt(index))
        stringOut_ = desc->pluginFormatName.toStdString();
    return stringOut_.c_str();
}

juce::Result Engine::addTrackFx(TrackId trackId, int pluginIndex)
{
    juce::String uid, name;
    if (auto desc = pluginHost_.descriptionAt(pluginIndex))
    {
        uid = desc->createIdentifierString();
        name = desc->name;
    }
    else
        return fail("plugin index out of range");

    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        FxSlot slot;
        slot.pluginUid = uid;
        slot.name = name;
        track->fx.push_back(slot);
        return true;
    });
}

juce::Result Engine::removeTrackFx(TrackId trackId, int slotIndex)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr || slotIndex < 0
            || slotIndex >= (int) track->fx.size())
        {
            fail("fx slot not found");
            return false;
        }
        track->fx.erase(track->fx.begin() + slotIndex);
        return true;
    });
}

juce::Result Engine::setTrackFxBypass(TrackId trackId, int slotIndex, bool bypass)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr || slotIndex < 0
            || slotIndex >= (int) track->fx.size())
        {
            fail("fx slot not found");
            return false;
        }
        track->fx[(size_t) slotIndex].bypass = bypass;
        return true;
    });
}

int Engine::trackFxCount(TrackId trackId) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            return (int) track->fx.size();
    return 0;
}

const char* Engine::trackFxNameAt(TrackId trackId, int slotIndex) const
{
    stringOut_.clear();
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            if (slotIndex >= 0 && slotIndex < (int) track->fx.size())
                stringOut_ = track->fx[(size_t) slotIndex].name.toStdString();
    return stringOut_.c_str();
}

bool Engine::trackFxBypassAt(TrackId trackId, int slotIndex) const
{
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            if (slotIndex >= 0 && slotIndex < (int) track->fx.size())
                return track->fx[(size_t) slotIndex].bypass;
    return false;
}

const char* Engine::trackInstrumentName(TrackId trackId) const
{
    stringOut_.clear();
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            stringOut_ = track->instrument.name.toStdString();
    return stringOut_.c_str();
}

juce::Result Engine::setTrackInstrumentPlugin(TrackId trackId, int pluginIndex)
{
    juce::String uid, name;
    if (auto desc = pluginHost_.descriptionAt(pluginIndex))
    {
        uid = desc->createIdentifierString();
        name = desc->name;
    }
    else
        return fail("plugin index out of range");

    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->instrument.pluginUid = uid;
        track->instrument.name = name;
        return true;
    });
}

juce::Result Engine::clearTrackInstrument(TrackId trackId)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->instrument = {};
        return true;
    });
}

namespace {
// Rebuilds the state wrapper JUCE's VST3 setStateInformation() parses:
// `<VST3PluginState><IComponent>{base64}</IComponent>
// <IEditController>{base64}</IEditController></VST3PluginState>`, serialised by
// AudioProcessor::copyXmlToBinary (magic + length + XML). Cubase stores those
// two states as separate blobs (audioComponent / editController); JUCE wants
// them recombined exactly like this. See docs/design/cubase-import.md.
juce::MemoryBlock wrapVst3PluginState(const void* component, int componentSize,
                                      const void* controller, int controllerSize)
{
    juce::XmlElement state("VST3PluginState");
    if (component != nullptr && componentSize > 0)
    {
        const juce::MemoryBlock mb(component, (size_t) componentSize);
        state.createNewChildElement("IComponent")
            ->addTextElement(mb.toBase64Encoding());
    }
    if (controller != nullptr && controllerSize > 0)
    {
        const juce::MemoryBlock mb(controller, (size_t) controllerSize);
        state.createNewChildElement("IEditController")
            ->addTextElement(mb.toBase64Encoding());
    }
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary(state, out);
    return out;
}
} // namespace

juce::Result Engine::setTrackFxState(TrackId trackId, int slotIndex,
                                     const void* component, int componentSize,
                                     const void* controller, int controllerSize)
{
    const auto wrapped = wrapVst3PluginState(component, componentSize,
                                             controller, controllerSize);
    juce::Result result = juce::Result::fail("no plugin in that slot");
    runOnMessageThread([&] {
        auto proc = transport_->fxProcessorFor(trackId, slotIndex);
        if (proc == nullptr)
        {
            result = fail("no plugin in that slot");
            return;
        }
        proc->setStateInformation(wrapped.getData(), (int) wrapped.getSize());
        result = juce::Result::ok();
    });
    return result;
}

juce::Result Engine::setTrackInstrumentState(TrackId trackId,
                                             const void* component,
                                             int componentSize,
                                             const void* controller,
                                             int controllerSize)
{
    const auto wrapped = wrapVst3PluginState(component, componentSize,
                                             controller, controllerSize);
    juce::Result result = juce::Result::fail("no instrument plugin");
    runOnMessageThread([&] {
        auto proc = transport_->instrumentProcessorFor(trackId);
        if (proc == nullptr)
        {
            result = fail("no instrument plugin");
            return;
        }
        proc->setStateInformation(wrapped.getData(), (int) wrapped.getSize());
        result = juce::Result::ok();
    });
    return result;
}

const char* Engine::pluginStateBase64(TrackId trackId, int slotKind,
                                      int slotIndex) const
{
    stringOut_.clear();
    runOnMessageThread([&] {
        std::shared_ptr<juce::AudioProcessor> proc;
        if (slotKind == 1)
            proc = transport_->instrumentProcessorFor(trackId);
        else
            proc = transport_->fxProcessorFor(trackId, slotIndex);
        if (proc == nullptr)
            return;
        juce::MemoryBlock mb;
        proc->getStateInformation(mb);
        stringOut_ = mb.toBase64Encoding().toStdString();
    });
    return stringOut_.c_str();
}

// MIDI inserts (EZArpeggio) live in the track's fx ARRAY like any effect
// (mixer chips, editor, persistence all work for free) — the engine just
// routes them as MIDI instead of audio.
juce::Result Engine::setTrackMidiFx(TrackId trackId, int pluginIndex)
{
    juce::String uid, name;
    if (auto desc = pluginHost_.descriptionAt(pluginIndex))
    {
        uid = desc->createIdentifierString();
        name = desc->name;
    }
    else
        return fail("plugin index out of range");

    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->fx.erase(
            std::remove_if(track->fx.begin(), track->fx.end(),
                           [](const FxSlot& s) {
                               return s.pluginUid.containsIgnoreCase(
                                   "ezarp");
                           }),
            track->fx.end());
        FxSlot slot;
        slot.pluginUid = uid;
        slot.name = name;
        track->fx.push_back(std::move(slot));
        return true;
    });
}

juce::Result Engine::clearTrackMidiFx(TrackId trackId)
{
    return editProject([&] (Project& project) {
        Track* track = project.findTrack(trackId);
        if (track == nullptr)
        {
            fail("track not found: " + juce::String((int) trackId));
            return false;
        }
        track->fx.erase(
            std::remove_if(track->fx.begin(), track->fx.end(),
                           [](const FxSlot& s) {
                               return s.pluginUid.containsIgnoreCase(
                                   "ezarp");
                           }),
            track->fx.end());
        return true;
    });
}

const char* Engine::trackMidiFxName(TrackId trackId) const
{
    stringOut_.clear();
    if (auto project = std::atomic_load(&project_))
        if (const Track* track = project->findTrack(trackId))
            for (const auto& slot : track->fx)
                if (slot.pluginUid.containsIgnoreCase("ezarp"))
                    stringOut_ = slot.name.toStdString();
    return stringOut_.c_str();
}

juce::Result Engine::addMasterFx(int pluginIndex)
{
    juce::String uid, name;
    if (auto desc = pluginHost_.descriptionAt(pluginIndex))
    {
        uid = desc->createIdentifierString();
        name = desc->name;
    }
    else
        return fail("plugin index out of range");

    return editProject([&] (Project& project) {
        FxSlot slot;
        slot.pluginUid = uid;
        slot.name = name;
        project.masterFx.push_back(slot);
        return true;
    });
}

juce::Result Engine::removeMasterFx(int slotIndex)
{
    return editProject([&] (Project& project) {
        if (slotIndex < 0 || slotIndex >= (int) project.masterFx.size())
        {
            fail("master fx slot not found");
            return false;
        }
        project.masterFx.erase(project.masterFx.begin() + slotIndex);
        return true;
    });
}

juce::Result Engine::setMasterFxBypass(int slotIndex, bool bypass)
{
    return editProject([&] (Project& project) {
        if (slotIndex < 0 || slotIndex >= (int) project.masterFx.size())
        {
            fail("master fx slot not found");
            return false;
        }
        project.masterFx[(size_t) slotIndex].bypass = bypass;
        return true;
    });
}

int Engine::masterFxCount() const
{
    if (auto project = std::atomic_load(&project_))
        return (int) project->masterFx.size();
    return 0;
}

const char* Engine::masterFxNameAt(int slotIndex) const
{
    stringOut_.clear();
    if (auto project = std::atomic_load(&project_))
        if (slotIndex >= 0 && slotIndex < (int) project->masterFx.size())
            stringOut_ = project->masterFx[(size_t) slotIndex].name.toStdString();
    return stringOut_.c_str();
}

bool Engine::masterFxBypassAt(int slotIndex) const
{
    if (auto project = std::atomic_load(&project_))
        if (slotIndex >= 0 && slotIndex < (int) project->masterFx.size())
            return project->masterFx[(size_t) slotIndex].bypass;
    return false;
}

juce::Result Engine::renderToFile(const juce::File& outFile, double maxSeconds)
{
    return renderToFileEx(outFile, maxSeconds, 16);
}

// Offline render: NOT realtime — blocks are processed as fast as the CPU
// allows, at the project sample rate (no resampling, no artifacts).
//
// The app used to build a SECOND ProjectAudioSource for every export, which
// instantiated a second copy of every VST3. Analog Lab V leaves run-loop
// timers behind when such a copy is destroyed: the WAV was complete, then the
// app crashed in AppKit → Analog Lab V a few seconds later. When the live
// device is open at the project rate we now detach the audio callback, render
// through the EXISTING plugin instances, then reattach it. The device itself
// stays open — closing/reopening it can block forever in AudioDeviceStart on
// the UR22. No duplicate VST3s, no dangling timers. The separate-source path
// is only a fallback and its instances are quarantined until Engine
// destruction.
juce::Result Engine::renderToFileEx(const juce::File& outFile, double maxSeconds,
                                    int bitsPerSample)
{
    const std::scoped_lock lock(editMutex_);

    auto project = std::atomic_load(&project_);
    if (project == nullptr || project->tracks.empty())
        return fail("nothing to render: project is empty");

    const double duration = std::min(project->durationSeconds(), juce::jmax(0.0, maxSeconds));
    const int totalSamples = (int) std::ceil(duration * project->sampleRate);
    if (totalSamples <= 0)
        return fail("nothing to render: project has no clips");

    // FileOutputStream appends to existing files; renders must replace.
    outFile.deleteFile();

    auto fileStream = outFile.createOutputStream();
    if (fileStream == nullptr)
        return fail("cannot open " + outFile.getFullPathName() + " for writing");

    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(project->sampleRate)
                     .withNumChannels(2)
                     .withBitsPerSample(
                         juce::jlimit(16, 24, bitsPerSample));

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    auto writer = wavFormat.createWriterFor(stream, options);
    if (writer == nullptr)
        return fail("cannot create wav writer");

    auto writeBlocks = [&](ProjectAudioSource& source, int blockSize) {
        juce::AudioBuffer<float> block(2, blockSize);
        for (int pos = 0; pos < totalSamples && source.isPlaying();
             pos += blockSize)
        {
            block.clear();
            const int n = std::min(blockSize, totalSamples - pos);
            const juce::AudioSourceChannelInfo info { &block, 0, n };
            source.getNextAudioBlock(info);
            if (! writer->writeFromAudioSampleBuffer(block, 0, n))
                return false;
        }
        return true;
    };

    auto* device = deviceManager_.getCurrentAudioDevice();
    const double deviceRate = device != nullptr
        ? device->getCurrentSampleRate() : 0.0;
    const int deviceBlock = device != nullptr
        ? device->getCurrentBufferSizeSamples() : 0;
    const double liveRate = transport_->preparedSampleRate() > 0.0
        ? transport_->preparedSampleRate() : deviceRate;
    const int liveBlock = transport_->preparedBlockSize() > 0
        ? transport_->preparedBlockSize() : deviceBlock;
    const bool canUseLive = !isRecording()
        && juce::MessageManager::getInstance()->isThisTheMessageThread()
        && device != nullptr
        && liveRate > 0.0
        && std::abs(liveRate - project->sampleRate) < 0.5;

    if (canUseLive)
    {
        const bool wasPlaying = transport_->isPlaying();
        const double position = transport_->positionSeconds();
        const bool metronome = transport_->metronomeEnabled();
        const int blockSize = liveBlock > 0
            ? juce::jmin(512, liveBlock) : 512;

        // Do NOT close the device: on the UR22 AudioDeviceStart can block
        // forever in HALB_IOThread::StartAndWaitForState. Removing the
        // callback is enough to guarantee the audio thread is no longer
        // touching the plugin instances; the device keeps its IO thread and
        // is simply reattached after the render.
        intentionalDeviceStop_.store(true, std::memory_order_release);
        transport_->pause();
        transport_->requestPanic();
        deviceManager_.removeAudioCallback(outputCallback_.get());
        transport_->flushAllNotes();
        transport_->setMetronome(false);
        transport_->setOfflineRender(true);
        transport_->seek(0.0);
        transport_->play();

        bool callbackRemoved = true;
        auto restore = [&] {
            if (! callbackRemoved)
                return;
            callbackRemoved = false;
            transport_->setOfflineRender(false);
            transport_->stop();
            transport_->seek(position);
            transport_->setMetronome(metronome);
            if (wasPlaying)
                transport_->play();
            else
                transport_->pause();
            deviceManager_.addAudioCallback(outputCallback_.get());
            intentionalDeviceStop_.store(false, std::memory_order_release);
        };
        juce::ScopeGuard guard { restore };

        if (! writeBlocks(*transport_, blockSize))
            return fail("cannot write " + outFile.getFullPathName());
        writer.reset();
        return juce::Result::ok();
    }

    // Fallback: separate source. Keep it alive until the Engine dies — some
    // VST3s schedule run-loop work even without an editor, and destroying the
    // instance immediately is what crashed the app after the export.
    auto renderSource = std::make_unique<ProjectAudioSource>();
    renderSource->setProject(project);
    renderSource->reconcileNodes(*project, pluginHost_, 512, false);
    // EZOrquestra decodes its program on a loader thread — the offline
    // render must wait for it or the render is silent.
    ezorq::waitProgramsLoaded();
    renderSource->play();
    if (! writeBlocks(*renderSource, 512))
        return fail("cannot write " + outFile.getFullPathName());
    renderSource->stop();
    offlineRenderSources_.push_back(std::move(renderSource));
    writer.reset();
    return juce::Result::ok();
}

} // namespace cadence

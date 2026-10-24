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
    {
        const juce::SpinLock::ScopedLockType lock(synthsLock_);
        for (auto it = synths_.begin(); it != synths_.end();)
        {
            bool kept = false;
            for (const auto& track : newProject->tracks)
                if (track.id == it->first) { kept = true; break; }
            it = kept ? std::next(it) : synths_.erase(it);
        }
    }
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

void ProjectAudioSource::prepareToPlay(int samplesPerBlockExpected, double /*sampleRate*/)
{
    trackScratch_.setSize(2, samplesPerBlockExpected, false, false, true);
}

void ProjectAudioSource::releaseResources()
{
    trackScratch_.setSize(2, 0);
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

void ProjectAudioSource::pushExternalMidi(const juce::MidiMessage& m)
{
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
    const juce::AudioSourceChannelInfo& info, const Project& project,
    bool injectPanic)
{
    // NOTE: this must render EVERY block while a key is held — the noteOn
    // arrives in exactly one block, and held voices (synth or plugin) only
    // keep sounding if the track keeps being rendered. Gating on "this block
    // has new MIDI events" made notes die after ~10 ms.
    const int numOut = std::min(2, info.buffer->getNumChannels());
    trackScratch_.setSize(2, info.numSamples, false, false, true);

    int trackIndex = 0;
    for (const auto& track : project.tracks)
    {
        const int meterSlot = trackIndex++ & (kMeterTracks - 1);
        if (track.type != Track::Type::Instrument || track.mute
            || ! track.armed)
            continue;

        blockMidi_.clear();
        if (injectPanic)
        {
            // Stop/pause/seek panic must also reach armed tracks being
            // monitored with the transport stopped — otherwise a stuck
            // sustained-loop voice rings forever after stop.
            blockMidi_.addEvent(
                juce::MidiMessage::allNotesOff(1), 0);
            blockMidi_.addEvent(
                juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        }
        for (const auto& m : pendingExternal_)
            blockMidi_.addEvent(m, 0);

        // Live input goes through the instrument plugin when one is loaded,
        // exactly like playback — the internal synth is only the fallback.
        auto* node = nodeFor(track.id);
        const bool hasInstrumentPlugin =
            node != nullptr && node->instrument != nullptr
            && ! node->instrumentBypass
            && info.numSamples <= node->preparedBlock.load(
                                       std::memory_order_acquire);

        if (hasInstrumentPlugin)
        {
            trackScratch_.clear();
            node->instrument->processBlock(trackScratch_, blockMidi_);
        }
        else
        {
            auto* synth = getOrCreateSynth(track.id, project.sampleRate);
            if (synthsRate_ != project.sampleRate)
                synth->setCurrentPlaybackSampleRate(project.sampleRate);
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
        const float left = std::cos(angle) * track.volume * project.masterGain;
        const float right = std::sin(angle) * track.volume * project.masterGain;
        info.buffer->addFrom(0, info.startSample, trackScratch_, 0, 0,
                             info.numSamples, left);
        if (numOut > 1)
            info.buffer->addFrom(1, info.startSample, trackScratch_, 1, 0,
                                 info.numSamples, right);
    }
}

// Rebuilds fx/instrument plugin instances whose configuration changed.
// Must run on the message thread (plugin instantiation is not RT-safe);
// completed chains are swapped into the audio path under a short lock.
void ProjectAudioSource::reconcileNodes(const Project& project, PluginHost& host,
                                        int blockSize, bool deferHeavyPlugins)
{
    const std::scoped_lock lock(trackNodesMutex_);
    // Plugins get the device's real block size as their maximum contract;
    // the audio thread skips blocks larger than this instead of overrunning.
    const int maxBlock = juce::jlimit(256, 8192, blockSize > 0 ? blockSize : 512);

    // Stale queued jobs: drop any whose track/config no longer matches.
    pluginJobs_.erase(std::remove_if(pluginJobs_.begin(), pluginJobs_.end(),
        [&](const PluginJob& job) {
            for (const auto& track : project.tracks)
                if (track.id == job.trackId)
                    return job.sampleRate != project.sampleRate
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
        FxChain chain;
        std::vector<juce::String> states;
        std::shared_ptr<juce::AudioProcessor> instrument;
        juce::String instrumentState;
        std::shared_ptr<juce::AudioProcessor> midiFx; // ezarp insert
    };
    std::map<TrackId, ChainBuild> builds;

    for (const auto& track : project.tracks)
    {
        if (track.fx.empty() && track.instrument.pluginUid.isEmpty()
            && trackNodes_.find(track.id) == trackNodes_.end())
            continue;

        // A node whose chain is UNCHANGED must not be rebuilt: re-preparing
        // an instance takes the wrapper's processMutex while the audio
        // thread holds it in processBlock — seconds of deadline overruns,
        // and CoreAudio then kills the device client (total silence until
        // the device is reopened). Bypass toggles only update flags.
        auto existingIt = trackNodes_.find(track.id);
        if (existingIt != trackNodes_.end() && existingIt->second != nullptr)
        {
            auto& node = *existingIt->second;
            const std::scoped_lock fxLock(node.fxMutex);
            const bool chainSame = node.fx != nullptr
                && node.fx->uids.size() == track.fx.size()
                && [&] {
                       for (int i = 0; i < node.fx->uids.size(); ++i)
                           if (node.fx->uids[i] != track.fx[(size_t) i].pluginUid)
                               return false;
                       return true;
                   }()
                && node.preparedBlock.load(std::memory_order_acquire) == maxBlock
                && node.preparedRate == project.sampleRate;
            const bool instrumentSame =
                node.instrumentUid == track.instrument.pluginUid
                && node.instrumentPrepared
                       == track.instrument.pluginUid.isNotEmpty();
            if (chainSame && instrumentSame)
            {
                // Same instances: publish an updated bypass flags copy.
                if (node.fx != nullptr)
                {
                    auto updated = std::make_shared<FxChain>(*node.fx);
                    for (int i = 0;
                         i < (int) updated->bypass.size()
                                 && i < (int) track.fx.size();
                         ++i)
                        updated->bypass[(size_t) i] =
                            track.fx[(size_t) i].bypass;
                    node.fx = std::move(updated);
                }
                node.instrumentBypass = track.instrument.bypass;
                continue;
            }
        }

        // Deferred mode (project load): a track whose plugins are not live
        // yet gets an EMPTY chain right away (the mix runs dry/synth) and a
        // queued job; each message-loop pass then loads ONE track's plugins,
        // keeping the UI responsive instead of freezing for many seconds.
        if (deferHeavyPlugins
            || jobQueuedFor(track, project.sampleRate, maxBlock))
        {
            enqueuePluginJob(track, project.sampleRate, maxBlock);
            builds[track.id] = ChainBuild{}; // dry until the job lands
            continue;
        }

        // Pool of LIVE instances reusable across this rebuild (same uid →
        // same object): adding/removing one fx must NOT re-instantiate the
        // others — fresh instances reset every preset to default.
        std::multimap<juce::String, std::shared_ptr<juce::AudioProcessor>>
            reusePool;
        std::shared_ptr<juce::AudioProcessor> reuseInstrument;
        if (existingIt != trackNodes_.end() && existingIt->second != nullptr
            && existingIt->second->preparedRate == project.sampleRate
            && existingIt->second->preparedBlock.load(
                   std::memory_order_acquire) == maxBlock)
        {
            auto& oldNode = *existingIt->second;
            const std::scoped_lock oldFxLock(oldNode.fxMutex);
            if (oldNode.fx)
                for (size_t k = 0; k < oldNode.fx->processors.size(); ++k)
                    if (oldNode.fx->processors[k])
                        reusePool.emplace(oldNode.fx->uids[k],
                                          oldNode.fx->processors[k]);
            if (oldNode.instrument != nullptr
                && oldNode.instrumentUid == track.instrument.pluginUid)
                reuseInstrument = oldNode.instrument;
        }

        ChainBuild build;
        for (const auto& slot : track.fx)
        {
            std::shared_ptr<juce::AudioProcessor> proc;
            auto reuseIt = reusePool.find(slot.pluginUid);
            if (reuseIt != reusePool.end())
            {
                proc = reuseIt->second; // same object: preset survives
                reusePool.erase(reuseIt);
            }
            else
            {
                proc = host.create(slot.pluginUid, project.sampleRate, maxBlock);
                if (proc)
                {
                    // Plugins are prepared on the message thread: VST3
                    // prepareToPlay takes the MessageManagerLock and deadlocks
                    // the device if called from the audio thread.
                    proc->setPlayHead(this);
                    activateStereoBuses(*proc);
                    proc->setPlayConfigDetails(2, 2, project.sampleRate, maxBlock);
                    proc->prepareToPlay(project.sampleRate, maxBlock);
                }
                if (slot.state.isNotEmpty())
                {
                    juce::MemoryBlock mb;
                    if (mb.fromBase64Encoding(slot.state))
                        proc->setStateInformation(mb.getData(), (int) mb.getSize());
                }
            }
            build.chain.processors.push_back(proc);
            build.chain.bypass.push_back(slot.bypass);
            build.chain.uids.add(slot.pluginUid);
            build.chain.valid.push_back(true);
            build.states.push_back(slot.state);
            // MIDI insert: instantiate for the RENDER PATH (0-in/0-out),
            // never the audio chain (processFxChain skips it by uid).
            if (slot.pluginUid.containsIgnoreCase("ezarp"))
            {
                build.midiFx = std::move(proc);
                if (build.midiFx)
                {
                    build.midiFx->setPlayHead(this);
                    build.midiFx->setPlayConfigDetails(0, 0,
                                                       project.sampleRate,
                                                       maxBlock);
                    build.midiFx->prepareToPlay(project.sampleRate,
                                                maxBlock);
                }
            }
        }
        if (! track.instrument.pluginUid.isEmpty())
        {
            if (reuseInstrument != nullptr)
            {
                build.instrument = std::move(reuseInstrument);
            }
            else
            {
                build.instrument = host.create(track.instrument.pluginUid,
                                               project.sampleRate, maxBlock);
                if (build.instrument)
                {
                    // Instruments have no input bus: use the plugin's own
                    // layout — forcing 2 inputs leaves VST3s unactivated.
                    build.instrument->setPlayHead(this);
                    activateStereoBuses(*build.instrument);
                    build.instrument->setPlayConfigDetails(
                        build.instrument->getMainBusNumInputChannels(),
                        build.instrument->getMainBusNumOutputChannels(),
                        project.sampleRate, maxBlock);
                    build.instrument->prepareToPlay(project.sampleRate, maxBlock);
                }
                if (track.instrument.state.isNotEmpty())
                {
                    juce::MemoryBlock mb;
                    if (mb.fromBase64Encoding(track.instrument.state))
                        build.instrument->setStateInformation(mb.getData(),
                                                              (int) mb.getSize());
                }
            }
        }
        builds[track.id] = std::move(build);
    }

    // Apply: swap chains into the nodes (audio thread picks up next block).
    for (const auto& track : project.tracks)
    {
        auto it = builds.find(track.id);
        if (it == builds.end())
            continue;
        // Create the node on first use — operator[] inserts a null ptr for
        // unknown tracks, and dereferencing that was a guaranteed crash on
        // the first fx/instrument a track ever got.
        auto& nodePtr = trackNodes_[track.id];
        if (nodePtr == nullptr)
        {
            const juce::SpinLock::ScopedLockType structLock(synthsLock_);
            nodePtr = std::make_unique<TrackAudioNode>();
        }
        auto& node = *nodePtr;
        auto fx = std::make_shared<FxChain>();
        fx->processors = it->second.chain.processors;
        fx->bypass = it->second.chain.bypass;
        fx->uids = it->second.chain.uids;
        fx->valid = it->second.chain.valid;
        {
            const std::scoped_lock fxLock(node.fxMutex);
            node.fx = std::move(fx);
            node.preparedRates.assign(node.fx->processors.size(),
                                      project.sampleRate);
            node.preparedRate = project.sampleRate;
            node.preparedBlock.store(maxBlock, std::memory_order_release);
        }
        {
            const std::scoped_lock instLock(node.instrumentMutex);
            node.instrument = it->second.instrument;
            node.instrumentBypass = track.instrument.bypass;
            node.instrumentPrepared = it->second.instrument != nullptr;
            node.instrumentUid = track.instrument.pluginUid;
        }
        // MIDI insert: the build phase instantiated it from the fx slot —
        // publish under the node's lock.
        {
            const std::scoped_lock mfxLock(node.midiFxMutex);
            node.midiFx = it->second.midiFx;
            node.midiFxPrepared = node.midiFx != nullptr;
            node.midiFxUid = node.midiFx != nullptr ? node.midiFxUid
                                                    : juce::String();
        }
    }

    // Drop nodes of removed tracks (map structure guarded against nodeFor).
    for (auto it = trackNodes_.begin(); it != trackNodes_.end();)
    {
        bool kept = false;
        for (const auto& track : project.tracks)
            if (track.id == it->first) { kept = true; break; }
        if (kept)
        {
            it = std::next(it);
        }
        else
        {
            const juce::SpinLock::ScopedLockType structLock(synthsLock_);
            it = trackNodes_.erase(it);
        }
    }

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
    const std::scoped_lock lock(trackNodesMutex_);

    auto& nodePtr = trackNodes_[job.trackId];
    if (nodePtr == nullptr)
    {
        const juce::SpinLock::ScopedLockType structLock(synthsLock_);
        nodePtr = std::make_unique<TrackAudioNode>();
    }
    auto& node = *nodePtr;

    // Reuse live instances with the same uid (keeps presets alive when a
    // job re-applies a chain that partially survived).
    std::multimap<juce::String, std::shared_ptr<juce::AudioProcessor>> reusePool;
    if (node.fx != nullptr)
        for (size_t k = 0; k < node.fx->processors.size(); ++k)
            if (node.fx->processors[k])
                reusePool.emplace(node.fx->uids[k], node.fx->processors[k]);

    auto fx = std::make_shared<FxChain>();
    for (const auto& slot : job.fx)
    {
        std::shared_ptr<juce::AudioProcessor> proc;
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
                proc->setPlayConfigDetails(2, 2, job.sampleRate, job.maxBlock);
                proc->prepareToPlay(job.sampleRate, job.maxBlock);
            }
            if (slot.state.isNotEmpty())
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding(slot.state))
                    proc->setStateInformation(mb.getData(), (int) mb.getSize());
            }
        }
        fx->processors.push_back(std::move(proc));
        fx->bypass.push_back(slot.bypass);
        fx->uids.add(slot.pluginUid);
        fx->valid.push_back(true);
    }
    std::shared_ptr<juce::AudioProcessor> instrument;
    if (! job.instrument.pluginUid.isEmpty())
    {
        if (node.instrument != nullptr
            && node.instrumentUid == job.instrument.pluginUid)
        {
            instrument = node.instrument; // same object: patch survives
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
            if (job.instrument.state.isNotEmpty())
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding(job.instrument.state))
                    instrument->setStateInformation(mb.getData(),
                                                    (int) mb.getSize());
            }
        }
    }

    {
        const std::scoped_lock fxLock(node.fxMutex);
        node.fx = std::move(fx);
        node.preparedRates.assign(node.fx->processors.size(),
                                  job.sampleRate);
        node.preparedRate = job.sampleRate;
        node.preparedBlock.store(job.maxBlock, std::memory_order_release);
    }
    {
        const std::scoped_lock instLock(node.instrumentMutex);
        node.instrument = std::move(instrument);
        node.instrumentBypass = job.instrument.bypass;
        node.instrumentPrepared = node.instrument != nullptr;
        node.instrumentUid = job.instrument.pluginUid;
    }
}

TrackAudioNode* ProjectAudioSource::nodeFor(TrackId trackId)
{
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    auto it = trackNodes_.find(trackId);
    return it != trackNodes_.end() ? it->second.get() : nullptr;
}

// Plugin-parameter automation (§7.12 v1.5): evaluates every param lane of
// the track at the block position and pushes the value into the target
// processor. Audio-thread safe: JUCE VST3 parameter changes are queued.
void ProjectAudioSource::applyParamAutomation(const Track& track,
                                              TrackAudioNode* node,
                                              std::int64_t blockStart,
                                              double rate)
{
    if (track.paramLanes.empty() || node == nullptr)
        return;

    std::shared_ptr<const FxChain> fx;
    std::shared_ptr<juce::AudioProcessor> instrument;
    {
        const std::scoped_lock fxLock(node->fxMutex);
        fx = node->fx;
    }
    {
        const std::scoped_lock instLock(node->instrumentMutex);
        instrument = node->instrument;
    }

    for (const auto& lane : track.paramLanes)
    {
        std::shared_ptr<juce::AudioProcessor> proc;
        if (lane.slotKind == 1)
            proc = instrument;
        else if (fx != nullptr && lane.slotIndex < (int) fx->processors.size())
            proc = fx->processors[(size_t) lane.slotIndex];
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
    const std::scoped_lock fxLock(node->fxMutex);
    if (node->fx == nullptr || slotIndex < 0
        || slotIndex >= (int) node->fx->processors.size())
        return nullptr;
    return node->fx->processors[(size_t) slotIndex];
}

std::shared_ptr<juce::AudioProcessor> ProjectAudioSource::instrumentProcessorFor(
    TrackId trackId)
{
    auto* node = nodeFor(trackId);
    if (node == nullptr)
        return nullptr;
    const std::scoped_lock instLock(node->instrumentMutex);
    return node->instrument;
}

std::shared_ptr<juce::AudioProcessor> ProjectAudioSource::midiFxProcessorFor(
    TrackId trackId)
{
    auto* node = nodeFor(trackId);
    if (node == nullptr)
        return nullptr;
    const std::scoped_lock mfxLock(node->midiFxMutex);
    return node->midiFx;
}

void ProjectAudioSource::processFxChain(TrackAudioNode& node, TrackId trackId,
                                        double rate, int numSamples)
{
    const std::scoped_lock fxLock(node.fxMutex);
    auto fx = node.fx; // keep the chain alive even if reconcile swaps it now
    if (fx == nullptr)
        return;
    // A block larger than the prepared maximum would overrun the plugins'
    // internal buffers — skip the block rather than process garbage.
    if (numSamples > node.preparedBlock.load(std::memory_order_acquire))
        return;

    // Processors arrive prepared (reconcileNodes, message thread). A chain
    // prepared for a different rate is skipped until the next reconcile.
    for (size_t i = 0; i < fx->processors.size(); ++i)
    {
        if (fx->bypass[i])
            continue;
        auto* proc = fx->processors[i].get();
        if (proc == nullptr)
            continue;
        // MIDI inserts (EZArpeggio) are driven from the instrument render
        // path with the track's real MIDI stream — never here (audio-only).
        if (fx->uids[(size_t) i].containsIgnoreCase("ezarp"))
            continue;
        if (i >= node.preparedRates.size() || node.preparedRates[i] != rate)
            continue;
        juce::MidiBuffer emptyMidi;
        proc->processBlock(trackScratch_, emptyMidi);
    }
}

void ProjectAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    if (info.buffer == nullptr)
        return;

    // Live MIDI monitoring: empty the input queue so the notes played reach
    // the armed instrument tracks' synths.
    drainExternalMidi();

    const auto project = std::atomic_load(&project_);
    // A queued panic (stop/pause/seek) injects All-NOTES-OFF + pedal-off for
    // every track while audio runs — plugins (SINE Player, ZENOLOGY, LABS)
    // otherwise keep a stuck voice sounding forever.
    const bool injectPanic = panicRequested_.exchange(false);

    if (! playing_.load() || project == nullptr)
    {
        info.clearActiveBufferRegion();
        if (project != nullptr)
            renderArmedInstrumentMonitoring(info, *project, injectPanic);
        return;
    }

    const double rate = project->sampleRate;
    const std::int64_t blockStart = positionSamples_.fetch_add(info.numSamples);
    const std::int64_t endSamples = (std::int64_t) (project->durationSeconds() * rate);

    // While recording, keep the transport rolling past the project end so
    // the capture is not cut short by the auto-stop.
    if (blockStart >= endSamples && ! recording_.load())
    {
        playing_.store(false);
        info.clearActiveBufferRegion();
        return;
    }

    info.clearActiveBufferRegion();

    bool anySolo = false;
    for (const auto& track : project->tracks)
        anySolo = anySolo || track.solo;

    const int numOut = std::min(2, info.buffer->getNumChannels());
    trackScratch_.setSize(2, info.numSamples, false, false, true);

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

        // Instrument tracks: render their MIDI clips through the internal
        // synth or through the loaded instrument plugin.
        if (track.type == Track::Type::Instrument)
        {
            blockMidi_.clear();
            if (injectPanic)
            {
                // CC123 all-notes-off + CC64 0: kills stuck/held voices on
                // plugins and resets the internal synth's sustain state.
                blockMidi_.addEvent(
                    juce::MidiMessage::allNotesOff(1), 0);
                blockMidi_.addEvent(
                    juce::MidiMessage::controllerEvent(1, 64, 0), 0);
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
                    if (on >= blockStart + info.numSamples)
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
                    if (off < blockStart + info.numSamples)
                        blockMidi_.addEvent(
                            juce::MidiMessage::noteOff(1, note.pitch),
                            (int) (off - blockStart));
                }
            }

            auto* node = nodeFor(track.id);
            applyParamAutomation(track, node, blockStart, rate);
            const bool hasInstrumentPlugin =
                node != nullptr && node->instrument != nullptr
                && ! node->instrumentBypass
                && info.numSamples <= node->preparedBlock.load(
                                           std::memory_order_acquire);

            // Sustain pedal (CC64) lane → MIDI CC events at the block head.
            // STEP semantics: on/off, never a fade between points.
            if (auto* susCurve = track.sustainAutomation.get())
            {
                const int cc = (int) std::lround(
                    evalStepAt(susCurve, blockStart, rate) * 127.0f);
                int& last = lastSustainCc_[track.id];
                if (cc != last)
                {
                    blockMidi_.addEvent(
                        juce::MidiMessage::controllerEvent(1, 64, cc), 0);
                    last = cc;
                }
            }
            if (const char* dbg = std::getenv("CADENCE_MIDI_DBG"))
            {
                if ((void*) dbg != nullptr && blockMidi_.getNumEvents() > 0)
                    std::fprintf(stderr,
                                 "[midi-dbg] node=%p inst=%p bypass=%d "
                                 "preparedBlock=%d numSamples=%d events=%d\n",
                                 (void*) node,
                                 node ? (void*) node->instrument.get() : nullptr,
                                 node ? (int) node->instrumentBypass : -1,
                                 node ? node->preparedBlock.load() : -1,
                                 info.numSamples, blockMidi_.getNumEvents());
            }

            // MIDI insert (arpeggiator): the instrument hears the
            // TRANSFORMED stream — the original clip notes are consumed.
            const bool midiFxActive =
                node != nullptr && node->midiFx != nullptr
                && ! node->midiFxBypass && node->midiFxPrepared;
            if (std::getenv("CADENCE_ARP_DBG") != nullptr)
                std::fprintf(stderr,
                             "[arp-dbg] node=%p mfx=%p prepared=%d "
                             "bypass=%d numSamples=%d events=%d\n",
                             (void*) node,
                             node ? (void*) node->midiFx.get() : nullptr,
                             node ? (int) node->midiFxPrepared : -1,
                             node ? (int) node->midiFxBypass : -1,
                             info.numSamples, blockMidi_.getNumEvents());
            if (midiFxActive)
            {
                midiFxSilent_.setSize(1, info.numSamples, false, false, true);
                midiFxSilent_.clear();
                node->midiFx->processBlock(midiFxSilent_, blockMidi_);
            }

            if (hasInstrumentPlugin)
            {
                trackScratch_.clear();
                node->instrument->processBlock(trackScratch_, blockMidi_);

                // MIDI generators (EZDrums): whatever the plugin
                // "performed" this block is written into the armed
                // track's MIDI take while recording — the machine records
                // itself into a normal clip, exactly like a player would.
                if (recording_.load(std::memory_order_relaxed) && track.armed)
                {
                    const bool ezdrumsGen = dynamic_cast<ezdrums::MidiGenerator*>(
                                                node->instrument.get())
                                            != nullptr;
                    if (ezdrumsGen)
                    {
                        auto* gen = dynamic_cast<ezdrums::MidiGenerator*>(
                            node->instrument.get());
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
                                    const std::scoped_lock lock(
                                        capture->midiMutex);
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
                                const std::scoped_lock lock(
                                    capture->midiMutex);
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
                auto* synth = getOrCreateSynth(track.id, rate);
                if (synthsRate_ != rate)
                    synth->setCurrentPlaybackSampleRate(rate);
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
                synth->renderNextBlock(trackScratch_, blockMidi_, 0, info.numSamples);
            }

            if (node != nullptr && node->fx)
                processFxChain(*node, track.id, rate, info.numSamples);

            // Volume + pan automation (§7.12, Read always active): gain
            // ramps across the block from the curves, replacing the static
            // volume/pan.
            const auto* volCurve = track.volumeAutomation.get();
            const auto* panCurve = track.panAutomation.get();
            const float v0 = volumeAt(track, volCurve, blockStart, rate);
            const float v1 = volumeAt(track, volCurve,
                                      blockStart + info.numSamples, rate);
            const float pan0 = panCurve
                ? evalCurveAt(panCurve, blockStart, rate, 0.5f) * 2.0f - 1.0f
                : track.pan;
            const float pan1 = panCurve
                ? evalCurveAt(panCurve, blockStart + info.numSamples, rate,
                              0.5f) * 2.0f - 1.0f
                : track.pan;

            const float ang0 = (juce::jlimit(-1.0f, 1.0f, pan0) + 1.0f)
                               * juce::MathConstants<float>::pi * 0.25f;
            const float ang1 = (juce::jlimit(-1.0f, 1.0f, pan1) + 1.0f)
                               * juce::MathConstants<float>::pi * 0.25f;
            const float left0 = std::cos(ang0) * project->masterGain * v0;
            const float left1 = std::cos(ang1) * project->masterGain * v1;
            const float right0 = std::sin(ang0) * project->masterGain * v0;
            const float right1 = std::sin(ang1) * project->masterGain * v1;
            trackScratch_.applyGainRamp(0, 0, info.numSamples, left0, left1);
            trackScratch_.applyGainRamp(1, 0, info.numSamples, right0, right1);

            for (int ch = 0; ch < 2; ++ch)
                trackPeak = std::max(trackPeak,
                    absMax(trackScratch_.getReadPointer(ch, 0), info.numSamples));

            info.buffer->addFrom(0, info.startSample, trackScratch_, 0, 0,
                                 info.numSamples, 1.0f);
            if (numOut > 1)
                info.buffer->addFrom(1, info.startSample, trackScratch_, 1, 0,
                                     info.numSamples, 1.0f);

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
            const std::int64_t to = std::min(blockStart + (std::int64_t) info.numSamples, clipEnd);
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
                                  blockStart + info.numSamples, rate);
        const float pan0 = panCurve
            ? evalCurveAt(panCurve, blockStart, rate, 0.5f) * 2.0f - 1.0f
            : track.pan;
        const float pan1 = panCurve
            ? evalCurveAt(panCurve, blockStart + info.numSamples, rate,
                          0.5f) * 2.0f - 1.0f
            : track.pan;

        auto* node = nodeFor(track.id);
        applyParamAutomation(track, node, blockStart, rate);
        if (node != nullptr && node->fx)
            processFxChain(*node, track.id, rate, info.numSamples);

        // Stereo sources pan as a balance (see panGains): a centred stereo
        // clip keeps its level, matching Cubase's stereo panner.
        const bool stereo = hasStereoSource(*project, track);
        float left0 = 0.0f, right0 = 0.0f, left1 = 0.0f, right1 = 0.0f;
        panGains(stereo, pan0, project->masterGain * v0, left0, right0);
        panGains(stereo, pan1, project->masterGain * v1, left1, right1);
        trackScratch_.applyGainRamp(0, 0, info.numSamples, left0, left1);
        trackScratch_.applyGainRamp(1, 0, info.numSamples, right0, right1);

        for (int ch = 0; ch < 2; ++ch)
            trackPeak = std::max(trackPeak,
                absMax(trackScratch_.getReadPointer(ch, 0), info.numSamples));

        info.buffer->addFrom(0, info.startSample, trackScratch_, 0, 0,
                             info.numSamples, 1.0f);
        if (numOut > 1)
            info.buffer->addFrom(1, info.startSample, trackScratch_, 1, 0,
                                 info.numSamples, 1.0f);

        trackPeaks_[meterSlot].store(std::bit_cast<std::uint32_t>(trackPeak),
                                     std::memory_order_relaxed);
    }

    // Metronome click: accent on the beat that starts a bar (4/4 fixed until
    // the tempo/time-signature editing lands).
    if (metronome_.load(std::memory_order_relaxed))
    {
        const double beatSamples = 60.0 / project->tempoBpm * rate;
        std::int64_t beat = (std::int64_t) std::ceil((double) blockStart / beatSamples);
        for (; beat * beatSamples < blockStart + info.numSamples; ++beat)
        {
            const std::int64_t clickPos = (std::int64_t) (beat * beatSamples) - blockStart;
            const int offset = (int) clickPos;
            const int clickLen = (int) std::min<std::int64_t>(
                (std::int64_t) (0.03 * rate), info.numSamples - offset);
            const bool accent = (beat % 4) == 0;
            const double freq = accent ? 1568.0 : 880.0;
            for (int i = 0; i < clickLen; ++i)
            {
                const float env = 1.0f - (float) i / (float) clickLen;
                const float sample = (float) (0.4 * env
                    * std::sin(juce::MathConstants<double>::twoPi * freq * i / rate));
                for (int ch = 0; ch < numOut; ++ch)
                    info.buffer->addSample(ch, info.startSample + offset + i, sample);
            }
        }
    }

    for (int ch = 0; ch < numOut; ++ch)
        masterPeaks_[ch].store(
            std::bit_cast<std::uint32_t>(
                absMax(info.buffer->getReadPointer(ch, info.startSample), info.numSamples)),
            std::memory_order_relaxed);
}

void ProjectAudioSource::allNotesOff()
{
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    for (auto& [id, synth] : synths_)
    {
        synth->allNotesOff(1, true);
        // A pedal left down (take ending pedaled, live CC) would hold every
        // future noteOff — reset the voices' sustain state too.
        for (int v = 0; v < synth->getNumVoices(); ++v)
            if (auto* voice = synth->getVoice(v))
                voice->controllerMoved(64, 0);
    }
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
    const std::scoped_lock lock(trackNodesMutex_);
    auto it = trackNodes_.find(trackId);
    return it != trackNodes_.end() && it->second != nullptr
        && it->second->instrument != nullptr;
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
        if (device != nullptr)
        {
            engine.transport_->prepareToPlay(device->getCurrentBufferSizeSamples(),
                                             device->getCurrentSampleRate());
            engine.preparePreview(device->getCurrentBufferSizeSamples(),
                                  device->getCurrentSampleRate());
        }
        else
        {
            engine.transport_->releaseResources();
            engine.releasePreview();
        }
    }

    void audioDeviceStopped() override
    {
        engine.transport_->releaseResources();
        engine.releasePreview();
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numFrames,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
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
                        const std::scoped_lock lock(capture->midiMutex);
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
    outputCallback_ = std::make_unique<OutputCallback>(*this);
    midiInCallback_ = std::make_unique<MidiInCallback>(*this);
    previewWatchdog_ = std::make_unique<PreviewWatchdog>(*this);
    aiService_ = std::make_unique<ai::Service>();
}

Engine::~Engine()
{
    aiCancel_.store(true, std::memory_order_release);
    if (aiThread_.joinable())
        aiThread_.join();
    transport_->releaseResources();
    deviceManager_.removeAudioCallback(outputCallback_.get());
    deviceManager_.closeAudioDevice();
    stopMessageLoop();
}

void Engine::initialiseAudioInBackground()
{
    // Opens the audio device on a background thread. The UI stays
    // responsive even if CoreAudio takes seconds to initialise (heavy
    // devices, HAL in transitional state after connect/disconnect).
    initThread_ = std::thread([this] {
        const auto r = initialise();
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
    deviceManager_.closeAudioDevice();
    return initialise();
}

juce::Result Engine::initialise()
{
    juce::Result error = juce::Result::ok();
    runOnMessageThread([&] {
        const auto err = deviceManager_.initialiseWithDefaultDevices(2, 2);
        error = err.isEmpty() ? juce::Result::ok() : juce::Result::fail(err);
    });
    if (error.failed())
    {
        lastErrorUtf8_ = error.getErrorMessage().toStdString();
        return juce::Result::fail(error.getErrorMessage());
    }

    deviceManager_.addAudioCallback(outputCallback_.get());
    return juce::Result::ok();
}

bool Engine::isInitialised() const
{
    return deviceManager_.getCurrentAudioDevice() != nullptr;
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
        auto setup = previous;
        if (outputDevice.isNotEmpty())
            setup.outputDeviceName = outputDevice;
        if (inputDevice.isNotEmpty())
            setup.inputDeviceName = inputDevice;
        if (sampleRate > 0)
            setup.sampleRate = sampleRate;
        if (bufferSize > 0)
            setup.bufferSize = bufferSize;

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
    });
    return result;
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
    if (auto state = deviceManager_.createStateXml())
        root->setProperty("deviceState", state->createDocument(juce::String()));
    else
        root->setProperty("deviceState", "");

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
    if (stateText.isNotEmpty())
    {
        if (auto xml = juce::parseXML(stateText))
            deviceManager_.initialise(2, 2, xml.get(), true);
        else
            deviceManager_.initialiseWithDefaultDevices(2, 2);
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
    return {};
}

int Engine::currentBufferSizeSamples() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();
    return 0;
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
    transport_->allNotesOff();
    transport_->requestPanic();
    transport_->pause();
}
void Engine::stop()
{
    finalizeRecording();
    transport_->allNotesOff();
    transport_->requestPanic();
    transport_->stop();
}
void Engine::seek(double seconds)
{
    transport_->allNotesOff();
    transport_->requestPanic();
    transport_->seek(seconds);
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
            editorWindows_.erase(it);
            it = editorWindows_.end();
        }
        if (it == editorWindows_.end())
            it = editorWindows_
                     .emplace(key,
                              EditorEntry{std::make_unique<PluginEditorWindow>(
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
            editorWindows_.erase(it);
            it = editorWindows_.end();
        }
        if (it == editorWindows_.end())
            it = editorWindows_
                     .emplace(key,
                              EditorEntry{std::make_unique<PluginEditorWindow>(
                                              proc),
                                          proc})
                     .first;
        it->second.window->setVisible(true);
        it->second.window->toFront(true);
    });
    return result;
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
// allows, at the project sample rate (no resampling, no artifacts). FX
// chains and instrument plugins are included via a dedicated reconcile.
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

    ProjectAudioSource renderSource;
    renderSource.setProject(project);
    // The offline source needs its own plugin nodes (fx chains + instrument
    // plugins) — reconcile on this thread before rendering. Runs on the
    // caller (message thread in the app), which is where instantiation
    // belongs anyway.
    renderSource.reconcileNodes(*project, pluginHost_, 512, false);
    // EZOrquestra decodes its program on a loader thread — the offline
    // render must wait for it or the render is silent.
    ezorq::waitProgramsLoaded();
    renderSource.play();

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

    constexpr int blockSize = 512;
    juce::AudioBuffer<float> block(2, blockSize);

    for (int pos = 0; pos < totalSamples && renderSource.isPlaying(); pos += blockSize)
    {
        block.clear();
        const juce::AudioSourceChannelInfo info { &block, 0, std::min(blockSize, totalSamples - pos) };
        renderSource.getNextAudioBlock(info);
        writer->writeFromAudioSampleBuffer(block, 0, info.numSamples);
    }

    writer.reset();
    if (stream != nullptr)
        stream->flush();

    return juce::Result::ok();
}

} // namespace cadence

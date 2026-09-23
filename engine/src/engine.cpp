#include "cadence/engine.hpp"

#include <bit>
#include <cmath>

namespace cadence {

namespace {
constexpr double kMinClipLength = 0.05; // seconds; one gesture cannot vanish a clip

float absMax(const float* data, int numSamples)
{
    const auto range = juce::Range<float>::findMinAndMax(data, numSamples);
    return std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
}
} // namespace

// Test hook definition (used by RecorderCallback; see setDebugLoopback).
std::atomic<bool> Engine::debugLoopback_ { false };

// ---------- Native instrument (v8/v9): polyphonic piano-ish voice ----------

namespace {

struct PianoSound : juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

struct PianoVoice : juce::SynthesiserVoice
{
    static constexpr int kPartials = 5;

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<PianoSound*>(sound) != nullptr;
    }

    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound*, int) override
    {
        const double hz = 440.0 * std::pow(2.0, (midiNoteNumber - 69) / 12.0);
        for (int p = 0; p < kPartials; ++p)
        {
            angle[p] = 0.0;
            delta[p] = juce::MathConstants<double>::twoPi * hz * (p + 1)
                       / sampleRate_;
        }
        // Harmonic mix (fixed), velocity scales the whole thing.
        amp[0] = 1.00; amp[1] = 0.55; amp[2] = 0.32; amp[3] = 0.20; amp[4] = 0.12;
        level = velocity * 0.4;

        // Bass notes ring longer than treble notes (piano-like).
        const double tau = juce::jlimit(0.8, 6.0, 6.0 - (midiNoteNumber - 21) * 0.06);
        decayPerSample = std::exp(-1.0 / (tau * sampleRate_));

        env = 1.0;
        releasing = false;
        active = true;
    }

    void stopNote(float, bool allowTailOff) override
    {
        releasing = true;
        if (! allowTailOff)
            env *= 0.05;
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    bool isVoiceActive() const override { return active; }

    void setCurrentPlaybackSampleRate(double rate) override
    {
        sampleRate_ = rate > 0.0 ? rate : 48000.0;
    }

    void renderNextBlock(juce::AudioBuffer<float>& output, int startSample,
                         int numSamples) override
    {
        if (! active)
            return;

        const double releaseDecay = std::exp(-1.0 / (0.18 * sampleRate_));

        for (int i = 0; i < numSamples; ++i)
        {
            double s = 0.0;
            for (int p = 0; p < kPartials; ++p)
            {
                s += amp[p] * std::sin(angle[p]);
                angle[p] += delta[p];
            }
            const float sample = (float) (s * level * env);
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample(ch, startSample + i, sample);

            env *= decayPerSample;
            if (releasing)
                env *= releaseDecay;
        }

        if (env < 0.0004)
        {
            active = false;
            clearCurrentNote();
        }
    }

    double sampleRate_ = 48000.0;
    double angle[kPartials] = {};
    double delta[kPartials] = {};
    double amp[kPartials] = {};
    double env = 1.0, decayPerSample = 1.0, level = 0.0;
    bool releasing = false, active = false;
};

} // namespace

// ---------- PluginHost ----------

void PluginHost::setup()
{
    formatManager_.addDefaultFormats();
}

void PluginHost::scan()
{
    for (auto* format : formatManager_.getFormats())
    {
        const juce::FileSearchPath searchPaths(
            format->searchPathsForPlugins(format->getDefaultLocationsToSearch(),
                                          true, false)
                .joinIntoString(";"));
        juce::KnownPluginList newList;
        juce::PluginDirectoryScanner scanner(newList, *format, searchPaths,
                                             true, deadMansPedalFile_);
        juce::String name;
        while (scanner.scanNextFile(true, name))
        {
        }
        for (int i = 0; i < newList.getNumTypes(); ++i)
            if (auto* desc = newList.getType(i))
                known_.addType(*desc);
    }
}

// ---------- ProjectAudioSource ----------

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

// Live monitoring (§7.11): with the transport stopped, armed instrument
// tracks still voice the incoming MIDI notes through their synth.
void ProjectAudioSource::renderArmedInstrumentMonitoring(
    const juce::AudioSourceChannelInfo& info, const Project& project)
{
    if (pendingExternal_.empty())
        return;

    const int numOut = std::min(2, info.buffer->getNumChannels());
    trackScratch_.setSize(2, info.numSamples, false, false, true);

    for (const auto& track : project.tracks)
    {
        if (track.type != Track::Type::Instrument || track.mute
            || ! track.armed)
            continue;

        auto* synth = getOrCreateSynth(track.id, project.sampleRate);
        if (synthsRate_ != project.sampleRate)
            synth->setCurrentPlaybackSampleRate(project.sampleRate);

        blockMidi_.clear();
        for (const auto& m : pendingExternal_)
            blockMidi_.addEvent(m, 0);

        trackScratch_.clear();
        synth->renderNextBlock(trackScratch_, blockMidi_, 0, info.numSamples);

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

TrackAudioNode* ProjectAudioSource::nodeFor(TrackId trackId)
{
    const juce::SpinLock::ScopedLockType lock(synthsLock_);
    auto it = trackNodes_.find(trackId);
    return it != trackNodes_.end() ? it->second.get() : nullptr;
}

void ProjectAudioSource::processFxChain(TrackAudioNode& node, TrackId trackId,
                                        double rate, int numSamples)
{
    if (node.fx == nullptr)
        return;

    auto* audioTrackNode = nodeFor(trackId);
    if (audioTrackNode == nullptr)
        return;

    for (size_t i = 0; i < node.fx->processors.size() && i < node.preparedRates.size(); ++i)
    {
        auto* proc = node.fx->processors[i].get();
        if (proc == nullptr || node.fx->bypass[i])
            continue;
        if (node.preparedRates[i] != rate)
        {
            proc->prepareToPlay(rate, 512);
            proc->setPlayConfigDetails(2, 2, rate, 512);
            node.preparedRates[i] = rate;
        }
    }

    // processBlock of each fx in order over the whole scratch buffer.
    for (size_t i = 0; i < node.fx->processors.size(); ++i)
    {
        if (node.fx->bypass[i])
            continue;
        auto* proc = node.fx->processors[i].get();
        if (proc == nullptr)
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

    if (! playing_.load() || project == nullptr)
    {
        info.clearActiveBufferRegion();
        renderArmedInstrumentMonitoring(info, *project);
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
            if (track.armed)
                for (const auto& m : pendingExternal_)
                    blockMidi_.addEvent(m, 0);
            for (const auto& clip : track.clips)
            {
                if (clip.muted || ! clip.isMidi() || clip.midi == nullptr)
                    continue;
                const std::int64_t clipStartSamples = (std::int64_t) (clip.startSeconds * rate);
                for (const auto& note : clip.midi->notes)
                {
                    const std::int64_t on = clipStartSamples
                        + (std::int64_t) (note.startSeconds * rate);
                    const std::int64_t off = on + (std::int64_t) (note.lengthSeconds * rate);
                    if (on >= blockStart + info.numSamples)
                        break; // notes are ordered by start; later notes too
                    if (off <= blockStart)
                        continue;

                    if (on >= blockStart)
                        blockMidi_.addEvent(
                            juce::MidiMessage::noteOn(1, note.pitch, note.velocity),
                            (int) (on - blockStart));

                    const auto offOffset = (int) std::min<std::int64_t>(
                        off - blockStart, info.numSamples - 1);
                    blockMidi_.addEvent(
                        juce::MidiMessage::noteOff(1, note.pitch), offOffset);
                }
            }

            auto* node = nodeFor(track.id);
            const bool hasInstrumentPlugin = node != nullptr && node->instrument != nullptr
                                             && ! node->instrumentBypass;

            if (hasInstrumentPlugin)
            {
                trackScratch_.clear();
                node->instrument->processBlock(trackScratch_, blockMidi_);
            }
            else
            {
                auto* synth = getOrCreateSynth(track.id, rate);
                if (synthsRate_ != rate)
                    synth->setCurrentPlaybackSampleRate(rate);
                trackScratch_.clear();
                synth->renderNextBlock(trackScratch_, blockMidi_, 0, info.numSamples);
            }

            if (node != nullptr && node->fx)
                processFxChain(*node, track.id, rate, info.numSamples);

            for (int ch = 0; ch < 2; ++ch)
                trackPeak = std::max(trackPeak,
                    absMax(trackScratch_.getReadPointer(ch, 0), info.numSamples));

            const float angle = (juce::jlimit(-1.0f, 1.0f, track.pan) + 1.0f)
                                * juce::MathConstants<float>::pi * 0.25f;
            const float leftG = std::cos(angle) * track.volume * project->masterGain;
            const float rightG = std::sin(angle) * track.volume * project->masterGain;
            info.buffer->addFrom(0, info.startSample, trackScratch_, 0, 0,
                                 info.numSamples, leftG);
            if (numOut > 1)
                info.buffer->addFrom(1, info.startSample, trackScratch_, 1, 0,
                                     info.numSamples, rightG);

            trackPeaks_[meterSlot].store(std::bit_cast<std::uint32_t>(trackPeak),
                                         std::memory_order_relaxed);
            continue;
        }

        // Audio tracks: accumulate clips into scratch, then fx, then pan.
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

        if (auto* node = nodeFor(track.id))
        {
            if (node->fx)
                processFxChain(*node, track.id, rate, info.numSamples);
            for (int ch = 0; ch < 2; ++ch)
                trackPeak = std::max(trackPeak,
                    absMax(trackScratch_.getReadPointer(ch, 0), info.numSamples));
        }

        const float angle = (juce::jlimit(-1.0f, 1.0f, track.pan) + 1.0f)
                            * juce::MathConstants<float>::pi * 0.25f;
        const float left = std::cos(angle) * track.volume * project->masterGain;
        const float right = std::sin(angle) * track.volume * project->masterGain;

        info.buffer->addFrom(0, info.startSample, trackScratch_, 0, 0, info.numSamples, left);
        if (numOut > 1)
            info.buffer->addFrom(1, info.startSample, trackScratch_, 1, 0, info.numSamples, right);

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
        synth->allNotesOff(1, true);
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
            engine.transport_->prepareToPlay(device->getCurrentBufferSizeSamples(),
                                             device->getCurrentSampleRate());
        else
            engine.transport_->releaseResources();
    }

    void audioDeviceStopped() override
    {
        engine.transport_->releaseResources();
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

// ---------- Engine ----------

Engine::Engine()
{
    formatManager_.registerBasicFormats();
    transport_ = std::make_unique<ProjectAudioSource>();
    outputCallback_ = std::make_unique<OutputCallback>(*this);
    midiInCallback_ = std::make_unique<MidiInCallback>(*this);
    startMessageLoop();
}

Engine::~Engine()
{
    transport_->releaseResources();
    deviceManager_.removeAudioCallback(outputCallback_.get());
    deviceManager_.closeAudioDevice();
    stopMessageLoop();
}

void Engine::startMessageLoop()
{
    juce::MessageManager::getInstance();
    if (messageLoopRunning_.exchange(true))
        return;
    messageThread_ = std::thread([] {
        // Blocks until stopDispatchLoop(); JUCE 8 removed runDispatchLoopUntil.
        juce::MessageManager::getInstance()->runDispatchLoop();
    });
}

void Engine::stopMessageLoop()
{
    if (! messageLoopRunning_.exchange(false))
        return;
    juce::MessageManager::getInstance()->stopDispatchLoop();
    if (messageThread_.joinable())
        messageThread_.join();
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
    juce::Result result = juce::Result::ok();
    runOnMessageThread([&] {
        auto setup = deviceManager_.getAudioDeviceSetup();
        if (outputDevice.isNotEmpty())
            setup.outputDeviceName = outputDevice;
        if (inputDevice.isNotEmpty())
            setup.inputDeviceName = inputDevice;
        if (sampleRate > 0)
            setup.sampleRate = sampleRate;
        if (bufferSize > 0)
            setup.bufferSize = bufferSize;

        const auto error = deviceManager_.setAudioDeviceSetup(setup, true);
        result = error.isNotEmpty()
                     ? juce::Result::fail("device config: " + error)
                     : juce::Result::ok();
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

    transport_->pushExternalMidi(message); // live monitoring

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

void Engine::publish(std::shared_ptr<const Project> project)
{
    std::atomic_store(&project_, project);
    transport_->setProject(std::move(project));
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
        clip->midi = std::move(midi);
        return true;
    });
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
    for (const auto& track : project->tracks)
    {
        if (! track.armed)
            continue;
        if (track.type == Track::Type::Instrument)
            anyMidiArmed = true;
        else if (deviceInputChannelCount() == 0)
            return fail("el dispositivo activo no tiene entradas — elige uno con "
                        "micrófono en Preferencias (engranaje)");
        list->push_back(std::make_shared<TrackCapture>(
            track.id, juce::jlimit(0, 1, track.inputChannel), captureRate, capacity,
            track.type == Track::Type::Instrument));
    }
    if (list->empty())
        return fail("no armed tracks to record");
    if (anyMidiArmed && selectedMidiInputId_.isEmpty()
        && ! debugLoopback_.load())
        return fail("no hay entrada MIDI seleccionada — elige una en "
                    "Preferencias (engranaje)");

    recordStartSeconds_.store(transport_->positionSeconds());
    recordStartSamples_.store(transport_->positionSamples());
    std::atomic_store(&captures_, std::static_pointer_cast<const CaptureList>(list));
    recording_.store(true);
    transport_->setRecording(true);
    transport_->play();
    return juce::Result::ok();
}

juce::Result Engine::stopRecord()
{
    finalizeRecording();
    transport_->pause();
    return juce::Result::ok();
}

void Engine::finalizeRecording()
{
    const std::scoped_lock lock(editMutex_);
    if (! recording_.exchange(false))
        return;
    transport_->setRecording(false);

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
    const std::int64_t elapsedSamples = (std::int64_t) (elapsed * rate);
    const std::int64_t minNoteSamples = (std::int64_t) (0.05 * rate);

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

        auto midi = std::make_shared<MidiClipData>();
        std::map<int, std::pair<std::int64_t, float>> openNotes;
        for (const auto& e : events)
        {
            const int type = e.status & 0xF0;
            if (type == 0x90 && e.d2 > 0)
            {
                openNotes[e.d1] = { (std::int64_t) e.sampleFromRecordStart,
                                    e.d2 / 127.0f };
            }
            else if (type == 0x80 || (type == 0x90 && e.d2 == 0))
            {
                auto it = openNotes.find(e.d1);
                if (it == openNotes.end())
                    continue;
                const std::int64_t on = it->second.first;
                const std::int64_t off = std::min<std::int64_t>(
                    (std::int64_t) e.sampleFromRecordStart, elapsedSamples);
                const std::int64_t len = std::max(minNoteSamples, off - on);
                midi->notes.push_back({ on / (double) rate,
                                        len / (double) rate,
                                        e.d1, it->second.second });
                openNotes.erase(it);
            }
        }
        for (const auto& [pitch, onv] : openNotes)
        {
            const std::int64_t len = std::max(minNoteSamples, elapsedSamples - onv.first);
            midi->notes.push_back({ onv.first / (double) rate,
                                    len / (double) rate, pitch, onv.second });
        }
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

TrackId Engine::trackIdAt(int index) const
{
    auto project = std::atomic_load(&project_);
    if (project == nullptr || index < 0 || index >= (int) project->tracks.size())
        return -1;
    return project->tracks[(size_t) index].id;
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

    const auto text = juce::JSON::toString(
        projectToVar(*project, file.getParentDirectory()), true);
    if (! file.replaceWithText(text))
        return fail("cannot write " + file.getFullPathName());

    projectFile_ = file;
    return juce::Result::ok();
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
    publish(shared);
    transport_->stop();
    projectFile_ = file;
    return juce::Result::ok();
}

void Engine::play() { transport_->play(); }
void Engine::pause()
{
    finalizeRecording();
    transport_->allNotesOff();
    transport_->pause();
}
void Engine::stop()
{
    finalizeRecording();
    transport_->allNotesOff();
    transport_->stop();
}
void Engine::seek(double seconds)
{
    transport_->allNotesOff();
    transport_->seek(seconds);
}

double Engine::durationSeconds() const
{
    if (auto project = std::atomic_load(&project_))
        return project->durationSeconds();
    return 0.0;
}

juce::Result Engine::renderToFile(const juce::File& outFile, double maxSeconds)
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
    renderSource.play();

    // FileOutputStream appends to existing files; renders must replace.
    outFile.deleteFile();

    auto fileStream = outFile.createOutputStream();
    if (fileStream == nullptr)
        return fail("cannot open " + outFile.getFullPathName() + " for writing");

    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(project->sampleRate)
                     .withNumChannels(2)
                     .withBitsPerSample(16);

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

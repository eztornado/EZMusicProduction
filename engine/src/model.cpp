#include "cadence/model.hpp"

#include <cmath>

namespace cadence {

double Project::durationSeconds() const
{
    double end = 0.0;
    for (const auto& track : tracks)
        for (const auto& clip : track.clips)
            end = std::max(end, clip.endSeconds());
    return end;
}

const Track* Project::findTrack(TrackId id) const
{
    for (const auto& track : tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}

Track* Project::findTrack(TrackId id)
{
    for (auto& track : tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}

const Clip* Project::findClip(ClipId id, const Track** ownerTrack) const
{
    for (const auto& track : tracks)
        for (const auto& clip : track.clips)
            if (clip.id == id)
            {
                if (ownerTrack != nullptr)
                    *ownerTrack = &track;
                return &clip;
            }
    return nullptr;
}

Clip* Project::findClip(ClipId id)
{
    for (auto& track : tracks)
        for (auto& clip : track.clips)
            if (clip.id == id)
                return &clip;
    return nullptr;
}

juce::Result loadAudioSourceData(juce::AudioFormatManager& formats,
                                 const juce::File& file,
                                 double projectRate,
                                 AudioSourceData& out)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return juce::Result::fail("unsupported audio format: " + file.getFullPathName());

    const int channels = (int) std::max<juce::int64>(1, reader->numChannels);
    const std::int64_t length = std::max<juce::int64>(0, reader->lengthInSamples);

    juce::AudioBuffer<float> native(channels, (int) length);
    reader->read(&native, 0, (int) length, 0, true, true);

    if (std::abs(reader->sampleRate - projectRate) < 1.0)
    {
        out.samples = std::move(native);
    }
    else
    {
        // One-shot offline resample to the project rate. Linear interpolation
        // is fine for the prototype; high-quality SRC is a later upgrade.
        juce::MemoryAudioSource memory(native, false);
        juce::ResamplingAudioSource resampler(&memory, false, channels);
        resampler.setResamplingRatio(reader->sampleRate / projectRate);

        const int outLength = (int) std::ceil((double) length * projectRate / reader->sampleRate) + 2;
        juce::AudioBuffer<float> resampled(channels, outLength);
        resampled.clear();

        resampler.prepareToPlay(4096, projectRate);
        for (int pos = 0; pos < outLength; pos += 4096)
        {
            juce::AudioSourceChannelInfo info { &resampled, pos, std::min(4096, outLength - pos) };
            resampler.getNextAudioBlock(info);
        }
        out.samples = std::move(resampled);
    }

    out.sampleRate = projectRate;
    out.durationSeconds = (double) out.samples.getNumSamples() / projectRate;
    return juce::Result::ok();
}

// ---------- JSON ----------

namespace {

juce::String pathForStorage(const juce::File& projectDir, const juce::String& fullPath)
{
    const juce::File file(fullPath);
    if (file.isAChildOf(projectDir))
        return file.getRelativePathFrom(projectDir);
    return fullPath;
}

} // namespace

// Var serialization of an fx chain (shared by track fx and master fx).
juce::Array<juce::var> fxToVar(const std::vector<FxSlot>& fx)
{
    juce::Array<juce::var> arr;
    for (const auto& slot : fx)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("uid", slot.pluginUid);
        o->setProperty("name", slot.name);
        o->setProperty("bypass", slot.bypass);
        o->setProperty("state", slot.state);
        arr.add(juce::var(o));
    }
    return arr;
}

juce::var projectToVar(const Project& project, const juce::File& projectDir)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "cadence-project");
    root->setProperty("formatVersion", 1);
    root->setProperty("name", project.name);
    root->setProperty("sampleRate", project.sampleRate);
    root->setProperty("tempoBpm", project.tempoBpm);
    root->setProperty("timeSigNum", project.timeSigNumerator);
    root->setProperty("timeSigDen", project.timeSigDenominator);
    root->setProperty("masterGain", (double) project.masterGain);
    if (project.inputQuantizeSeconds > 0.0)
        root->setProperty("inputQuantize", project.inputQuantizeSeconds);
    root->setProperty("nextTrackId", (juce::int64) project.nextTrackId);
    root->setProperty("nextClipId", (juce::int64) project.nextClipId);
    root->setProperty("nextSourceId", (juce::int64) project.nextSourceId);

    juce::Array<juce::var> sourcesVar;
    for (const auto& [id, path] : project.sourceFiles)
    {
        auto* source = new juce::DynamicObject();
        source->setProperty("id", (juce::int64) id);
        source->setProperty("path", pathForStorage(projectDir, path));
        sourcesVar.add(juce::var(source));
    }
    root->setProperty("sources", sourcesVar);

    juce::Array<juce::var> tracksVar;
    for (const auto& track : project.tracks)
    {
        auto* trackVar = new juce::DynamicObject();
        trackVar->setProperty("id", (juce::int64) track.id);
        trackVar->setProperty("type",
            track.type == Track::Type::Audio ? "audio" : "instrument");
        trackVar->setProperty("name", track.name);
        trackVar->setProperty("colorIndex", track.colorIndex);
        trackVar->setProperty("volume", (double) track.volume);
        trackVar->setProperty("pan", (double) track.pan);
        trackVar->setProperty("mute", track.mute);
        trackVar->setProperty("solo", track.solo);
        trackVar->setProperty("inputChannel", track.inputChannel);
        trackVar->setProperty("fx", fxToVar(track.fx));
        auto laneToVar = [](const AutomationCurve& curve) {
            juce::Array<juce::var> arr;
            for (const auto& point : curve.points)
            {
                auto* pointVar = new juce::DynamicObject();
                pointVar->setProperty("t", point.timeSeconds);
                pointVar->setProperty("v", point.value);
                arr.add(juce::var(pointVar));
            }
            return arr;
        };
        if (track.volumeAutomation != nullptr
            && ! track.volumeAutomation->points.empty())
            trackVar->setProperty("volAuto",
                                  laneToVar(*track.volumeAutomation));
        if (track.panAutomation != nullptr
            && ! track.panAutomation->points.empty())
            trackVar->setProperty("panAuto",
                                  laneToVar(*track.panAutomation));
        if (track.sustainAutomation != nullptr
            && ! track.sustainAutomation->points.empty())
            trackVar->setProperty("sustainAuto",
                                  laneToVar(*track.sustainAutomation));
        if (! track.paramLanes.empty())
        {
            juce::Array<juce::var> paramsVar;
            for (const auto& lane : track.paramLanes)
            {
                auto* laneVar = new juce::DynamicObject();
                laneVar->setProperty("slotKind", lane.slotKind);
                laneVar->setProperty("slotIndex", lane.slotIndex);
                laneVar->setProperty("paramIndex", lane.paramIndex);
                laneVar->setProperty("name", lane.name);
                laneVar->setProperty("points", laneToVar(*lane.curve));
                paramsVar.add(juce::var(laneVar));
            }
            trackVar->setProperty("paramAuto", paramsVar);
        }
        if (track.instrument.pluginUid.isNotEmpty())
        {
            auto* inst = new juce::DynamicObject();
            inst->setProperty("uid", track.instrument.pluginUid);
            inst->setProperty("name", track.instrument.name);
            inst->setProperty("bypass", track.instrument.bypass);
            inst->setProperty("state", track.instrument.state);
            trackVar->setProperty("instrument", juce::var(inst));
        }
        if (track.midiFx.pluginUid.isNotEmpty())
        {
            auto* mfx = new juce::DynamicObject();
            mfx->setProperty("uid", track.midiFx.pluginUid);
            mfx->setProperty("name", track.midiFx.name);
            mfx->setProperty("bypass", track.midiFx.bypass);
            mfx->setProperty("state", track.midiFx.state);
            trackVar->setProperty("midiFx", juce::var(mfx));
        }

        juce::Array<juce::var> clipsVar;
        for (const auto& clip : track.clips)
        {
            auto* clipVar = new juce::DynamicObject();
            clipVar->setProperty("id", (juce::int64) clip.id);
            clipVar->setProperty("start", clip.startSeconds);
            clipVar->setProperty("length", clip.lengthSeconds);
            clipVar->setProperty("muted", clip.muted);
            if (clip.isMidi())
            {
                clipVar->setProperty("kind", "midi");
                juce::Array<juce::var> notesVar;
                for (const auto& note : clip.midi->notes)
                {
                    auto* noteVar = new juce::DynamicObject();
                    noteVar->setProperty("pitch", note.pitch);
                    noteVar->setProperty("velocity", (double) note.velocity);
                    noteVar->setProperty("start", note.startSeconds);
                    noteVar->setProperty("length", note.lengthSeconds);
                    if (std::abs(note.origStartSeconds - note.startSeconds) > 1e-9)
                        noteVar->setProperty("os", note.origStartSeconds);
                    notesVar.add(juce::var(noteVar));
                }
                clipVar->setProperty("notes", notesVar);
            }
            else
            {
                clipVar->setProperty("sourceId", (juce::int64) clip.sourceId);
                clipVar->setProperty("offset", clip.sourceOffsetSeconds);
                clipVar->setProperty("gain", (double) clip.gain);
                clipVar->setProperty("fadeIn", (double) clip.fadeInSeconds);
                clipVar->setProperty("fadeOut", (double) clip.fadeOutSeconds);
            }
            clipsVar.add(juce::var(clipVar));
        }
        trackVar->setProperty("clips", clipsVar);
        tracksVar.add(juce::var(trackVar));
    }
    root->setProperty("masterFx", fxToVar(project.masterFx));
    root->setProperty("tracks", tracksVar);

    return juce::var(root);
}

namespace {

// DynamicObject::getProperty takes no fallback in JUCE 8.
juce::var prop(const juce::DynamicObject* object, const char* name, juce::var fallback)
{
    const auto value = object->getProperty(juce::Identifier(name));
    return value.isVoid() ? std::move(fallback) : value;
}

std::vector<FxSlot> fxFromArray(const juce::var& arrVar)
{
    std::vector<FxSlot> out;
    if (auto* arr = arrVar.getArray())
        for (const auto& item : *arr)
            if (auto* o = item.getDynamicObject())
            {
                FxSlot slot;
                slot.pluginUid = prop(o, "uid", juce::var()).toString();
                slot.name = prop(o, "name", juce::var()).toString();
                slot.bypass = (bool) prop(o, "bypass", false);
                slot.state = prop(o, "state", juce::var()).toString();
                out.push_back(slot);
            }
    return out;
}

} // namespace

juce::Result projectFromVar(const juce::var& value, Project& out)
{
    auto* root = value.getDynamicObject();
    if (root == nullptr)
        return juce::Result::fail("not a cadence project document");

    out.name = prop(root, "name", "Untitled").toString();
    out.sampleRate = (double) prop(root, "sampleRate", 48000.0);
    out.tempoBpm = (double) prop(root, "tempoBpm", 120.0);
    out.timeSigNumerator = (int) prop(root, "timeSigNum", 4);
    out.timeSigDenominator = (int) prop(root, "timeSigDen", 4);
    out.masterGain = (float) (double) prop(root, "masterGain", 1.0);
    out.inputQuantizeSeconds = (double) prop(root, "inputQuantize", 0.0);
    out.nextTrackId = (juce::int64) prop(root, "nextTrackId", (juce::int64) 1);
    out.nextClipId = (juce::int64) prop(root, "nextClipId", (juce::int64) 1);
    out.nextSourceId = (juce::int64) prop(root, "nextSourceId", (juce::int64) 1);

    if (auto* sourcesVar = prop(root, "sources", juce::var()).getArray())
    {
        for (const auto& item : *sourcesVar)
            if (auto* source = item.getDynamicObject())
                out.sourceFiles[(juce::int64) prop(source, "id", (juce::int64) -1)] =
                    prop(source, "path", juce::var()).toString();
    }

    if (auto* tracksVar = prop(root, "tracks", juce::var()).getArray())
    {
        for (const auto& item : *tracksVar)
        {
            auto* trackVar = item.getDynamicObject();
            if (trackVar == nullptr)
                continue;

            Track track;
            track.id = (juce::int64) prop(trackVar, "id", (juce::int64) -1);
            track.type = prop(trackVar, "type", "audio").toString() == "instrument"
                             ? Track::Type::Instrument : Track::Type::Audio;
            track.name = prop(trackVar, "name", juce::var()).toString();
            track.colorIndex = (int) prop(trackVar, "colorIndex", 0);
            track.volume = (float) (double) prop(trackVar, "volume", 1.0);
            track.pan = (float) (double) prop(trackVar, "pan", 0.0);
            track.mute = (bool) prop(trackVar, "mute", false);
            track.solo = (bool) prop(trackVar, "solo", false);
            track.inputChannel = (int) prop(trackVar, "inputChannel", 0);
            track.fx = fxFromArray(trackVar->getProperty("fx"));

            auto laneFromArray = [](const juce::var& v) {
                auto curve = std::make_shared<AutomationCurve>();
                if (auto* arr = v.getArray())
                {
                    for (const auto& item : *arr)
                    {
                        auto* pointVar = item.getDynamicObject();
                        if (pointVar == nullptr)
                            continue;
                        AutomationPoint point;
                        point.timeSeconds = (double) prop(pointVar, "t", 0.0);
                        point.value = juce::jlimit(
                            0.0, 1.0, (double) prop(pointVar, "v", 1.0));
                        curve->points.push_back(point);
                    }
                    std::sort(curve->points.begin(), curve->points.end(),
                              [](const AutomationPoint& a,
                                 const AutomationPoint& b) {
                                  return a.timeSeconds < b.timeSeconds;
                              });
                }
                return curve;
            };
            track.volumeAutomation = laneFromArray(trackVar->getProperty("volAuto"));
            track.panAutomation = laneFromArray(trackVar->getProperty("panAuto"));
            track.sustainAutomation =
                laneFromArray(trackVar->getProperty("sustainAuto"));
            if (auto* paramsVar = trackVar->getProperty("paramAuto").getArray())
            {
                for (const auto& item : *paramsVar)
                {
                    auto* laneVar = item.getDynamicObject();
                    if (laneVar == nullptr)
                        continue;
                    ParamAutomationLane lane;
                    lane.slotKind = (int) prop(laneVar, "slotKind", 0);
                    lane.slotIndex = (int) prop(laneVar, "slotIndex", 0);
                    lane.paramIndex = (int) prop(laneVar, "paramIndex", 0);
                    lane.name = prop(laneVar, "name", juce::var()).toString();
                    lane.curve = laneFromArray(laneVar->getProperty("points"));
                    if (lane.curve != nullptr)
                        track.paramLanes.push_back(std::move(lane));
                }
            }

            if (auto* volAutoVar = trackVar->getProperty("volAuto").getArray())
            {
                auto curve = std::make_shared<AutomationCurve>();
                for (const auto& item : *volAutoVar)
                {
                    auto* pointVar = item.getDynamicObject();
                    if (pointVar == nullptr)
                        continue;
                    AutomationPoint point;
                    point.timeSeconds =
                        (double) prop(pointVar, "t", 0.0);
                    point.value =
                        juce::jlimit(0.0, 1.0,
                                     (double) prop(pointVar, "v", 1.0));
                    curve->points.push_back(point);
                }
                std::sort(curve->points.begin(), curve->points.end(),
                          [](const AutomationPoint& a, const AutomationPoint& b) {
                              return a.timeSeconds < b.timeSeconds;
                          });
                track.volumeAutomation = std::move(curve);
            }

            auto instrumentVar = trackVar->getProperty("instrument");
            if (auto* inst = instrumentVar.getDynamicObject())
            {
                track.instrument.pluginUid = prop(inst, "uid", juce::var()).toString();
                track.instrument.name = prop(inst, "name", juce::var()).toString();
                track.instrument.bypass = (bool) prop(inst, "bypass", false);
                track.instrument.state = prop(inst, "state", juce::var()).toString();
            }

            auto midiFxVar = trackVar->getProperty("midiFx");
            if (auto* mfx = midiFxVar.getDynamicObject())
            {
                track.midiFx.pluginUid = prop(mfx, "uid", juce::var()).toString();
                track.midiFx.name = prop(mfx, "name", juce::var()).toString();
                track.midiFx.bypass = (bool) prop(mfx, "bypass", false);
                track.midiFx.state = prop(mfx, "state", juce::var()).toString();
            }

            if (auto* clipsVar = prop(trackVar, "clips", juce::var()).getArray())
            {
                for (const auto& clipItem : *clipsVar)
                {
                    auto* clipVar = clipItem.getDynamicObject();
                    if (clipVar == nullptr)
                        continue;

                    Clip clip;
                    clip.id = (juce::int64) prop(clipVar, "id", (juce::int64) -1);
                    clip.startSeconds = (double) prop(clipVar, "start", 0.0);
                    clip.lengthSeconds = (double) prop(clipVar, "length", 0.0);
                    clip.muted = (bool) prop(clipVar, "muted", false);

                    // "notes" present => MIDI clip; otherwise an audio clip.
                    if (auto* notesVar = prop(clipVar, "notes", juce::var()).getArray())
                    {
                        auto midi = std::make_shared<MidiClipData>();
                        for (const auto& noteItem : *notesVar)
                        {
                            auto* noteVar = noteItem.getDynamicObject();
                            if (noteVar == nullptr)
                                continue;
                            MidiNote note;
                            note.pitch = (int) prop(noteVar, "pitch", 60);
                            note.velocity = (float) (double) prop(noteVar, "velocity", 0.8);
                            note.startSeconds = (double) prop(noteVar, "start", 0.0);
                            note.lengthSeconds = (double) prop(noteVar, "length", 0.0);
                            note.origStartSeconds =
                                (double) prop(noteVar, "os", note.startSeconds);
                            midi->notes.push_back(note);
                        }
                        clip.midi = std::move(midi);
                    }
                    else
                    {
                        clip.sourceId = (juce::int64) prop(clipVar, "sourceId", (juce::int64) -1);
                        clip.sourceOffsetSeconds = (double) prop(clipVar, "offset", 0.0);
                        clip.gain = (float) (double) prop(clipVar, "gain", 1.0);
                        clip.fadeInSeconds = (float) (double) prop(clipVar, "fadeIn", 0.0);
                        clip.fadeOutSeconds = (float) (double) prop(clipVar, "fadeOut", 0.0);
                    }
                    track.clips.push_back(clip);
                }
            }
            out.tracks.push_back(std::move(track));
        }
    }

    return juce::Result::ok();
}

} // namespace cadence

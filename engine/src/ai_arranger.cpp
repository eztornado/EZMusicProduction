#include "ai_arranger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cadence::ai
{
namespace
{
constexpr int kMaxTracks = 16;
constexpr int kMaxClipsPerTrack = 16;
constexpr int kMaxNotesPerClip = 4000;
constexpr int kMaxNotes = 12000;
constexpr double kMaxBeat = 4096.0;

juce::var property(const juce::var& object, const juce::String& key)
{
    if (auto* dynamic = object.getDynamicObject())
        return dynamic->getProperty(key);
    return {};
}

juce::var firstProperty(const juce::var& object,
                        std::initializer_list<const char*> keys)
{
    for (const auto* key : keys)
    {
        auto value = property(object, key);
        if (! value.isVoid())
            return value;
    }
    return {};
}

bool numberValue(const juce::var& value, double& out)
{
    if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
    {
        out = (double) value;
        return std::isfinite(out);
    }
    if (value.isString())
    {
        const auto text = value.toString().trim();
        if (text.isEmpty())
            return false;
        out = text.getDoubleValue();
        return std::isfinite(out);
    }
    return false;
}

bool integerValue(const juce::var& value, int& out)
{
    double number = 0.0;
    if (! numberValue(value, number))
        return false;
    out = (int) std::llround(number);
    return true;
}

juce::String textValue(const juce::var& value, const juce::String& fallback = {})
{
    if (value.isVoid() || value.isArray() || value.isObject())
        return fallback;
    const auto text = value.toString().trim();
    return text.isEmpty() ? fallback : text;
}

int noteNameToPitch(juce::String text)
{
    text = text.trim();
    if (text.isEmpty())
        return -1;

    const auto lower = text.toLowerCase();
    if (lower.startsWith("midi:"))
        return lower.fromFirstOccurrenceOf(":", false, false).getIntValue();

    const int octavePos =
        juce::CharacterFunctions::isDigit(text.getLastCharacter())
            ? text.length() - 1
            : -1;
    int octave = 4;
    if (octavePos >= 0)
    {
        octave = text.getTrailingIntValue();
        text = text.substring(0, octavePos).trim();
    }

    if (text.isEmpty())
        return -1;
    const auto letter = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase(
        text[0]);
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };
    if (letter < 'a' || letter > 'g')
        return -1;
    int pitch = (octave + 1) * 12 + base[(int) (letter - 'a')];
    for (int i = 1; i < text.length(); ++i)
    {
        if (text[i] == '#')
            ++pitch;
        else if (text[i] == 'b')
            --pitch;
        else if (! juce::CharacterFunctions::isWhitespace(text[i]))
            return -1;
    }
    return pitch;
}

bool pitchValue(const juce::var& value, int& out, juce::String& error)
{
    if (value.isInt() || value.isInt64() || value.isDouble())
    {
        out = (int) std::llround((double) value);
    }
    else if (value.isString())
    {
        const auto text = value.toString().trim();
        if (text.isEmpty())
        {
            error = "pitch is empty";
            return false;
        }
        if (text.containsOnly("0123456789+-"))
            out = text.getIntValue();
        else
            out = noteNameToPitch(text);
    }
    else
    {
        error = "pitch must be a MIDI number or a note name";
        return false;
    }

    if (out < 0 || out > 127)
    {
        error = "pitch " + juce::String(out) + " is outside 0..127";
        return false;
    }
    return true;
}

juce::String clipPath(int trackIndex, int clipIndex)
{
    return "tracks[" + juce::String(trackIndex) + "].clips["
         + juce::String(clipIndex) + "]";
}

juce::String notePath(int trackIndex, int clipIndex, int noteIndex)
{
    return clipPath(trackIndex, clipIndex) + ".notes[" + juce::String(noteIndex)
         + "]";
}

} // namespace

bool parseArrangementPlan(const juce::String& json, ArrangementPlan& out,
                          juce::String& error)
{
    out = {};
    error.clear();

    if (json.trim().isEmpty())
    {
        error = "the plan is empty";
        return false;
    }

    juce::var root = juce::JSON::parse(json);
    if (! root.isObject())
    {
        error = "the plan is not a JSON object";
        return false;
    }

    // Accept the useful nesting variants models produce: the plan at the top,
    // or wrapped once under "plan"/"arrangement".
    for (const auto* wrapper : { "plan", "arrangement" })
    {
        auto nested = property(root, wrapper);
        if (nested.isObject() && ! property(nested, "tracks").isVoid())
        {
            root = nested;
            break;
        }
    }

    out.title = textValue(firstProperty(root, { "title", "name" }));
    out.summary = textValue(firstProperty(root, { "summary", "description",
                                                  "notes" }));

    const auto tracksValue = property(root, "tracks");
    const auto* tracks = tracksValue.getArray();
    if (tracks == nullptr || tracks->isEmpty())
    {
        error = "the plan has no tracks";
        return false;
    }
    if (tracks->size() > kMaxTracks)
    {
        error = "the plan has " + juce::String(tracks->size())
              + " tracks; the maximum is " + juce::String(kMaxTracks);
        return false;
    }

    int totalNotes = 0;

    for (int t = 0; t < tracks->size(); ++t)
    {
        const auto& trackValue = (*tracks)[t];
        if (! trackValue.isObject())
        {
            error = "tracks[" + juce::String(t) + "] is not an object";
            return false;
        }

        PlanTrack track;
        auto idValue = firstProperty(trackValue, { "track_id", "trackId", "id" });
        if (! idValue.isVoid())
        {
            int id = -1;
            if (! integerValue(idValue, id))
            {
                error = "tracks[" + juce::String(t)
                      + "].track_id is not a number";
                return false;
            }
            track.trackId = id > 0 ? id : -1;
        }

        track.name = textValue(firstProperty(trackValue, { "name" }),
                               "Track " + juce::String(t + 1));
        track.role = textValue(firstProperty(trackValue, { "role" }))
                         .toLowerCase();
        track.instrument =
            textValue(firstProperty(trackValue, { "instrument", "sound" }))
                .toLowerCase();
        track.instrumentName = textValue(
            firstProperty(trackValue, { "instrument_name", "instrumentName" }));

        int color = -1;
        if (integerValue(firstProperty(trackValue, { "color", "color_index" }),
                         color))
            track.color = juce::jlimit(0, 15, color);

        double volume = 0.0;
        if (numberValue(firstProperty(trackValue,
                                      { "volume_db", "volumeDb", "gain_db" }),
                        volume))
        {
            track.hasVolume = true;
            track.volumeDb = juce::jlimit(-60.0, 12.0, volume);
        }

        double pan = 0.0;
        if (numberValue(firstProperty(trackValue, { "pan" }), pan))
        {
            track.hasPan = true;
            track.pan = juce::jlimit(-1.0, 1.0, pan);
        }

        const auto clipsValue = property(trackValue, "clips");
        const auto* clips = clipsValue.getArray();
        if (clips != nullptr && clips->size() > kMaxClipsPerTrack)
        {
            error = "tracks[" + juce::String(t) + "] has "
                  + juce::String(clips->size()) + " clips; the maximum is "
                  + juce::String(kMaxClipsPerTrack);
            return false;
        }

        if (clips != nullptr)
        {
            for (int c = 0; c < clips->size(); ++c)
            {
                const auto& clipValue = (*clips)[c];
                if (! clipValue.isObject())
                {
                    error = clipPath(t, c) + " is not an object";
                    return false;
                }

                PlanClip clip;
                double start = 0.0;
                if (numberValue(firstProperty(clipValue,
                                              { "start_beat", "start_beats",
                                                "startBeat", "start" }),
                                start))
                    clip.startBeat = juce::jlimit(0.0, kMaxBeat, start);

                double length = 0.0;
                const bool hasLength = numberValue(
                    firstProperty(clipValue,
                                  { "length_beats", "lengthBeats", "length" }),
                    length);
                if (hasLength)
                    clip.lengthBeats = juce::jlimit(0.01, kMaxBeat, length);

                const auto notesValue = property(clipValue, "notes");
                const auto* notes = notesValue.getArray();
                if (notes == nullptr)
                {
                    error = clipPath(t, c)
                          + " has no notes array (use [] for an empty clip)";
                    return false;
                }
                if (notes->size() > kMaxNotesPerClip)
                {
                    error = clipPath(t, c) + " has " + juce::String(notes->size())
                          + " notes; the maximum per clip is "
                          + juce::String(kMaxNotesPerClip);
                    return false;
                }

                clip.notes.reserve((size_t) notes->size());
                for (int n = 0; n < notes->size(); ++n)
                {
                    const auto& noteValue = (*notes)[n];
                    if (! noteValue.isObject())
                    {
                        error = notePath(t, c, n) + " is not an object";
                        return false;
                    }

                    PlanNote note;
                    juce::String noteError;
                    if (! pitchValue(firstProperty(noteValue,
                                                   { "pitch", "note", "midi" }),
                                     note.pitch, noteError))
                    {
                        error = notePath(t, c, n) + ": " + noteError;
                        return false;
                    }

                    double noteStart = 0.0;
                    if (numberValue(firstProperty(noteValue,
                                                  { "start_beat", "start_beats",
                                                    "startBeat", "start" }),
                                    noteStart))
                        note.startBeat = juce::jlimit(0.0, kMaxBeat, noteStart);

                    double noteLength = 0.25;
                    if (numberValue(firstProperty(noteValue,
                                                  { "length_beats",
                                                    "lengthBeats", "length" }),
                                    noteLength))
                        note.lengthBeats =
                            juce::jlimit(0.01, 64.0, noteLength);

                    double velocity = 0.8;
                    if (numberValue(firstProperty(noteValue,
                                                  { "velocity", "vel" }),
                                    velocity))
                    {
                        // Accept both 0..1 and the MIDI-style 0..127 that
                        // models sometimes write.
                        velocity = velocity > 1.0 ? velocity / 127.0 : velocity;
                        note.velocity =
                            (float) juce::jlimit(0.01, 1.0, velocity);
                    }

                    clip.lengthBeats = juce::jmax(
                        clip.lengthBeats, note.startBeat + note.lengthBeats);
                    clip.notes.push_back(note);
                }

                if (clip.lengthBeats <= 0.0 || clip.lengthBeats > kMaxBeat)
                {
                    error = clipPath(t, c) + " has an invalid length";
                    return false;
                }

                totalNotes += (int) clip.notes.size();
                if (totalNotes > kMaxNotes)
                {
                    error = "the plan has more than " + juce::String(kMaxNotes)
                          + " notes";
                    return false;
                }

                track.clips.push_back(std::move(clip));
            }
        }

        out.tracks.push_back(std::move(track));
    }

    out.totalNotes = totalNotes;
    return true;
}

} // namespace cadence::ai

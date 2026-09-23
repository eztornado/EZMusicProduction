#include <cadence/cadence_engine.h>
#include <cadence/engine.hpp>
#include <cadence/model.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

void printUsage()
{
    std::fprintf(stderr,
        "cadence-cli - Cadence engine test harness\n"
        "\n"
        "usage:\n"
        "  cadence-cli devices\n"
        "  cadence-cli tone <out.wav> [seconds=3] [freq=440]\n"
        "  cadence-cli play <file> [seconds=9999]\n"
        "  cadence-cli selftest <file>\n"
        "  cadence-cli proj-demo <out.wav> [--play]\n"
        "      builds a 3-track project from generated tones, verifies mixing\n"
        "      (clip placement, solo, mute) via offline renders, writes <out.wav>,\n"
        "      and with --play also plays the project live.\n"
        "  cadence-cli doc-test <workdir>\n"
        "      verifies undo/redo and project save/load round-trip.\n"
        "  cadence-cli rectest\n"
        "      verifies the record pipeline end-to-end with a synthesized tone.\n");
}

int cmdDevices()
{
    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: could not open audio device: %s\n", engine.lastErrorUtf8());
        return 1;
    }

    std::printf("output device : %s\n", engine.currentOutputDeviceName().toRawUTF8());
    std::printf("sample rate   : %.0f Hz\n", engine.sampleRate());
    std::printf("buffer size   : %d samples\n", engine.currentBufferSizeSamples());
    std::printf("known outputs : ");
    for (const auto& name : engine.outputDeviceNames())
        std::printf("%s; ", name.toRawUTF8());
    std::printf("\n");
    return 0;
}

int writeToneWav(const juce::File& outFile, double seconds, double freq)
{
    if (seconds <= 0.0 || freq <= 0.0)
    {
        std::fprintf(stderr, "error: seconds and freq must be positive\n");
        return 1;
    }

    constexpr double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(sampleRate * seconds);
    const double fadeSamples = 0.02 * sampleRate;

    std::vector<float> left(static_cast<size_t>(numSamples));
    std::vector<float> right(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / sampleRate;
        const double fade = juce::jlimit(0.0, 1.0,
            juce::jmin(i / fadeSamples, (numSamples - i) / fadeSamples));
        left[i]  = static_cast<float>(0.4 * fade
            * std::sin(juce::MathConstants<double>::twoPi * freq * t));
        right[i] = static_cast<float>(0.4 * fade
            * std::sin(juce::MathConstants<double>::twoPi * (freq + 1.0) * t));
    }

    const float* channels[2] = { left.data(), right.data() };

    // FileOutputStream appends to existing files; regenerate from scratch.
    outFile.deleteFile();

    auto fileStream = outFile.createOutputStream();
    if (fileStream == nullptr)
    {
        std::fprintf(stderr, "error: cannot open %s for writing\n",
                     outFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(sampleRate)
                     .withNumChannels(2)
                     .withBitsPerSample(16);

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    auto writer = wavFormat.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        std::fprintf(stderr, "error: cannot create wav writer\n");
        return 1;
    }

    writer->writeFromFloatArrays(channels, 2, numSamples);
    writer.reset();
    if (stream != nullptr)
        stream->flush();

    std::printf("wrote %s (%.1f s, %.0f Hz, stereo 44.1 kHz)\n",
                outFile.getFullPathName().toRawUTF8(), seconds, freq);
    return 0;
}

int cmdTone(const char* path, double seconds, double freq)
{
    return writeToneWav(juce::File(juce::String::fromUTF8(path)), seconds, freq);
}

int cmdPlay(const char* path, double maxSeconds)
{
    CadenceEngine* engine = cadence_engine_create();
    if (cadence_engine_initialise(engine) != kCadenceOk)
    {
        std::fprintf(stderr, "error: could not open audio device: %s\n",
                     cadence_engine_last_error(engine));
        cadence_engine_destroy(engine);
        return 1;
    }

    if (cadence_engine_load_file(engine, path) != kCadenceOk)
    {
        std::fprintf(stderr, "error: %s\n", cadence_engine_last_error(engine));
        cadence_engine_destroy(engine);
        return 1;
    }

    std::printf("loaded %s - %.2f s @ %.0f Hz (%d track)\n", path,
                cadence_engine_duration_seconds(engine),
                cadence_engine_sample_rate(engine),
                cadence_engine_track_count(engine));

    cadence_engine_play(engine);
    const auto startClock = std::chrono::steady_clock::now();

    while (cadence_engine_is_playing(engine))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::printf("\r  %6.2f / %.2f s", cadence_engine_position_seconds(engine),
                    cadence_engine_duration_seconds(engine));
        std::fflush(stdout);

        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startClock).count();
        if (elapsed >= maxSeconds)
        {
            cadence_engine_stop(engine);
            break;
        }
    }

    std::printf("\nstopped\n");
    cadence_engine_destroy(engine);
    return 0;
}

int cmdSelfTest(const char* path)
{
    CadenceEngine* engine = cadence_engine_create();
    if (cadence_engine_initialise(engine) != kCadenceOk
        || cadence_engine_load_file(engine, path) != kCadenceOk)
    {
        std::fprintf(stderr, "error: %s\n", cadence_engine_last_error(engine));
        cadence_engine_destroy(engine);
        return 1;
    }

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-28s %s\n", what, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    };

    cadence_engine_play(engine);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    check("is playing", cadence_engine_is_playing(engine) == 1);

    cadence_engine_pause(engine);
    const double pausedPos = cadence_engine_position_seconds(engine);
    check("pause keeps position", cadence_engine_is_playing(engine) == 0 && pausedPos > 0.2);

    cadence_engine_seek(engine, 1.5);
    check("seek", std::abs(cadence_engine_position_seconds(engine) - 1.5) < 0.01);

    cadence_engine_play(engine);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    check("resume from seek", cadence_engine_is_playing(engine) == 1
        && cadence_engine_position_seconds(engine) > 1.5);

    cadence_engine_stop(engine);
    check("stop rewinds", !cadence_engine_is_playing(engine)
        && cadence_engine_position_seconds(engine) == 0.0);

    cadence_engine_set_gain(engine, 0.0f);
    check("set gain", cadence_engine_last_error(engine)[0] == '\0');

    std::printf(failures == 0 ? "selftest: all ok\n" : "selftest: %d FAILURES\n", failures);
    cadence_engine_destroy(engine);
    return failures == 0 ? 0 : 1;
}

// Builds the demo project: 3 tracks, one 3 s clip each at 0 / 2 / 4 s,
// tone sources generated on the fly. The 44.1 kHz sources land in a 48 kHz
// project on purpose, to exercise the load-time resample.
juce::Result buildDemoProject(cadence::Engine& engine, const juce::File& dir)
{
    dir.createDirectory();

    struct Part { const char* file; double freq; const char* track; int color; double start; float gain; float pan; };
    const Part parts[] = {
        { "demo_tone_440.wav", 440.0, "Drums", 0, 0.0, 1.0f,  0.0f },
        { "demo_tone_660.wav", 660.0, "Bass",  3, 2.0, 0.8f, -0.6f },
        { "demo_tone_330.wav", 330.0, "Keys",  6, 4.0, 0.9f,  0.6f },
    };

    if (engine.newProject(48000.0).failed())
        return juce::Result::fail(engine.lastErrorUtf8());

    for (const auto& part : parts)
    {
        const juce::File toneFile = dir.getChildFile(part.file);
        if (writeToneWav(toneFile, 3.0, part.freq) != 0)
            return juce::Result::fail("could not generate " + toneFile.getFullPathName());

        const auto trackId = engine.addTrack(part.track, part.color);
        if (trackId < 0)
            return juce::Result::fail(engine.lastErrorUtf8());

        if (engine.setTrackPan(trackId, part.pan).failed())
            return juce::Result::fail(engine.lastErrorUtf8());

        if (engine.addClip(trackId, toneFile, part.start, part.gain) < 0)
            return juce::Result::fail(engine.lastErrorUtf8());
    }

    return juce::Result::ok();
}

// RMS of both channels in [t0, t1) of a wav file; -1 if unreadable.
double rmsInRange(juce::AudioFormatManager& formats, const juce::File& file, double t0, double t1)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return -1.0;

    const int sr = (int) reader->sampleRate;
    const int total = (int) reader->lengthInSamples;
    const int start = juce::jlimit(0, total, (int) (t0 * sr));
    const int len = juce::jlimit(0, total - start, (int) ((t1 - t0) * sr));
    if (len <= 0)
        return 0.0;

    juce::AudioBuffer<float> buffer((int) reader->numChannels, len);
    reader->read(&buffer, 0, len, start, true, true);

    double sum = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < len; ++i)
            sum += (double) data[i] * data[i];
    }
    return std::sqrt(sum / ((double) len * buffer.getNumChannels()));
}

int cmdProjDemo(const char* outPath, bool playLive)
{
    const juce::File outFile(juce::String::fromUTF8(outPath));
    const juce::File dir = outFile.getParentDirectory().isDirectory()
        ? outFile.getParentDirectory()
        : juce::File::getCurrentWorkingDirectory();

    cadence::Engine engine;
    if (buildDemoProject(engine, dir).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    std::printf("project: %d tracks, duration %.3f s @ %.0f Hz\n",
                engine.trackCount(), engine.durationSeconds(), engine.sampleRate());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-42s %s\n", what, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    };

    // Full render: three 3 s clips at 0 / 2 / 4 -> duration ~7 s.
    const juce::File full = dir.getChildFile("proj_demo_verify_full.wav");
    if (engine.renderToFile(full, 9999.0).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    const double expected = 4.0 + 3.0; // clips: 3 s sources at 0 / 2 / 4 s (resampling keeps duration)
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(full));
    const double actual = r != nullptr ? (double) r->lengthInSamples / r->sampleRate : -1.0;
    check("render length ~ 7.00 s", std::abs(actual - expected) < 0.02);
    check("  seg 0.0-0.5 audible (Drums)", rmsInRange(formats, full, 0.0, 0.5) > 0.02);
    check("  seg 3.1-3.6 audible (Bass only)", rmsInRange(formats, full, 3.1, 3.6) > 0.02);
    check("  seg 5.2-5.7 audible (Keys)", rmsInRange(formats, full, 5.2, 5.7) > 0.02);

    // Solo Drums: everything else must go silent. Track ids start at 1 and
    // Drums is the first track added by buildDemoProject.
    const auto drums = 1;
    if (engine.setTrackSolo(drums, true).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    const juce::File soloed = dir.getChildFile("proj_demo_verify_solo.wav");
    if (engine.renderToFile(soloed, 4.0).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    check("solo Drums: seg 0.0-0.5 audible", rmsInRange(formats, soloed, 0.0, 0.5) > 0.02);
    check("solo Drums: seg 3.1-3.6 silent", rmsInRange(formats, soloed, 3.1, 3.6) < 0.000001);
    check("solo Drums: seg 5.2-5.7 silent", rmsInRange(formats, soloed, 5.2, 5.7) < 0.000001);
    engine.setTrackSolo(drums, false);

    // Mute Drums: 0-2 s region goes silent (Bass starts at 2 s).
    if (engine.setTrackMute(drums, true).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    const juce::File muted = dir.getChildFile("proj_demo_verify_mute.wav");
    if (engine.renderToFile(muted, 2.0).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    check("mute Drums: seg 0.0-0.5 silent", rmsInRange(formats, muted, 0.0, 0.5) < 0.000001);
    engine.setTrackMute(drums, false);

    // Final render of the unmuted project.
    if (engine.renderToFile(outFile, 9999.0).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    std::printf("wrote %s (%.2f s)\n", outPath, engine.durationSeconds());

    if (playLive)
    {
        if (engine.initialise().failed())
        {
            std::printf("audio device unavailable (%s); skipping live playback\n",
                        engine.lastErrorUtf8());
        }
        else
        {
            std::printf("playing project live...\n");
            engine.play();
            std::this_thread::sleep_for(std::chrono::milliseconds(
                (int) ((engine.durationSeconds() + 0.3) * 1000.0)));
            engine.stop();
        }
    }

    std::printf(failures == 0 ? "proj-demo: all ok\n" : "proj-demo: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}

int cmdDocTest(const char* dirPath)
{
    const juce::File dir(juce::String::fromUTF8(dirPath));
    dir.createDirectory();

    cadence::Engine engine;
    if (buildDemoProject(engine, dir).failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-42s %s\n", what, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    };
    auto durationNear = [&](double expected) {
        return std::abs(engine.durationSeconds() - expected) < 0.02;
    };

    const double fullDur = 4.0 + 144002.0 / 48000.0;  // 3 clips of 3 s (resampled) at 0/2/4
    const double shortDur = 2.0 + 144002.0 / 48000.0; // after removing/moving the last clip

    check("build: 3 tracks", engine.trackCount() == 3);
    check("build: duration ~ 7.00 s", durationNear(fullDur));
    check("build: undo stack non-empty", engine.canUndo());

    check("save", ! engine.saveProject(dir.getChildFile("doc_test.cadence")).failed());

    check("removeTrack -> 2 tracks", engine.removeTrack(1).wasOk() && engine.trackCount() == 2);
    check("undo restores track", engine.undo() && engine.trackCount() == 3);
    check("redo removes track", engine.redo() && engine.trackCount() == 2);
    check("undo again", engine.undo() && engine.trackCount() == 3);

    check("removeClip(3) -> ~5.00 s", engine.removeClip(3).wasOk() && durationNear(shortDur));
    check("undo restores clip", engine.undo() && durationNear(fullDur));
    check("redo removes clip", engine.redo() && durationNear(shortDur));
    engine.undo();
    check("undo again -> ~7.00 s", durationNear(fullDur));

    engine.setClipGain(1, 0.0f);
    const juce::File gainFile = dir.getChildFile("doc_test_gain.wav");
    engine.renderToFile(gainFile, 2.0);
    check("clip gain 0 renders silent", rmsInRange(formats, gainFile, 0.0, 0.5) < 0.000001);
    engine.undo();
    engine.renderToFile(gainFile, 2.0);
    check("undo restores clip gain", rmsInRange(formats, gainFile, 0.0, 0.5) > 0.02);

    // Fades: a 0.2 s linear fade-in must duck the head of the clip.
    // (Center-pan puts the full-scale body RMS at ~0.2 under the constant-power
    // pan law, and the generated tone already carries a 20 ms fade-in.)
    engine.setClipFades(1, 0.2, 0.0);
    engine.renderToFile(gainFile, 2.0);
    const double fadedHead = rmsInRange(formats, gainFile, 0.0, 0.05);
    const double body = rmsInRange(formats, gainFile, 0.6, 0.9);
    check("fade-in ducks clip head",
          fadedHead < body * 0.25 && fadedHead > 0.001 && body > 0.15);
    engine.undo();
    engine.renderToFile(gainFile, 2.0);
    check("undo restores fades",
          rmsInRange(formats, gainFile, 0.0, 0.05) > body * 0.6);

    check("setClipStart(3, 1.0) -> ~5.00 s",
          engine.setClipStart(3, 1.0).wasOk() && durationNear(shortDur));
    check("undo restores position", engine.undo() && durationNear(fullDur));

    // Trim via setClipBounds: clip3 (id 3) to 5.0..6.0 s, offset 0.5.
    check("clip max length ~3.00 s",
          std::abs(engine.clipLengthMax(3) - 3.0000417) < 0.01);
    check("setClipBounds trims -> ~6.00 s",
          engine.setClipBounds(3, 5.0, 0.5, 1.0).wasOk() && durationNear(6.0));
    check("undo restores bounds", engine.undo() && durationNear(fullDur));

    engine.saveProject(dir.getChildFile("doc_test.cadence"));
    const auto saved = dir.getChildFile("doc_test.cadence").loadFileAsString();
    check("saved JSON uses relative paths",
          saved.contains("demo_tone_440.wav") && ! saved.contains(dir.getFullPathName()));

    check("newProject: empty", ! engine.newProject(48000.0).failed()
        && engine.trackCount() == 0 && ! engine.canUndo());

    check("loadProject: 3 tracks, ~7.00 s", ! engine.loadProject(dir.getChildFile("doc_test.cadence")).failed()
        && engine.trackCount() == 3 && durationNear(fullDur) && ! engine.canUndo());

    const juce::File loaded = dir.getChildFile("doc_test_loaded.wav");
    engine.renderToFile(loaded, 9999.0);
    check("loaded project renders audio", rmsInRange(formats, loaded, 0.0, 0.5) > 0.02);

    std::printf(failures == 0 ? "doc-test: all ok\n" : "doc-test: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}

int cmdRecTest()
{
    cadence::Engine::setDebugLoopback(true);

    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: device: %s\n", engine.lastErrorUtf8());
        cadence::Engine::setDebugLoopback(false);
        return 1;
    }

    engine.newProject(48000);
    const auto track = engine.addTrack("Rec", 0);
    engine.setTrackArmed(track, true);

    if (engine.startRecord().failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        cadence::Engine::setDebugLoopback(false);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    engine.stopRecord();
    cadence::Engine::setDebugLoopback(false);

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-36s %s\n", what, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    };

    check("take finalized as one clip", engine.trackClipCount(track) == 1);
    const auto clipId = engine.clipIdAt(track, 0);
    check("take length ~1.5 s",
          clipId > 0 && std::abs(engine.clipLength(clipId) - 1.5) < 0.25);
    check("undo removes the take", engine.undo() && engine.trackClipCount(track) == 0);
    check("redo restores the take", engine.redo() && engine.trackClipCount(track) == 1);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const juce::File out("/tmp/cadence_rectest.wav");
    engine.renderToFile(out, 999);
    check("take renders with content",
          rmsInRange(formats, out, 0.3, 1.0) > 0.05);

    // ---- MIDI pass: loopback notes into an armed instrument track ----
    engine.setDebugLoopback(true);
    engine.setTrackArmed(1, false); // disarm the audio take from pass 1
    engine.seek(0.0);
    const auto midiTrack = engine.addTrack("Synth", 8, cadence::Track::Type::Instrument);
    engine.setTrackArmed(midiTrack, true);
    if (engine.startRecord().failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        cadence::Engine::setDebugLoopback(false);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    engine.stopRecord();
    cadence::Engine::setDebugLoopback(false);

    check("midi take finalized as one clip", engine.trackClipCount(midiTrack) == 1);
    const auto midiClip = engine.clipIdAt(midiTrack, 0);
    check("midi clip is midi kind", engine.clipIsMidi(midiClip));
    const int noteCount = engine.clipNoteCount(midiClip);
    check("midi clip captured notes (>=4)", noteCount >= 4);
    if (noteCount > 0)
    {
        int pitch = 0; float vel = 0; double start = 0, len = 0;
        engine.clipNoteAt(midiClip, 0, &pitch, &vel, &start, &len);
        check("first note within range", pitch >= 0 && pitch <= 127 && vel > 0);
    }
    check("midi undo removes notes", engine.undo()
        && engine.trackClipCount(midiTrack) == 0);
    check("midi redo restores", engine.redo() && engine.trackClipCount(midiTrack) == 1);

    // Synth renders the MIDI clip: mute the recorded audio take and the demo
    // tones cannot hide the synth output.
    std::printf("  [dbg] synthTrack type=%d armed=%d mute=%d clips=%d\n",
                engine.trackType(midiTrack), engine.trackArmed(midiTrack),
                engine.trackMute(midiTrack), engine.trackClipCount(midiTrack));
    {
        int pitch = 0; float vel = 0; double st = 0, ln = 0;
        for (int n = 0; n < engine.clipNoteCount(midiClip); ++n)
        {
            engine.clipNoteAt(midiClip, n, &pitch, &vel, &st, &ln);
            std::printf("  [dbg] note pitch=%d vel=%.2f start=%.3f len=%.3f\n",
                        pitch, vel, st, ln);
        }
    }
    check("mute audio take", engine.setTrackMute(1, true).wasOk());
    engine.renderToFile(out, 999);
    for (int seg = 0; seg < 6; ++seg)
        std::printf("  [dbg] seg %d-%d s rms=%.5f\n", seg * 25, seg * 25 + 25,
                    rmsInRange(formats, out, seg * 0.25, seg * 0.25 + 0.25));
    check("synth renders midi notes",
          rmsInRange(formats, out, 0.3, 1.0) > 0.01);
    engine.setTrackMute(1, false);

    std::printf(failures == 0 ? "rectest: all ok\n" : "rectest: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "devices")
        return cmdDevices();

    if (cmd == "tone")
    {
        if (argc < 3)
        {
            printUsage();
            return 1;
        }
        const double seconds = argc > 3 ? std::atof(argv[3]) : 3.0;
        const double freq = argc > 4 ? std::atof(argv[4]) : 440.0;
        return cmdTone(argv[2], seconds, freq);
    }

    if (cmd == "play")
    {
        if (argc < 3)
        {
            printUsage();
            return 1;
        }
        const double seconds = argc > 3 ? std::atof(argv[3]) : 9999.0;
        return cmdPlay(argv[2], seconds);
    }

    if (cmd == "selftest")
    {
        if (argc < 3)
        {
            printUsage();
            return 1;
        }
        return cmdSelfTest(argv[2]);
    }

    if (cmd == "proj-demo")
    {
        if (argc < 3)
        {
            printUsage();
            return 1;
        }
        bool playLive = false;
        for (int i = 3; i < argc; ++i)
            if (std::string(argv[i]) == "--play")
                playLive = true;
        return cmdProjDemo(argv[2], playLive);
    }

    if (cmd == "doc-test")
    {
        if (argc < 3)
        {
            printUsage();
            return 1;
        }
        return cmdDocTest(argv[2]);
    }

    if (cmd == "rectest")
        return cmdRecTest();

    if (cmd == "rectest2")
    {
        cadence::Engine engine;
        engine.initialise();
        engine.newProject(48000);
        cadence::Engine::setDebugLoopback(true);
        const auto track = engine.addTrack("Synth", 0, cadence::Track::Type::Instrument);
        engine.setTrackArmed(track, true);
        if (engine.startRecord().failed())
        {
            std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        engine.stopRecord();
        cadence::Engine::setDebugLoopback(false);

        std::printf("clip notes: %d\n", engine.clipNoteCount(engine.clipIdAt(track, 0)));
        const juce::File out("/tmp/cadence_rectest2.wav");
        engine.renderToFile(out, 999);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (int seg = 0; seg < 6; ++seg)
            std::printf("  [dbg] seg %d rms=%.5f\n", seg,
                        rmsInRange(formats, out, seg * 0.25, seg * 0.25 + 0.25));
        return 0;
    }

    if (cmd == "synthtest")
    {
        cadence::Engine engine;
        engine.newProject(48000);
        const auto track = engine.addTrack("Synth", 0, cadence::Track::Type::Instrument);
        const auto clip = engine.addMidiClip(track, 0.0, 1.0);
        engine.midiClipAddNote(clip, 69, 0.8, 0.0, 1.0);
        const juce::File out("/tmp/cadence_synthtest.wav");
        engine.renderToFile(out, 999);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (int seg = 0; seg < 4; ++seg)
            std::printf("  [dbg] seg %d rms=%.5f\n", seg,
                        rmsInRange(formats, out, seg * 0.25, seg * 0.25 + 0.25));
        return 0;
    }

    printUsage();
    return 1;
}

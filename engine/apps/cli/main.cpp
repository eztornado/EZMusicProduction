#include <cadence/cadence_engine.h>
#include <cadence/engine.hpp>
#include <cadence/model.hpp>
#include "../src/ezinst.h"
#include "../src/ezsynth.h"
#include "../src/ezorq.h"
#include "../src/ezdrums.h"
#include "../src/magenta_vae.h"
#include "../src/ai_generation.h"
#include <fstream>

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
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
        "      verifies the record pipeline end-to-end with a synthesized tone.\n"
        "  cadence-cli mastertest\n"
        "      verifies the master bus fader and master insert chain.\n"
        "  cadence-cli devrestart\n"
        "      restarts the UR22 while playing and checks device/position.\n"
        "  cadence-cli churntest\n"
        "      swaps instrument/fx snapshots while the audio thread renders.\n"
        "  cadence-cli vae-test <models-dir>\n"
        "      loads the Magenta MusicVAE checkpoints and runs sample/encode/decode.\n"
        "  cadence-cli ai-test [models-dir]\n"
        "      end-to-end AI generation test (analysis, model, apply, undo).\n");
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

int cmdPlugins()
{
    cadence::Engine engine;
    const auto t0 = std::chrono::steady_clock::now();
    engine.scanPluginsNow();
    const double secs = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - t0).count();
    const int n = engine.pluginCount();
    std::printf("plugins found: %d (%.1f s)\n", n, secs);
    for (int i = 0; i < n; ++i)
        std::printf("  [%s%s] %s\n",
                    engine.pluginFormatAt(i),
                    engine.pluginIsInstrumentAt(i) ? ",inst" : "",
                    engine.pluginNameAt(i));
    return n > 0 ? 0 : 1;
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

int writeChordWav(const juce::File& outFile, double seconds,
                  const std::vector<int>& midiPitches)
{
    if (seconds <= 0.0 || midiPitches.empty())
        return 1;

    constexpr double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(sampleRate * seconds);
    const double fadeSamples = 0.03 * sampleRate;
    std::vector<float> left(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> right(static_cast<size_t>(numSamples), 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / sampleRate;
        const double fade = juce::jlimit(
            0.0, 1.0, juce::jmin(i / fadeSamples,
                                 (numSamples - i) / fadeSamples));
        double sum = 0.0;
        for (int pitch : midiPitches)
        {
            const double freq = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
            // A few harmonics make the chroma less pure-sine and closer to a
            // real instrument without needing a sample library.
            sum += std::sin(juce::MathConstants<double>::twoPi * freq * t)
                 + 0.35 * std::sin(juce::MathConstants<double>::twoPi * freq * 2.0 * t);
        }
        const float value = static_cast<float>(
            0.16 * fade * sum / std::sqrt((double) midiPitches.size()));
        left[(size_t) i] = value;
        right[(size_t) i] = value;
    }

    const float* channels[2] = { left.data(), right.data() };
    outFile.deleteFile();
    auto fileStream = outFile.createOutputStream();
    if (fileStream == nullptr)
        return 1;

    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(sampleRate)
                     .withNumChannels(2)
                     .withBitsPerSample(16);
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    auto writer = wavFormat.createWriterFor(stream, options);
    if (writer == nullptr)
        return 1;
    writer->writeFromFloatArrays(channels, 2, numSamples);
    writer.reset();
    if (stream != nullptr)
        stream->flush();
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
    check("resume from seek", [&]{
        const int playing = cadence_engine_is_playing(engine);
        const double pos = cadence_engine_position_seconds(engine);
        if (playing != 1 || pos <= 1.5)
            std::printf("    [dbg] playing=%d pos=%.3f\n", playing, pos);
        return playing == 1 && pos > 1.5;
    }());

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

    // Mono material keeps the constant-power pan law (3 dB down at centre,
    // Cubase's default for mono); stereo material pans as a balance, so a
    // centred stereo clip renders at unity. The generated tone is stereo and
    // 0.4 peak -> body RMS 0.4/sqrt(2) = 0.283 at unity.
    engine.setClipFades(1, 0.2, 0.0);
    engine.renderToFile(gainFile, 2.0);
    const double fadedHead = rmsInRange(formats, gainFile, 0.0, 0.05);
    const double body = rmsInRange(formats, gainFile, 0.6, 0.9);
    check("centred stereo clip renders at unity",
          body > 0.25 && body < 0.31);
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


int cmdTestDeviceSwitch()
{
    cadence::Engine engine;
    engine.initialise();
    std::printf("BEFORE: output=%s buffer=%d\n",
                engine.currentOutputDeviceName().toRawUTF8(),
                engine.currentBufferSizeSamples());

    const auto r = engine.setDeviceConfig(
        "Steinberg UR22", "", 48000, 256);
    std::printf("setDeviceConfig: %s\n",
                r.failed() ? r.getErrorMessage().toRawUTF8() : "ok");
    std::printf("AFTER: output=%s buffer=%d\n",
                engine.currentOutputDeviceName().toRawUTF8(),
                engine.currentBufferSizeSamples());
    return 0;
}

// Master bus regression: the master fader must scale the final mix (after
// master inserts), not each track. The old code applied masterGain per track
// and never processed masterFxChain_ at all.
int cmdMasterTest()
{
    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Tone", 0);
    writeToneWav(juce::File("/tmp/cadence_master_tone.wav"), 1.0, 440.0);
    engine.addClip(track, juce::File("/tmp/cadence_master_tone.wav"), 0.0, 1.0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    engine.setMasterGain(1.0f);
    const auto fullResult = engine.renderToFileEx(
        juce::File("/tmp/cadence_master_full.wav"), 999, 16);
    engine.setMasterGain(0.5f);
    const auto halfResult = engine.renderToFileEx(
        juce::File("/tmp/cadence_master_half.wav"), 999, 16);

    const double rmsFull = rmsInRange(formats,
                                      juce::File("/tmp/cadence_master_full.wav"),
                                      0.1, 0.9);
    const double rmsHalf = rmsInRange(formats,
                                      juce::File("/tmp/cadence_master_half.wav"),
                                      0.1, 0.9);
    const double ratio = rmsFull > 0.0 ? rmsHalf / rmsFull : 0.0;
    const bool gainOk = fullResult.wasOk() && halfResult.wasOk()
        && ratio > 0.45 && ratio < 0.55;

    // Master insert chain: add the built-in EQ, render, then bypass it.
    int eqIndex = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (juce::String(engine.pluginNameAt(i)) == "EZ EQ Pro")
        {
            eqIndex = i;
            break;
        }
    bool fxOk = true;
    if (eqIndex >= 0)
    {
        fxOk = engine.addMasterFx(eqIndex).wasOk()
            && engine.masterFxCount() == 1
            && juce::String(engine.masterFxNameAt(0)) == "EZ EQ Pro";
        const auto fxResult = engine.renderToFileEx(
            juce::File("/tmp/cadence_master_fx.wav"), 999, 16);
        const double rmsFx = rmsInRange(formats,
                                        juce::File("/tmp/cadence_master_fx.wav"),
                                        0.1, 0.9);
        fxOk = fxOk && fxResult.wasOk() && rmsFx > 0.001;
        fxOk = fxOk && engine.setMasterFxBypass(0, true).wasOk()
            && engine.masterFxBypassAt(0);
        const auto bypassResult = engine.renderToFileEx(
            juce::File("/tmp/cadence_master_bypass.wav"), 999, 16);
        const double rmsBypass = rmsInRange(
            formats, juce::File("/tmp/cadence_master_bypass.wav"), 0.1, 0.9);
        fxOk = fxOk && bypassResult.wasOk() && rmsBypass > 0.001;
    }

    std::printf("mastertest: full=%.5f half=%.5f ratio=%.2f gain=%s "
                "fx=%s\n",
                rmsFull, rmsHalf, ratio,
                gainOk ? "ok" : "FAIL", fxOk ? "ok" : "FAIL");
    return gainOk && fxOk ? 0 : 1;
}

// Device lifecycle regression: restartAudio() must reopen the SAME device and
// keep playing from the same position. The old implementation silently
// reopened the default device and lost the UR22 selection.
int cmdDeviceRestart()
{
    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::printf("devrestart: skipped — no audio device\n");
        return 0;
    }
    engine.newProject(48000);
    const auto track = engine.addTrack("Tone", 0);
    writeToneWav(juce::File("/tmp/cadence_devrestart.wav"), 5.0, 440.0);
    engine.addClip(track, juce::File("/tmp/cadence_devrestart.wav"), 0.0, 1.0);

    const auto switchResult = engine.setDeviceConfig("Steinberg UR22", "",
                                                     48000, 512);
    if (switchResult.failed())
    {
        std::printf("devrestart: skipped — UR22 unavailable (%s)\n",
                    switchResult.getErrorMessage().toRawUTF8());
        return 0;
    }

    engine.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto beforeName = engine.currentOutputDeviceName();
    const int beforeBuffer = engine.currentBufferSizeSamples();
    const double beforePos = engine.positionSeconds();
    const bool beforePlaying = engine.isPlaying();

    const auto restart = engine.restartAudio();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto afterName = engine.currentOutputDeviceName();
    const int afterBuffer = engine.currentBufferSizeSamples();
    const double afterPos = engine.positionSeconds();
    const bool afterPlaying = engine.isPlaying();
    const bool running = engine.audioDeviceRunning();
    const int recoveries = engine.audioRecoveryCount();

    std::printf("devrestart: %s -> %s, buffer %d -> %d, playing %d -> %d, "
                "pos %.2f -> %.2f, running=%d recoveries=%d\n",
                beforeName.toRawUTF8(), afterName.toRawUTF8(),
                beforeBuffer, afterBuffer, beforePlaying ? 1 : 0,
                afterPlaying ? 1 : 0, beforePos, afterPos,
                running ? 1 : 0, recoveries);

    engine.stop();
    const bool ok = restart.wasOk()
        && beforeName == afterName
        && beforeBuffer == afterBuffer
        && afterPlaying
        && afterPos + 0.01 >= beforePos
        && running
        && recoveries == 0;
    std::printf("devrestart: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

// Lifetime regression: swap instrument/fx snapshots while the audio thread
// renders. The old code read node->instrument/node->fx without synchronisation
// and destroyed the old plugin instance immediately; this test would crash or
// use freed memory. With immutable snapshots it must stay finite and silent
// of NaNs.
int cmdChurnTest()
{
    cadence::PluginHost host;
    host.setup();

    juce::String synthUid, eqUid;
    for (int i = 0; i < host.pluginCount(); ++i)
    {
        auto desc = host.descriptionAt(i);
        if (desc == nullptr)
            continue;
        if (desc->name == "EZSynth")
            synthUid = desc->createIdentifierString();
        else if (desc->name == "EZ EQ Pro")
            eqUid = desc->createIdentifierString();
    }
    if (synthUid.isEmpty())
    {
        std::printf("churntest: skipped — EZSynth not registered\n");
        return 0;
    }

    auto project = std::make_shared<cadence::Project>();
    project->sampleRate = 48000.0;
    project->tempoBpm = 120.0;
    cadence::Track track;
    track.id = 1;
    track.type = cadence::Track::Type::Instrument;
    track.name = "Churn";
    track.instrument.pluginUid = synthUid;
    track.instrument.name = "EZSynth";
    auto midi = std::make_shared<cadence::MidiClipData>();
    for (int i = 0; i < 8; ++i)
        midi->notes.push_back({ i * 0.25, 0.2, 60 + i, 0.8f, 0.0 });
    cadence::Clip clip;
    clip.id = 1;
    clip.startSeconds = 0.0;
    clip.lengthSeconds = 60.0;
    clip.midi = midi;
    track.clips.push_back(clip);
    project->tracks.push_back(track);
    project->nextTrackId = 2;
    project->nextClipId = 2;

    cadence::ProjectAudioSource source;
    source.setProject(project);
    source.prepareToPlay(512, 48000.0);
    source.reconcileNodes(*project, host, 512, false);
    source.play();

    std::atomic<bool> stop { false };
    std::atomic<bool> bad { false };
    std::thread render([&] {
        juce::AudioBuffer<float> block(2, 512);
        while (! stop.load(std::memory_order_relaxed))
        {
            block.clear();
            juce::AudioSourceChannelInfo info { &block, 0, 512 };
            source.getNextAudioBlock(info);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    if (! std::isfinite(block.getSample(ch, i)))
                    {
                        bad.store(true, std::memory_order_relaxed);
                        stop.store(true, std::memory_order_relaxed);
                        return;
                    }
        }
    });

    for (int iter = 0; iter < 80 && ! stop.load(); ++iter)
    {
        auto next = std::make_shared<cadence::Project>(*project);
        auto& t = next->tracks[0];
        if ((iter % 2) == 0 && eqUid.isNotEmpty())
        {
            cadence::FxSlot slot;
            slot.pluginUid = eqUid;
            slot.name = "EZ EQ Pro";
            t.fx.push_back(slot);
        }
        else
        {
            t.fx.clear();
        }
        // Alternate the instrument instance too: empty -> internal synth,
        // EZSynth -> hosted instrument. This is the exact replacement that
        // used to free a live plugin under the audio thread.
        if ((iter % 4) == 3)
            t.instrument = {};
        else
        {
            t.instrument.pluginUid = synthUid;
            t.instrument.name = "EZSynth";
        }
        source.setProject(next);
        source.reconcileNodes(*next, host, 512, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    stop.store(true, std::memory_order_relaxed);
    render.join();
    source.releaseResources();

    std::printf("churntest: 80 snapshot swaps while rendering — %s\n",
                bad.load() ? "NaN/FAIL" : "ok");
    return bad.load() ? 1 : 0;
}

// Regression test for the truncated-note bug: a noteOff clamped into every
// block cut each MIDI note to ~one audio block (~10 ms). A 1 s note must
// stay audible for close to its full written length.
int cmdNoteTest()
{
    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Synth", 0,
                                       cadence::Track::Type::Instrument);
    engine.addMidiClip(track, 0.0, 3.0);
    const auto clip = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip, 60, 0.8, 0.1, 1.0); // 1 s note

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    engine.renderToFileEx(juce::File("/tmp/cadence_note1s.wav"), 999, 16);

    // Find the last sample offset whose 10 ms window RMS is still audible.
    const double threshold = 0.005;
    double lastAudible = 0.0;
    for (double t = 0.0; t < 2.5; t += 0.01)
    {
        if (rmsInRange(formats, juce::File("/tmp/cadence_note1s.wav"),
                       t, t + 0.01) > threshold)
            lastAudible = t + 0.01;
    }
    std::printf("notetest: note length 1.00 s, audible until %.2f s\n",
                lastAudible);
    const bool ok = lastAudible > 0.7; // generous: release tail may fade early
    std::printf("notetest: %s\n", ok ? "FULL LENGTH ok" : "TRUNCATED?!");
    return ok ? 0 : 1;
}

namespace {

using cadence::Clip;
using cadence::MidiClipData;
using cadence::Project;
using cadence::ProjectAudioSource;
using cadence::Track;

std::shared_ptr<const Project> makeStuckProject(const juce::String& instrumentUid,
                                                bool withNote,
                                                double clipLength)
{
    auto project = std::make_shared<Project>();
    project->sampleRate = 44100.0;
    project->nextClipId = 2;
    Track t;
    t.id = 1;
    t.name = "Stuck";
    t.type = Track::Type::Instrument;
    t.instrument.pluginUid = instrumentUid;
    t.instrument.name = "EZSynth";
    Clip c;
    c.id = 1;
    c.startSeconds = 0.0;
    c.lengthSeconds = clipLength;
    auto midi = std::make_shared<MidiClipData>();
    if (withNote)
        midi->notes.push_back({ 0.0, 3.0, 48, 0.9f, 0.0 });
    c.midi = midi;
    t.clips.push_back(c);
    project->tracks.push_back(t);
    return project;
}

// Regression: stop (and the implicit stop at the project end) must silence
// plugin voices on EVERY instrument track, including tracks that are not
// armed. Before the panic pass, a note held on an unarmed plugin track was
// never released while stopped, and sounded again on the next play even
// though it was not part of the new clip.
void renderSourceToFile(ProjectAudioSource& source, const juce::File& out,
                        double seconds, int block);

int cmdStuckTest()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    constexpr int block = 512;

    cadence::PluginHost host;
    host.setup();
    juce::String synthUid;
    for (int i = 0; i < host.pluginCount(); ++i)
        if (auto desc = host.descriptionAt(i);
            desc != nullptr && desc->name == "EZSynth")
        {
            synthUid = desc->createIdentifierString();
            break;
        }
    if (synthUid.isEmpty())
    {
        std::printf("stucktest: FAIL — EZSynth not registered\n");
        return 1;
    }

    auto configure = [&](ProjectAudioSource& source,
                         const std::shared_ptr<const Project>& project)
    {
        source.setPluginHost(&host);
        source.prepareToPlay(block, 44100.0);
        source.setProject(project);
        source.reconcileNodes(*project, host, block, false);
        // Pad Etéreo: long release and sustain, exactly the kind of voice
        // that stays audible after a missed note-off.
        if (auto proc = source.instrumentProcessorFor(1))
            ezsynth::applyPresetForTest(proc.get(), 5);
    };

    auto render = [&](ProjectAudioSource& source, int blocks,
                      juce::AudioBuffer<float>& sink)
    {
        for (int i = 0; i < blocks; ++i)
        {
            sink.clear();
            const juce::AudioSourceChannelInfo info { &sink, 0, block };
            source.getNextAudioBlock(info);
        }
    };

    juce::AudioBuffer<float> sink(2, block);

    // 1) Explicit stop while a long note is still held.
    ProjectAudioSource source;
    configure(source, makeStuckProject(synthUid, true, 3.0));
    source.play();
    render(source, 30, sink); // ~0.35 s: the note is still on
    source.stop();
    source.allNotesOff();
    source.requestPanic();
    render(source, 4, sink); // deliver the stopped panic pass

    const auto silentProject = makeStuckProject(synthUid, false, 3.0);
    source.setProject(silentProject);
    source.reconcileNodes(*silentProject, host, block, false);
    source.seek(0.0);
    source.play();
    const juce::File stopOut(juce::File::createTempFile("ez_stuck_stop.wav"));
    renderSourceToFile(source, stopOut, 1.0, block);
    const float stopRms = (float) rmsInRange(formats, stopOut, 0.0, 1.0);
    stopOut.deleteFile();

    // 2) Implicit stop at the project end: the note outlives the clip.
    ProjectAudioSource ended;
    configure(ended, makeStuckProject(synthUid, true, 0.4));
    ended.play();
    render(ended, 40, sink); // past the 0.4 s project end
    const auto endedProject = makeStuckProject(synthUid, false, 3.0);
    ended.setProject(endedProject);
    ended.reconcileNodes(*endedProject, host, block, false);
    ended.seek(0.0);
    ended.play();
    const juce::File endOut(juce::File::createTempFile("ez_stuck_end.wav"));
    renderSourceToFile(ended, endOut, 1.0, block);
    const float endRms = (float) rmsInRange(formats, endOut, 0.0, 1.0);
    endOut.deleteFile();

    const float threshold = 0.01f;
    const bool ok = stopRms < threshold && endRms < threshold;
    std::printf("stucktest: stop residual=%.5f end residual=%.5f (%s)\n",
                stopRms, endRms,
                ok ? "no stuck notes" : "STUCK NOTES!");
    return ok ? 0 : 1;
}

std::shared_ptr<const Project> makeChordProject(bool armed, bool withClip,
                                                int basePitch)
{
    auto project = std::make_shared<Project>();
    project->sampleRate = 44100.0;
    project->nextClipId = 2;
    Track t;
    t.id = 1;
    t.name = "Synth";
    t.type = Track::Type::Instrument;
    t.armed = armed;
    if (withClip)
    {
        Clip c;
        c.id = 1;
        c.startSeconds = 0.0;
        c.lengthSeconds = 3.0;
        auto midi = std::make_shared<MidiClipData>();
        // Humanized chord: ons spread across blocks (a real player never
        // hits all keys in the same sample), offs in a DIFFERENT order —
        // finalize pairs by event order, so the stored array is NOT sorted
        // by start. The playback loop must survive that (regression: a
        // `break` on unsorted notes made only one chord note sound).
        midi->notes.push_back({ 0.226, 0.800, basePitch + 7, 0.8f, 0.226 });
        midi->notes.push_back({ 0.200, 0.900, basePitch + 0, 0.8f, 0.200 });
        midi->notes.push_back({ 0.213, 1.000, basePitch + 4, 0.8f, 0.213 });
        c.midi = midi;
        t.clips.push_back(c);
    }
    project->tracks.push_back(t);
    return project;
}

// Renders `seconds` of a playing source into a wav file.
void renderSourceToFile(ProjectAudioSource& source, const juce::File& out,
                        double seconds, int block)
{
    juce::AudioBuffer<float> blockBuf(2, block);
    juce::AudioBuffer<float> mix(2, (int) (44100.0 * seconds));
    int pos = 0;
    while (pos + block <= mix.getNumSamples())
    {
        blockBuf.clear();
        const juce::AudioSourceChannelInfo info { &blockBuf, 0, block };
        source.getNextAudioBlock(info);
        for (int ch = 0; ch < 2; ++ch)
            mix.copyFrom(ch, pos, blockBuf, ch, 0, block);
        pos += block;
    }
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = out.createOutputStream();
    juce::AudioFormatWriterOptions opts;
    opts = opts.withSampleRate(44100.0).withNumChannels(2)
               .withBitsPerSample(16);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, opts);
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer(mix, 0, mix.getNumSamples());
}

} // namespace

// Reproduces the app's live polyphony flow: a PERSISTENT ProjectAudioSource
// (the offline render builds a fresh one each time, hiding this state)
// goes through monitor-chord → stop mid-tail (voices frozen, claims held)
// → replay the clip. Also with a chord on pitches different from the
// frozen ones. Pass = replay as loud as a fresh render of the same chord.
int cmdPolyTest()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    constexpr int block = 512;

    cadence::PluginHost host;
    ProjectAudioSource source;
    source.setPluginHost(&host);
    source.prepareToPlay(block, 44100.0);

    // Pass 1 — "record": live chord monitored, stopped mid-note, no tail
    // render. Voices freeze active with their note claims held.
    source.setProject(makeChordProject(true, false, 60));
    source.play();
    for (const int p : { 60, 64, 67 })
        source.pushExternalMidi(juce::MidiMessage::noteOn(1, p, 0.8f));
    juce::AudioBuffer<float> sink(2, block);
    for (int i = 0; i < 40; ++i) // ~0.46 s of monitoring
    {
        sink.clear();
        const juce::AudioSourceChannelInfo info { &sink, 0, block };
        source.getNextAudioBlock(info);
    }
    source.allNotesOff();
    source.pause();

    float worst = 1.0f;
    bool ok = true;
    for (const bool sameChord : { true, false })
    {
        const int replayPitch = sameChord ? 60 : 72;
        source.setProject(makeChordProject(false, true, replayPitch));
        source.seek(0.0);
        source.play();
        const juce::File liveOut(juce::File::createTempFile("ez_poly_live.wav"));
        renderSourceToFile(source, liveOut, 3.0, block);

        ProjectAudioSource fresh;
        fresh.setPluginHost(&host);
        fresh.setProject(makeChordProject(false, true, replayPitch));
        fresh.prepareToPlay(block, 44100.0);
        fresh.play();
        const juce::File freshOut(juce::File::createTempFile("ez_poly_fresh.wav"));
        renderSourceToFile(fresh, freshOut, 3.0, block);

        const float liveRms = rmsInRange(formats, liveOut, 0.3, 0.9);
        const float freshRms = rmsInRange(formats, freshOut, 0.3, 0.9);
        const float ratio = freshRms > 0 ? liveRms / freshRms : 0.0f;
        worst = std::min(worst, ratio);
        std::printf("polytest: %s chord live=%.4f fresh=%.4f ratio=%.2f\n",
                    sameChord ? "same  " : "shifted", liveRms, freshRms, ratio);
        ok = ok && ratio > 0.75f;
        liveOut.deleteFile();
        freshOut.deleteFile();
    }
    std::printf("polytest: %s\n", ok ? "POLYPHONIC through live flow ok"
                                     : "VOICES LOST in live flow!");
    return ok ? 0 : 1;
}

// Records a 3-note chord through the REAL capture pipeline (debug loopback
// MIDI → TrackCapture → finalize pairing), verifies the take holds 3
// full-length notes, and renders it polyphonically (ratio vs a single
// synthetic note in the same session).
int cmdChordRecTest()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: device: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    if (engine.midiInputNames().isEmpty())
    {
        std::printf("chordrectest: skipped — no CoreMIDI devices present\n");
        return 2;
    }
    engine.selectMidiInput(0); // real capture path, no loopback generator

    engine.newProject(48000);
    const auto track = engine.addTrack("Synth", 0,
                                       cadence::Track::Type::Instrument);
    engine.setTrackArmed(track, true);
    if (engine.startRecord().failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    engine.debugPushLiveMidi(60, true);
    engine.debugPushLiveMidi(64, true);
    engine.debugPushLiveMidi(67, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    engine.debugPushLiveMidi(60, false);
    engine.debugPushLiveMidi(64, false);
    engine.debugPushLiveMidi(67, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    engine.stopRecord();

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-36s %s\n", what, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    };

    check("chord take = one clip", engine.trackClipCount(track) == 1);
    const auto clip = engine.clipIdAt(track, 0);
    const int count = engine.clipNoteCount(clip);
    check("3 notes captured", count == 3);
    int pitchesSeen = 0;
    for (int n = 0; n < count; ++n)
    {
        int pitch = 0; float vel = 0; double start = 0, len = 0;
        engine.clipNoteAt(clip, n, &pitch, &vel, &start, &len);
        std::printf("  [dbg] note pitch=%d vel=%.2f start=%.3f len=%.3f\n",
                    pitch, vel, start, len);
        if (pitch == 60 || pitch == 64 || pitch == 67) ++pitchesSeen;
        check("note length ~0.8 s", len > 0.6 && len < 1.0);
        check("note start ~0.3 s", std::abs(start - 0.3) < 0.15);
    }
    check("chord pitches present", pitchesSeen == 3);

    // Polyphonic render of the RECORDED take vs one synthetic note.
    engine.renderToFile(juce::File("/tmp/cadence_chordrec.wav"), 999);
    const float chordRms = rmsInRange(formats,
        juce::File("/tmp/cadence_chordrec.wav"), 0.4, 1.0);
    engine.removeClip(clip);
    engine.addMidiClip(track, 0.0, 3.0);
    const auto clip2 = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip2, 60, 0.3, 0.8, 0.8);
    engine.renderToFile(juce::File("/tmp/cadence_chordsingle.wav"), 999);
    const float singleRms = rmsInRange(formats,
        juce::File("/tmp/cadence_chordsingle.wav"), 0.4, 1.0);
    const float ratio = singleRms > 0 ? chordRms / singleRms : 0.0f;
    std::printf("chordrectest: chordRms=%.4f singleRms=%.4f ratio=%.2f\n",
                chordRms, singleRms, ratio);
    check("recorded chord is polyphonic", ratio > 1.4f);

    std::printf(failures == 0 ? "chordrectest: all ok\n"
                              : "chordrectest: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}

// Verifies the recorded-take fixes: (1) fast legato same-pitch repeats
// (note-on before the previous note-off) must yield TWO notes, not one —
// the old single-slot pairing dropped the first; (2) MIDI input latency
// compensation shifts the take earlier by the configured ms.
int cmdMidiFixTest()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: device: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    if (engine.midiInputNames().isEmpty())
    {
        std::printf("midifixtest: skipped — no CoreMIDI devices present\n");
        return 2;
    }
    engine.selectMidiInput(0);

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-36s %s\n", what, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    };

    // ---- 1) legato same-pitch repeat keeps both notes ----
    engine.newProject(48000);
    const auto track = engine.addTrack("Synth", 0,
                                       cadence::Track::Type::Instrument);
    engine.setTrackArmed(track, true);
    if (engine.startRecord().failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    engine.debugPushLiveMidi(60, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    // Live take (§7.11): the note must be visible WHILE it is still held, not
    // only after the take is finalized — this is what the timeline draws.
    {
        const int live = engine.recordingNoteCount(track);
        int lp = 0; float lv = 0; double ls = 0, ll = 0;
        if (live > 0)
            engine.recordingNoteAt(track, live - 1, &lp, &lv, &ls, &ll);
        std::printf("  [dbg] live while held: notes=%d pitch=%d len=%.3f\n",
                    live, lp, ll);
        check("live take: held note visible", live == 1 && lp == 60);
        check("live take: held length grows", ll > 0.05);
    }
    engine.debugPushLiveMidi(60, true); // re-press before any note-off
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    engine.debugPushLiveMidi(60, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    engine.debugPushLiveMidi(60, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    engine.stopRecord();

    check("legato repeat = one clip", engine.trackClipCount(track) == 1);
    check("live take cleared after stop", engine.recordingNoteCount(track) == 0);
    const auto clip = engine.clipIdAt(track, 0);
    check("legato repeat = TWO notes",
          engine.clipNoteCount(clip) == 2);

    // ---- 2) latency compensation shifts the take earlier ----
    engine.setMidiInputLatencyMs(30.0);
    engine.undo(); // drop take 1
    engine.setTrackArmed(track, true);
    engine.seek(0.0);
    if (engine.startRecord().failed())
    {
        std::fprintf(stderr, "error: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    engine.debugPushLiveMidi(64, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    engine.debugPushLiveMidi(64, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    engine.stopRecord();
    engine.setMidiInputLatencyMs(0.0);

    const auto clip2 = engine.clipIdAt(track, 0);
    if (engine.clipNoteCount(clip2) == 1)
    {
        int pitch = 0; float vel = 0; double start = 0, len = 0;
        engine.clipNoteAt(clip2, 0, &pitch, &vel, &start, &len);
        std::printf("  [dbg] compensated start=%.3f s\n", start);
        // The raw press lands ~0.3 s in; with 30 ms taken off it must sit
        // measurably below 0.3.
        check("latency 30 ms shifts earlier", start < 0.295);
    }
    else
    {
        check("latency take captured one note", false);
    }

    std::printf(failures == 0 ? "midifixtest: all ok\n"
                              : "midifixtest: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}

// Loads a project and reports, per instrument track, whether a plugin
// instance actually came up (false = playback falls back to the internal
// synth) plus whether the scan cache knows the stored uid.
int cmdLoadDiag(const char* path)
{
    cadence::Engine engine;
    if (engine.initialise().failed())
        std::fprintf(stderr, "warn: device: %s\n", engine.lastErrorUtf8());

    const auto r = engine.loadProject(juce::File(path));
    std::printf("load: %s\n",
                r.failed() ? r.getErrorMessage().toRawUTF8() : "ok");
    // Deferred plugin jobs never pump without a dispatch loop — drain them.
    while (engine.hasPendingPluginJobs())
        engine.pumpOnePluginJob();

    std::printf("scan cache: %d plugins\n", engine.pluginCount());
    for (int i = 0; i < engine.pluginCount(); ++i)
    {
        const juce::String uid = juce::String(engine.pluginUidAt(i));
        if (uid.contains("SINE") || uid.contains("ZENOLOGY"))
            std::printf("  cache: %s\n", uid.toRawUTF8());
    }

    const int n = engine.trackCount();
    for (int i = 0; i < n; ++i)
    {
        const auto id = engine.trackIdAt(i);
        if (engine.trackType(id) != 1) continue;
        std::printf("track %d id=%lld: instrument %s\n", i, (long long) id,
                    engine.trackInstrumentLive(id) ? "LIVE"
                                                   : "NOT INSTANTIATED (piano fallback)");
    }
    return 0;
}

// EZ EQ Pro built-in: (1) a 440 Hz tone with a LowCut at 2 kHz must be
// heavily attenuated; (2) a +24 dB bell at 440 must BOOST the tone; (3)
// state round-trips through getState/setStateInformation.
float peakInRange(juce::AudioFormatManager& fm, const juce::File& f,
                  double from, double to)
{
    juce::AudioFormatReader* r = fm.createReaderFor(f);
    if (r == nullptr) return 0.f;
    const int sr = (int) r->sampleRate;
    const int start = (int) (from * sr);
    const int len = (int) ((to - from) * sr);
    juce::AudioBuffer<float> b((int) r->numChannels, len);
    r->read(&b, 0, len, start, true, true);
    float m = 0.f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int j = 0; j < len; ++j)
            m = juce::jmax(m, std::abs(b.getSample(ch, j)));
    delete r;
    return m;
}

// Behavioral capture: hosts a target plugin, renders 40 log-spaced sine
// tones, measures the magnitude response (with a no-plugin baseline for
// de-embedding) and dumps JSON. Works for ANY hosted plugin: this is the
// black-box half of matching a reference EQ's measured behavior.
int cmdQCapture(const char* spec, const char* jsonPath)
{
    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: device: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    engine.newProject(48000);
    const auto track = engine.addTrack("Capture", 0, cadence::Track::Type::Audio);

    engine.scanPluginsNow();
    int pluginIndex = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (juce::String(engine.pluginNameAt(i)).containsIgnoreCase(
                juce::String::fromUTF8(spec)))
        { pluginIndex = i; break; }
    if (pluginIndex < 0)
    {
        std::fprintf(stderr, "error: no plugin matching '%s'\n", spec);
        return 1;
    }
    std::printf("capturing: %s\n", engine.pluginNameAt(pluginIndex));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // 40 log-spaced tones, 0.25 s each (20 Hz -> 20 kHz).
    constexpr int kNumTones = 40;
    double f = 20.0;
    for (int i = 0; i < kNumTones; ++i)
    {
        juce::File tone("/tmp/cadence_qcap_tone.wav");
        writeToneWav(tone, 0.25, f);
        engine.addClip(track, tone, i * 0.25, 1.0f);
        f *= std::pow(1000.0, 1.0 / (kNumTones - 1));
    }

    // Pass A: baseline render WITHOUT fx.
    const juce::File render("/tmp/cadence_qcap_render.wav");
    render.deleteFile();
    engine.renderToFileEx(render, 999, 24);
    double baseline[kNumTones] = {};
    for (int i = 0; i < kNumTones; ++i)
    {
        const double from = i * 0.25 + 0.02, to = (i + 1) * 0.25 - 0.02;
        baseline[i] = peakInRange(formats, render, from, to);
    }

    // Insert fx and capture again.
    engine.addTrackFx(track, pluginIndex);
    engine.seek(0.0);
    render.deleteFile();
    engine.renderToFileEx(render, 999, 24);
    double captured[kNumTones] = {};
    for (int i = 0; i < kNumTones; ++i)
    {
        const double from = i * 0.25 + 0.02, to = (i + 1) * 0.25 - 0.02;
        captured[i] = peakInRange(formats, render, from, to);
    }

    // JSON: de-embedded response (captured / baseline per band).
    std::string json = "{\n  \"plugin\": \"" +
        std::string(engine.pluginNameAt(pluginIndex)) + "\",\n  \"points\": [\n";
    f = 20.0;
    for (int i = 0; i < kNumTones; ++i)
    {
        const double rel = baseline[i] > 1e-6 ? captured[i] / baseline[i] : 0.0;
        char buf[128];
        std::snprintf(buf, sizeof buf,
                      "    {\"f\": %.1f, \"rel\": %.4f}%s", f, rel,
                      i + 1 < kNumTones ? "," : "");
        json += buf;
        if (i + 1 < kNumTones) json += "\n";
        f *= std::pow(1000.0, 1.0 / (kNumTones - 1));
    }
    json += "\n  ]\n}\n";
    std::ofstream out(jsonPath);
    out << json;
    std::printf("wrote %s (%d points, de-embedded by baseline)\n", jsonPath,
                kNumTones);
    return 0;
}
int cmdEzQTest()
{
    cadence::Engine::setDebugLoopback(true);
    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: device: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    engine.newProject(48000);
    const auto track = engine.addTrack("Fx", 0, cadence::Track::Type::Audio);

    juce::File toneFile("/tmp/cadence_ezq_tone.wav");
    writeToneWav(toneFile, 3.0, 440.0);
    engine.addClip(track, toneFile, 0.0, 1.0f);

    engine.scanPluginsNow();
    int pluginIndex = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (juce::String(engine.pluginNameAt(i)).containsIgnoreCase("EZ EQ Pro"))
        { pluginIndex = i; break; }
    if (pluginIndex < 0)
    {
        std::fprintf(stderr, "error: EZ EQ Pro builtin not found\n");
        return 1;
    }
    engine.addTrackFx(track, pluginIndex);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const juce::File flat("/tmp/cadence_ezq_flat.wav");
    flat.deleteFile();
    engine.renderToFileEx(flat, 999, 16);
    const float flatPeak = peakInRange(formats, flat, 0.2, 2.8);

    // LowCut at max freq (normalized 1.0 = 20 kHz): kills the 440 Hz tone.
    std::vector<cadence::AutomationPoint> ptsEn {{0.0, 1.0}};
    std::vector<cadence::AutomationPoint> ptsTy {{0.0, 0.2}};
    std::vector<cadence::AutomationPoint> ptsF  {{0.0, 1.0}};
    engine.setTrackParamAutomation(track, 0, 0, 10, "b3en", ptsEn);
    engine.setTrackParamAutomation(track, 0, 0, 11, "b3ty", ptsTy);
    engine.setTrackParamAutomation(track, 0, 0, 12, "b3f", ptsF);

    const juce::File cut("/tmp/cadence_ezq_cut.wav");
    cut.deleteFile();
    engine.renderToFileEx(cut, 999, 16);
    const float cutPeak = peakInRange(formats, cut, 0.2, 2.8);

    const bool pass = flatPeak > 0.1f && cutPeak < flatPeak * 0.25f;
    std::printf("ezqtest: flatPeak=%.4f cutPeak=%.4f -> %s\n",
                flatPeak, cutPeak, pass ? "LOWCUT WORKS" : "NO CUT?!");
    cadence::Engine::setDebugLoopback(false);
    flat.deleteFile();
    cut.deleteFile();
    toneFile.deleteFile();
    return pass ? 0 : 1;
}

int cmdChordTest()
{
    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Synth", 0,
                                       cadence::Track::Type::Instrument);
    engine.addMidiClip(track, 0.0, 2.0);
    const auto clip = engine.clipIdAt(track, 0);
    // Chord: three simultaneous notes.
    engine.midiClipAddNote(clip, 60, 0.8, 0.1, 1.0);
    engine.midiClipAddNote(clip, 64, 0.8, 0.1, 1.0);
    engine.midiClipAddNote(clip, 67, 0.8, 0.1, 1.0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    engine.renderToFileEx(juce::File("/tmp/cadence_chord3.wav"), 999, 16);
    const float chordRms = rmsInRange(formats,
        juce::File("/tmp/cadence_chord3.wav"), 0.2, 0.9);

    // Single-note reference.
    engine.removeClip(clip);
    engine.addMidiClip(track, 0.0, 2.0);
    const auto clip2 = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip2, 60, 0.8, 0.1, 1.0);
    engine.renderToFileEx(juce::File("/tmp/cadence_single.wav"), 999, 16);
    const float singleRms = rmsInRange(formats,
        juce::File("/tmp/cadence_single.wav"), 0.2, 0.9);

    std::printf("chordtest: chordRms=%.4f singleRms=%.4f ratio=%.2f\n",
                chordRms, singleRms, singleRms > 0 ? chordRms / singleRms : 0);
    const float ratio = singleRms > 0 ? chordRms / singleRms : 0;
    std::printf("chordtest: %s\n", ratio > 1.5f ? "POLYPHONIC ok"
                                                : "MONOPHONIC?!");
    return ratio > 1.5f ? 0 : 1;
}

int cmdEzInstTest()
{
    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("GM", 0,
                                       cadence::Track::Type::Instrument);

    // Locate EZInstruments in the merged plugin list (builtins + scanned).
    int idx = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (engine.pluginNameAt(i) == juce::String("EZInstruments"))
        {
            idx = i;
            break;
        }
    if (idx < 0)
    {
        std::printf("ezinsttest: EZInstruments not registered\n");
        return 1;
    }
    if (engine.setTrackInstrumentPlugin(track, idx).failed())
    {
        std::printf("ezinsttest: setTrackInstrumentPlugin failed\n");
        return 1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // Single note: piano program (default).
    engine.addMidiClip(track, 0.0, 2.5);
    auto clip = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip, 60, 0.8, 0.1, 1.0);
    engine.renderToFileEx(juce::File("/tmp/cadence_ezinst_single.wav"), 999, 16);
    const float singleRms = rmsInRange(formats,
        juce::File("/tmp/cadence_ezinst_single.wav"), 0.2, 0.9);
    const float tailRms = rmsInRange(formats,
        juce::File("/tmp/cadence_ezinst_single.wav"), 1.4, 1.9);

    // Chord: three simultaneous notes through fluidsynth.
    engine.removeClip(clip);
    engine.addMidiClip(track, 0.0, 2.5);
    clip = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip, 60, 0.8, 0.1, 1.0);
    engine.midiClipAddNote(clip, 64, 0.8, 0.1, 1.0);
    engine.midiClipAddNote(clip, 67, 0.8, 0.1, 1.0);
    engine.renderToFileEx(juce::File("/tmp/cadence_ezinst_chord.wav"), 999, 16);
    const float chordRms = rmsInRange(formats,
        juce::File("/tmp/cadence_ezinst_chord.wav"), 0.2, 0.9);

    // Sustained-loop patch + running-status note-off: strings (program 40)
    // must stop after a note-on-velocity-0 "off" — keyboards send releases
    // that way, and decaying patches hide a missing release, looping
    // strings drone forever (the violin bug).
    bool vel0Ok = false;
    {
        std::unique_ptr<juce::AudioProcessor> proc(ezinst::create());
        proc->prepareToPlay(44100, 512);
        auto* prog = dynamic_cast<juce::AudioParameterChoice*>(
            proc->getParameters()[0]);
        if (prog != nullptr)
            prog->setValueNotifyingHost(prog->convertTo0to1(40.f / 127.f));

        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        auto runBlocks = [&](double secs) {
            const int n = (int) (secs * 44100.0 / 512.0 + 0.5);
            float peak = 0.f;
            for (int i = 0; i < n; ++i)
            {
                buf.clear();
                proc->processBlock(buf, midi);
                if (midi.getNumEvents() > 0)
                    midi.clear(); // events fire exactly once
                peak = juce::jmax(peak, buf.getMagnitude(0, 512));
            }
            return peak;
        };

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        const float ringPeak = runBlocks(0.5);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.0f), 0); // vel-0 off
        runBlocks(0.05);
        const float tailPeak = runBlocks(1.5);
        vel0Ok = ringPeak > 0.01f && tailPeak < ringPeak * 0.15f;
        std::printf("ezinsttest: vel0off ring=%.4f tail=%.4f -> %s\n",
                    ringPeak, tailPeak, vel0Ok ? "RELEASES ok" : "STUCK!");
    }

    const float polyRatio = singleRms > 0 ? chordRms / singleRms : 0;
    const float envRatio = singleRms > 0 ? tailRms / singleRms : 1;
    // The normalized field-recording material is gentler than a 0.01 RMS.
    const bool pass = singleRms > 0.003f && polyRatio > 1.5f && envRatio < 0.6f
                      && vel0Ok;
    std::printf("ezinsttest: singleRms=%.4f tailRms=%.4f chordRms=%.4f "
                "poly=%.2f env=%.2f -> %s\n",
                singleRms, tailRms, chordRms, polyRatio, envRatio,
                pass ? "EZINSTRUMENTS WORKS" : "NO SOUND / BROKEN");
    return pass ? 0 : 1;
}

int cmdEzSynthUiTest()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> proc(ezsynth::create());
    proc->prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
    if (ed == nullptr)
    {
        std::printf("ezsynthuitest: no editor\n");
        return 1;
    }
    bool ok = true;
    const std::pair<int, int> sizes[] = { { 1020, 700 }, { 600, 420 },
                                          { 1400, 900 } };
    for (const auto& sz : sizes)
    {
        ed->setSize(sz.first, sz.second);
        ok = ok && ed->getWidth() == sz.first && ed->getHeight() == sz.second;
    }
    // Exercise the clip/LINES page layout too.
    ezsynth::setEditorPageForTest(ed.get(), 1);
    ed->setSize(1020, 700);
    ezsynth::setEditorPageForTest(ed.get(), 0);
    ed->setSize(1020, 700);
    const int nparams = (int) proc->getParameters().size();
    ok = ok && nparams == 50;

    // State round-trip: a non-default patch must survive save + load.
    auto byId = [](juce::AudioProcessor& p, const char* id)
        -> juce::AudioProcessorParameterWithID* {
        for (auto* q : p.getParameters())
            if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*>(q))
                if (w->paramID == juce::String(id))
                    return w;
        return nullptr;
    };
    auto setC = [&](const char* id, float v) {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(byId(*proc, id)))
            c->setValueNotifyingHost(c->convertTo0to1(v));
    };
    auto setF = [&](const char* id, float v) {
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(byId(*proc, id)))
            f->setValueNotifyingHost(f->convertTo0to1(v));
    };
    setC("o1wave", 1.f);
    setF("o1det", 22.f);
    setF("fltcut", 3000.f);
    juce::MemoryBlock mb;
    proc->getStateInformation(mb);
    std::unique_ptr<juce::AudioProcessor> p2(ezsynth::create());
    p2->prepareToPlay(48000.0, 512);
    p2->setStateInformation(mb.getData(), (int) mb.getSize());
    auto val = [&](juce::AudioProcessor& p, const char* id) -> float {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(byId(p, id)))
            return (float) c->getIndex();
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(byId(p, id)))
            return f->get();
        return -999.f;
    };
    const bool stateOk = val(*p2, "o1wave") == 1.f
                         && std::abs(val(*p2, "o1det") - 22.f) < 0.01f
                         && std::abs(val(*p2, "fltcut") - 3000.f) < 1.f;
    ok = ok && stateOk;

    // Preset bank: every patch must sound (non-silent, not clipping).
    const int nPresets = ezsynth::presetCountForTest();
    int presetsOk = 0;
    for (int i = 0; i < nPresets; ++i)
    {
        ezsynth::applyPresetForTest(proc.get(), i);
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        float peak = 0.f;
        // Long window: slow-attack patches (pads, risers) need time to bloom.
        for (int b = 0; b < 240; ++b)
        {
            buf.clear();
            proc->processBlock(buf, midi);
            midi.clear();
            peak = juce::jmax(peak, buf.getMagnitude(0, buf.getNumSamples()));
        }
        if (peak > 1e-3f && peak < 4.f)
            ++presetsOk;
        else
            std::printf("ezsynthuitest: preset %d peak=%.4f BAD\n", i, peak);
    }
    ok = ok && presetsOk == nPresets;

    std::printf("ezsynthuitest: editor %dx%d params=%d state=%s presets=%d/%d "
                "-> %s\n",
                ed->getWidth(), ed->getHeight(), nparams,
                stateOk ? "ok" : "LOST", presetsOk, nPresets,
                ok ? "UI OK" : "BROKEN");
    proc->editorBeingDeleted(ed.get());
    ed.reset();
    return ok ? 0 : 1;
}

int cmdEzSynthTest()
{
    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Keys", 0,
                                       cadence::Track::Type::Instrument);

    int idx = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (engine.pluginNameAt(i) == juce::String("EZSynth"))
        {
            idx = i;
            break;
        }
    if (idx < 0)
    {
        std::printf("ezsynthtest: EZSynth not registered\n");
        return 1;
    }
    if (engine.setTrackInstrumentPlugin(track, idx).failed())
    {
        std::printf("ezsynthtest: setTrackInstrumentPlugin failed\n");
        return 1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    engine.addMidiClip(track, 0.0, 2.5);
    auto clip = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip, 60, 0.8, 0.1, 1.0);
    engine.renderToFileEx(juce::File("/tmp/cadence_ezsynth_single.wav"), 999, 16);
    const float singleRms = rmsInRange(formats,
        juce::File("/tmp/cadence_ezsynth_single.wav"), 0.2, 0.9);
    const float tailRms = rmsInRange(formats,
        juce::File("/tmp/cadence_ezsynth_single.wav"), 1.4, 1.9);

    engine.removeClip(clip);
    engine.addMidiClip(track, 0.0, 2.5);
    clip = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip, 60, 0.8, 0.1, 1.0);
    engine.midiClipAddNote(clip, 64, 0.8, 0.1, 1.0);
    engine.midiClipAddNote(clip, 67, 0.8, 0.1, 1.0);
    engine.renderToFileEx(juce::File("/tmp/cadence_ezsynth_chord.wav"), 999, 16);
    const float chordRms = rmsInRange(formats,
        juce::File("/tmp/cadence_ezsynth_chord.wav"), 0.2, 0.9);

    // Vel-0 note-off regression through the direct processor.
    bool vel0Ok = false;
    {
        std::unique_ptr<juce::AudioProcessor> proc(ezsynth::create());
        proc->prepareToPlay(44100, 512);
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        auto runBlocks = [&](double secs) {
            const int n = (int) (secs * 44100.0 / 512.0 + 0.5);
            float peak = 0.f;
            for (int i = 0; i < n; ++i)
            {
                buf.clear();
                proc->processBlock(buf, midi);
                if (midi.getNumEvents() > 0)
                    midi.clear();
                peak = juce::jmax(peak, buf.getMagnitude(0, 512));
            }
            return peak;
        };
        // Default motor = Synth.
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        const float sring = runBlocks(0.5);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.0f), 0);
        runBlocks(0.8); // 0.3 s synth release + margin
        const float stail = runBlocks(1.0);
        const bool synthOk = sring > 0.01f && stail < sring * 0.15f;
        std::printf("ezsynthtest: vel0off(synth) ring=%.4f tail=%.4f -> %s\n",
                    sring, stail, synthOk ? "RELEASES ok" : "STUCK!");

        // Piano engine: switch motor, same release contract.
        auto* motor = dynamic_cast<juce::AudioParameterChoice*>(
            proc->getParameters()[0]);
        bool pianoOk = false;
        if (motor != nullptr)
        {
            motor->setValueNotifyingHost(motor->convertTo0to1(0.f));
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            const float ring = runBlocks(0.5);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.0f), 0);
            runBlocks(0.4); // ~150 ms damper release + margin
            const float tail = runBlocks(1.5);
            pianoOk = ring > 0.01f && tail < ring * 0.15f;
            std::printf("ezsynthtest: vel0off(piano) ring=%.4f tail=%.4f -> %s\n",
                        ring, tail, pianoOk ? "RELEASES ok" : "STUCK!");
        }
        vel0Ok = synthOk && pianoOk;
    }

    // ---- LINES: polyphonic warp (per-voice pitch pull) ----
    bool warpOk = false;
    {
        const std::vector<std::pair<float, float>> curve = { { 0.f, 72.f },
                                                             { 2.f, 72.f } };
        auto makeProc = [] {
            auto p = std::unique_ptr<juce::AudioProcessor>(ezsynth::create());
            p->prepareToPlay(48000.0, 512);
            auto set = [&](const char* id, float v) {
                for (auto* q : p->getParameters())
                {
                    auto* wid =
                        dynamic_cast<juce::AudioProcessorParameterWithID*>(q);
                    if (wid == nullptr || wid->paramID != juce::String(id))
                        continue;
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(q))
                        f->setValueNotifyingHost(f->convertTo0to1(v));
                    else if (auto* c =
                                 dynamic_cast<juce::AudioParameterChoice*>(q))
                        c->setValueNotifyingHost(c->convertTo0to1(v));
                    else if (auto* n =
                                 dynamic_cast<juce::AudioParameterInt*>(q))
                        n->setValueNotifyingHost(n->convertTo0to1(v));
                    return;
                }
            };
            set("motor", 1.f); set("o1wave", 0.f); set("o1lvl", 0.9f);
            set("o2lvl", 0.f); set("o3lvl", 0.f); set("sublvl", 0.f);
            set("noise", 0.f); set("uni", 1.f); set("flttype", 0.f);
            set("fltcut", 16000.f); set("fltres", 0.f); set("fltdrive", 1.f);
            set("aenvA", 0.001f); set("aenvD", 4.f); set("aenvS", 1.f);
            set("aenvR", 0.2f); set("vel", 0.f); set("level", 1.f);
            set("glide", 0.f); set("l1depth", 0.f); set("l2depth", 0.f);
            return p;
        };
        auto hzOf = [](juce::AudioProcessor& proc, int note) {
            const int blocks = 24; // ~0.256 s
            juce::AudioBuffer<float> buf(2, 512), acc(2, blocks * 512);
            acc.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                proc.processBlock(buf, midi);
                midi.clear();
                for (int ch = 0; ch < 2; ++ch)
                    acc.copyFrom(ch, b * 512, buf, ch, 0, 512);
            }
            const float* d = acc.getReadPointer(0);
            const int n = acc.getNumSamples();
            int zc = 0;
            for (int i = n / 2 + 1; i < n; ++i)
                if (d[i - 1] <= 0.f && d[i] > 0.f)
                    ++zc;
            return zc / ((n - n / 2) / 48000.0);
        };
        auto near = [](double a, double b) {
            return std::abs(a - b) < b * 0.06;
        };

        auto p0 = makeProc();
        const double base = hzOf(*p0, 69);          // A4 = 440 Hz
        auto p1 = makeProc();
        ezsynth::setWarpForTest(p1.get(), curve, 1.0, 1.0f, 12.f);
        const double pulled = hzOf(*p1, 69);        // 69 -> toward 72
        auto p2 = makeProc();
        ezsynth::setWarpForTest(p2.get(), curve, 1.0, 1.0f, 12.f);
        const double onCurve = hzOf(*p2, 72);       // 72 sits on the curve

        // 69 (diff 3, R 12) shifts +2.25 st = 501.1 Hz; 72 stays 523.25 Hz —
        // two notes under the SAME area move by DIFFERENT amounts.
        const bool baseOk = near(base, 440.0);
        const bool pulledOk = near(pulled, 501.1);
        const bool curveOk = near(onCurve, 523.25);

        // Simultaneous polyphony: a chord of 69 + 72 under the SAME area
        // must keep BOTH warped pitches (501 & 523) and lose the raw 440 —
        // the distortion is per voice, not a global transposition.
        auto renderChord = [&]() {
            auto p = makeProc();
            ezsynth::setWarpForTest(p.get(), curve, 1.0, 1.0f, 12.f);
            const int blocks = 32;
            juce::AudioBuffer<float> buf(2, 512), acc(2, blocks * 512);
            acc.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                p->processBlock(buf, midi);
                midi.clear();
                for (int ch = 0; ch < 2; ++ch)
                    acc.copyFrom(ch, b * 512, buf, ch, 0, 512);
            }
            std::vector<float> x((size_t)(blocks * 512));
            for (int i = 0; i < blocks * 512; ++i)
                x[(size_t) i] = acc.getSample(0, i);
            return x;
        };
        auto goertzel = [](const std::vector<float>& x, double f) {
            const double sr = 48000.0;
            const int n = (int) x.size();
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double win =
                    0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
                const double w = 2.0 * M_PI * f * i / sr;
                re += x[(size_t) i] * win * std::cos(w);
                im += x[(size_t) i] * win * std::sin(w);
            }
            return std::sqrt(re * re + im * im) / n;
        };
        const auto chord = renderChord();
        const double m440 = goertzel(chord, 440.0);
        const double m501 = goertzel(chord, 501.1);
        const double m523 = goertzel(chord, 523.25);
        const bool chordOk = m501 > 3.0 * m440 && m523 > 3.0 * m440;

        warpOk = baseOk && pulledOk && curveOk && chordOk;
        std::printf("ezsynthtest: warp base=%.1fHz pulled=%.1fHz(exp 501) "
                    "onCurve=%.1fHz(exp 523) chord 501/440=%.1f 523/440=%.1f "
                    "-> %s\n",
                    base, pulled, onCurve, m501 / juce::jmax(1e-9, m440),
                    m523 / juce::jmax(1e-9, m440),
                    warpOk ? "POLYPHONIC OK" : "BROKEN");
    }

    const float polyRatio = singleRms > 0 ? chordRms / singleRms : 0;
    const float envRatio = singleRms > 0 ? tailRms / singleRms : 1;
    // The normalized field-recording material is gentler than a 0.01 RMS.
    const bool pass = singleRms > 0.003f && polyRatio > 1.5f && envRatio < 0.6f
                      && vel0Ok && warpOk;
    std::printf("ezsynthtest: singleRms=%.4f tailRms=%.4f chordRms=%.4f "
                "poly=%.2f env=%.2f -> %s\n",
                singleRms, tailRms, chordRms, polyRatio, envRatio,
                pass ? "EZSYNTH WORKS" : "BROKEN");
    return pass ? 0 : 1;
}


// EZOrquestra end-to-end: (1) synthesize an Alpine-style tree (brass names
// C4_f.wav… + freesound-style strings without note names), (2) scan it into
// the programs dir, (3) load EZOrquestra on a track, switch program, render
// a note and check the audio. Cleans its program JSONs afterwards.
int cmdEzorqTest()
{
    using juce::String;
    const auto root = juce::File("/tmp/ezorq_lib");
    root.deleteRecursively();
    root.createDirectory();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto writeSine = [&](const juce::File& f, double hz) {
        f.getParentDirectory().createDirectory();
        constexpr double sr = 44100.0;
        const int n = (int) (sr * 1.2);
        std::vector<float> samples((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double env = juce::jmin(1.0, (n - i) / (0.3 * sr));
            samples[(size_t) i] =
                (float) (0.5 * env
                         * std::sin(juce::MathConstants<double>::twoPi * hz
                                    * i / sr));
        }
        const float* chans[1] = { samples.data() };
        f.deleteFile();
        auto stream =
            std::unique_ptr<juce::OutputStream>(f.createOutputStream());
        juce::AudioFormatWriterOptions o;
        o = o.withSampleRate(sr).withNumChannels(1).withBitsPerSample(16);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream, o);
        writer->writeFromFloatArrays(chans, 1, n);
        writer.reset();
        if (stream != nullptr)
            stream->flush();
    };

    // Brass-style: C4/E4/G4 with dynamics (parseable names).
    writeSine(root.getChildFile("Brass/Samples/ATestTrumpet Samples/atesttrumpet C4 f.wav"),
              261.63);
    writeSine(root.getChildFile("Brass/Samples/ATestTrumpet Samples/atesttrumpet C4 ff.wav"),
              261.63);
    writeSine(root.getChildFile("Brass/Samples/ATestTrumpet Samples/atesttrumpet E4 f.wav"),
              329.63);
    writeSine(root.getChildFile("Brass/Samples/ATestTrumpet Samples/atesttrumpet G4 ff.wav"),
              392.0);
    // Strings-style: freesound names, no note (pitch detection must nail it).
    writeSine(root.getChildFile("Strings/Samples/ATestViolin/atestviolin 1.wav"),
              293.66); // D4
    writeSine(root.getChildFile("Strings/Samples/ATestViolin/atestviolin 2.wav"),
              440.0);  // A4

    const auto outDir = juce::File::getSpecialLocation(
                            juce::File::userApplicationDataDirectory)
                            .getChildFile("EZMusicProduction/Orchestra");
    const int written = ezorq::scanLibrary(root, outDir);
    std::printf("ezorqtest: scan wrote %d programas\n", written);
    if (written < 2)
    {
        std::printf("ezorqtest: FAIL (expected >= 2 programs)\n");
        return 1;
    }

    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Orq", 0,
                                       cadence::Track::Type::Instrument);
    int idx = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (engine.pluginNameAt(i) == juce::String("EZOrquestra"))
        {
            idx = i;
            break;
        }
    if (idx < 0 || engine.setTrackInstrumentPlugin(track, idx).failed())
    {
        std::printf("ezorqtest: instrument setup failed\n");
        return 1;
    }
    // Program decode is asynchronous now — give the loader time.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    // First generated program (sorted): expect a Brass trumpet program.
    engine.addMidiClip(track, 0.0, 2.0);
    const auto clip = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip, 60, 0.8, 0.1, 1.0); // C4
    engine.renderToFileEx(juce::File("/tmp/cadence_ezorq.wav"), 999, 16);
    const float rms = rmsInRange(formats,
        juce::File("/tmp/cadence_ezorq.wav"), 0.15, 0.9);

    const bool pass = rms > 0.004f;
    std::printf("ezorqtest: rms=%.4f -> %s\n", rms,
                pass ? "EZORQ WORKS" : "BROKEN");
    // Editor paint smoke-test: the ADSR canvas bound a dangling member
    // reference once (declaration order) — crashing only on PAINT, which
    // no CLI test exercised until now.
    {
        std::unique_ptr<juce::AudioProcessor> proc(ezorq::create());
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        ed->setSize(940, 560);
        auto snap = ed->createComponentSnapshot(ed->getLocalBounds(), false);
        float peak = 0.f;
        for (int y = 0; y < snap.getHeight(); y += 7)
            for (int x = 0; x < snap.getWidth(); x += 7)
                peak = juce::jmax(peak, std::abs(snap.getPixelAt(x, y)
                                                     .getFloatRed()));
        std::printf("ezorqtest: editor snapshot pico=%.2f %s\n", peak,
                    peak > 0.01f ? "(pinta OK)" : "(VACIO!)");
    }
    // Cleanup ONLY the synthetic programs (never the user's real ones).
    for (const auto& f : outDir.findChildFiles(juce::File::findFiles, false,
                                               "*ATestViolin*.json"))
        f.deleteFile();
    for (const auto& f : outDir.findChildFiles(juce::File::findFiles, false,
                                               "*ATestTrumpet*.json"))
        f.deleteFile();
    return pass ? 0 : 1;
}


// Reproduces the app flow on a real project: load, drain plugin jobs,
// inspect track 4's instrument, arm + live MIDI + meter.
int cmdOrqLive(const char* path)
{
    cadence::Engine engine;
    if (engine.initialise().failed())
        std::fprintf(stderr, "warn: device: %s\n", engine.lastErrorUtf8());
    const auto r = engine.loadProject(juce::File(path));
    std::printf("load: %s\n", r.failed() ? "FAIL" : "ok");
    while (engine.hasPendingPluginJobs())
        engine.pumpOnePluginJob();

    const int n = engine.trackCount();
    std::printf("pistas: %d\n", n);
    for (int i = 0; i < n; ++i)
    {
        const auto id = engine.trackIdAt(i);
        std::printf("  pista %d: %s tipo=%d inst=%s live=%d\n", i + 1,
                    engine.trackName(id), engine.trackType(id),
                    engine.trackInstrumentName(id),
                    (int) engine.trackInstrumentLive(id));
    }

    // Track 4: arm + live MIDI + meter.
    const auto t4 = engine.trackIdAt(3);
    engine.setTrackArmed(t4, true);
    engine.debugPushLiveMidi(60, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const float p1 = engine.meterTrackPeak(3);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const float p2 = engine.meterTrackPeak(3);
    engine.debugPushLiveMidi(60, false);
    std::printf("orqlive: track4 live peaks %.4f / %.4f %s\n", p1, p2,
                p1 > 0.001f ? "SUENA" : "SILENCIO");

    // Playback of the clip through the transport.
    engine.setTrackSolo(t4, true);
    engine.seek(0.0);
    engine.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const float pb = engine.meterTrackPeak(3);
    engine.stop();
    std::printf("orqlive: track4 playback peak %.4f %s\n", pb,
                pb > 0.001f ? "SUENA" : "SILENCIO");
    return 0;
}


// Chromatic pitch audit: renders every semitone in the instrument range,
// measures the ACTUAL output frequency via autocorrelation, and reports
// any note that's off by >= 1 semitone from what was pressed.
// AUDIT MEASUREMENT — hypothesis test, not pitch estimation. Renders of
// real instruments have weak fundamentals (flutes: f0 at 17%, 2f0
// dominant) and formant bumps; generic pitch detectors lock onto 2f0 or
// the formant and report phantom octaves. But the audit KNOWS the
// expected pitch: test the three hypotheses k = -1,0,+1 (the render
// contains the harmonic series of expected×2^k) against the measured
// peak list, and require k=0 to win. Returns measured milli-Hz of the
// k=0 series, or 0/negative codes when a wrong octave wins.
static double auditMeasureHz(const float* d, int nSmp, double sr,
                             double expectedHz)
{
    // ADAPTIVE window on the NOTE'S RING: spiccato/staccato renders decay
    // in ~0.15 s while sustains run long — a fixed 0.4 s window lands on
    // silence+attack noise for short takes. Find the highest-energy 25 ms
    // hop, then extend while the ring lives (cap 0.5 s, floor 0.12 s).
    const int hop = (int) (0.025 * sr);
    const int scanEnd = juce::jmin(nSmp, (int) (1.4 * sr));
    int peakHop = -1;
    double peakR = 0.0;
    for (int s = 0; s + hop <= scanEnd; s += hop)
    {
        double r = 0.0;
        for (int i = s; i < s + hop; ++i)
            r += (double) d[i] * d[i];
        if (r > peakR)
        {
            peakR = r;
            peakHop = s;
        }
    }
    if (peakHop < 0 || peakR <= 0.0)
        return 0.0; // true silence
    int start = peakHop;
    int len = hop;
    {
        const double keep = peakR * 0.15;
        for (int s = peakHop + hop; s + hop <= scanEnd; s += hop)
        {
            double r = 0.0;
            for (int i = s; i < s + hop; ++i)
                r += (double) d[i] * d[i];
            if (r < keep)
                break;
            len += hop;
            if (len >= (int) (0.5 * sr))
                break;
        }
        const int minLen = (int) (0.12 * sr);
        if (len < minLen)
            len = juce::jmin(minLen, nSmp - start);
    }
    if (len < 2048)
        return 0.0;

    // Goertzel power scan, 2 Hz steps, 150..2600 Hz.
    constexpr int kFlo = 150, kFhi = 2600, kStep = 2;
    const int nFreq = (kFhi - kFlo) / kStep + 1;
    std::vector<double> pw((size_t) nFreq, 0.0);
    for (int fi = 0; fi < nFreq; ++fi)
    {
        const double f = (double) (kFlo + fi * kStep);
        const double coeff = 2.0 * std::cos(2.0 * 3.14159265358979 * f / sr);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double s0 = (double) d[start + i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        pw[(size_t) fi] = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }

    // Top-5 peaks, deduped ±50 Hz (vibrato spreads one partial over bins).
    std::vector<std::pair<double, double>> peaks; // (Hz, power)
    {
        std::vector<size_t> order((size_t) nFreq);
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return pw[a] > pw[b];
        });
        for (const size_t idx : order)
        {
            const double f = (double) (kFlo + (int) idx * kStep);
            bool dup = false;
            for (const auto& pk : peaks)
                if (std::abs(pk.first - f) < 50.0)
                {
                    dup = true;
                    break;
                }
            if (! dup)
                peaks.push_back({ f, pw[idx] });
            if (peaks.size() >= 5)
                break;
        }
    }
    if (peaks.empty())
        return 0.0;

    // Diagnostics: remember the dominant peak for the failure message.
    const double domHz = peaks[0].first;

    // Score the two FALSIFIABLE octave hypotheses. The sub-octave (k=-1)
    // is unfalsifiable by coverage — every harmonic of f0 is one of f0/2
    // — so it is not tested: a pure upper-series tie means the render is
    // an octave UP; any odd-harmonic energy keeps k=0 winning.
    double explained[2];
    for (int k = 0; k < 2; ++k)
    {
        const double f0 = expectedHz * std::pow(2.0, (double) k);
        const double tol = juce::jmax(6.0, f0 * 0.02);
        double cov = 0.0;
        for (const auto& pk : peaks)
        {
            const double h = juce::jmax(1.0, std::round(pk.first / f0));
            if (std::abs(pk.first - h * f0) <= tol)
                cov += pk.second;
        }
        explained[k] = cov;
    }
    if (explained[1] > explained[0] * 1.05)
        return -2.0; // the render's series is the UPPER octave: real error

    // Refine: power-weighted mean of peak/h over the k=0 series.
    const double f0 = expectedHz;
    const double tol = juce::jmax(6.0, f0 * 0.02);
    double num = 0.0, den = 0.0;
    for (const auto& pk : peaks)
    {
        const double h = juce::jmax(1.0, std::round(pk.first / f0));
        if (std::abs(pk.first - h * f0) <= tol)
        {
            num += (pk.first / h) * pk.second;
            den += pk.second;
        }
    }
    if (den <= 0.0)
    {
        // Peaks exist but match neither octave: the render plays
        // something unrelated (report dominant peak via fractional part).
        const double domNote = 69.0 + 12.0 * std::log2(domHz / 440.0);
        return -(3000.0 + domNote) / 1000.0; // e.g. -3.079 → note 79
    }
    return num / den;
}

static int auditNoteOnTrack(cadence::Engine& engine, int track,
                            juce::AudioFormatManager& formats, int note)
{
    engine.addMidiClip(track, 0.0, 1.5);
    const auto clip = engine.clipIdAt(track, 0);
    engine.midiClipAddNote(clip, note, 0.8, 0.05, 0.8);
    engine.renderToFileEx(juce::File("/tmp/cadence_audit_note.wav"), 999, 16);
    engine.removeClip(clip);

    juce::AudioFormatReader* r = formats.createReaderFor(
        juce::File("/tmp/cadence_audit_note.wav"));
    if (r == nullptr)
        return -1;
    const int sr = (int) r->sampleRate;
    const int nSmp = (int) juce::jmin((juce::int64) (sr * 1),
                                      r->lengthInSamples);
    juce::AudioBuffer<float> buf(1, nSmp);
    r->read(&buf, 0, nSmp, 0, true, true);
    delete r;

    const double expectedHz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const double hz = auditMeasureHz(buf.getReadPointer(0), nSmp,
                                     (double) sr, expectedHz);
    if (hz <= 0.0)
        return (int) std::lround(hz * 1000.0); // diagnostic codes pass through
    return (int) std::lround(hz * 1000.0);
}

static int auditRunProgram(cadence::Engine& engine, int track,
                           juce::AudioFormatManager& formats,
                           const int* notes, int nNotes)
{
    int wrong = 0;
    for (int ni = 0; ni < nNotes; ++ni)
    {
        const int note = notes[ni];
        const int mHz = auditNoteOnTrack(engine, track, formats, note);
        const double expectedHz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        if (mHz <= 0)
        {
            if (mHz == 0)
                std::printf("audit nota %d: SILENCIO (rms sin picos) ✗\n",
                            note);
            else if (mHz <= -3000)
                std::printf("audit nota %d: suena OTRO PITCH (~nota %.1f) "
                            "✗\n",
                            note, (double) (-(mHz) - 3000));
            else if (mHz == -2000)
                std::printf("audit nota %d: suena la OCTAVA SUPERIOR "
                            "(%.0f Hz) ✗\n",
                            note, expectedHz * 2.0);
            else
                std::printf("audit nota %d: serie no encontrada ✗\n", note);
            wrong++;
            continue;
        }
        const double measuredHz = mHz / 1000.0;
        const double cents = 1200.0 * std::log2(measuredHz / expectedHz);
        const bool ok = std::abs(cents) < 60;
        if (! ok)
        {
            std::printf("audit nota %d: expected %.1f Hz, got %.1f Hz "
                        "(%+.0f cents) ✗\n",
                        note, expectedHz, measuredHz, cents);
            wrong++;
        }
    }
    return wrong;
}

int cmdChromaticAudit(int argc = 0, char** argv = nullptr)
{
    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Audit", 0,
                                       cadence::Track::Type::Instrument);
    int idx = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (engine.pluginNameAt(i) == juce::String("EZOrquestra"))
        {
            idx = i;
            break;
        }
    if (idx < 0)
    {
        std::printf("audit: EZOrquestra not found\n");
        return 1;
    }
    engine.setTrackInstrumentPlugin(track, idx);
    ezorq::waitProgramsLoaded();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    int wrong = 0, total = 0;
    const int loNote = argc > 2 ? std::atoi(argv[2]) : 55;
    const int hiNote = argc > 3 ? std::atoi(argv[3]) : 84;
    for (int note = loNote; note <= hiNote; ++note)
    {
        if (auditRunProgram(engine, track, formats, &note, 1) > 0)
            wrong++;
        total++;
    }
    std::printf("audit: %d/%d notas correctas\n", total - wrong, total);
    return wrong > 0 ? 1 : 0;
}

// Full per-instrument validation: every program in the orchestra library,
// 5 notes spanning its range each. This is the guarantee that pressing a
// note produces that note on EVERY instrument, not just the flute.
int cmdChromaticAll(int from = 0, int to = -1)
{
    const auto dir = juce::File::getSpecialLocation(
                         juce::File::userApplicationDataDirectory)
                         .getChildFile("EZMusicProduction/Orchestra");
    juce::Array<juce::File> progFiles
        = dir.findChildFiles(juce::File::findFiles, false, "*.json");
    if (progFiles.isEmpty())
    {
        std::printf("chromaticall: sin programas en %s\n",
                    dir.getFullPathName().toRawUTF8());
        return 1;
    }
    juce::StringArray names;
    for (const auto& f : progFiles)
        names.add(f.getFileNameWithoutExtension());
    names.sort(true);

    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Audit", 0,
                                       cadence::Track::Type::Instrument);
    int idx = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (engine.pluginNameAt(i) == juce::String("EZOrquestra"))
        {
            idx = i;
            break;
        }
    if (idx < 0)
    {
        std::printf("chromaticall: EZOrquestra not found\n");
        return 1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    int totalWrong = 0, programsOk = 0;
    if (to < 0 || to >= names.size())
        to = names.size() - 1;
    for (int p = from; p <= to && p < names.size(); ++p)
    {
        ezorq::setProbeProgram(p);
        ezorq::clearProgramCache(); // bound memory: one program at a time
        engine.clearTrackInstrument(track);
        engine.setTrackInstrumentPlugin(track, idx);
        ezorq::waitProgramsLoaded();
        // Test notes WITHIN the instrument's real root range — pressing a
        // bass two octaves above its top key says nothing about tuning.
        int notes[5];
        int lo = 48, hi = 84;
        if (! ezorq::probeRange(&lo, &hi))
        {
            std::printf("chromaticall [%2d/%2d] %s: SIN RANGO\n",
                        p + 1, names.size(), names[p].toRawUTF8());
            totalWrong += 5;
            continue;
        }
        // Below note 34 (<55 Hz) the FFT detector can't measure the
        // fundamental — auditing there measures harmonics, not the engine.
        lo = juce::jmax(lo, 34);
        for (int k = 0; k < 5; ++k)
            notes[k] = lo + (int) std::lround(
                k * (double) (hi - lo) / 4.0);
        const int wrong = auditRunProgram(engine, track, formats, notes, 5);
        if (wrong == 0)
            programsOk++;
        else
            totalWrong += wrong;
        std::printf("chromaticall [%2d/%2d] %s [%d-%d]: %d/%d notas %s\n",
                    p + 1, names.size(), names[p].toRawUTF8(), lo, hi,
                    5 - wrong, 5, wrong > 0 ? "✗ ← REVISAR" : "✓");
        std::fflush(stdout);
    }
    std::printf("chromaticall: %d/%d programas correctos "
                "(%d notas mal en total)\n",
                programsOk, names.size(), totalWrong);
    return totalWrong > 0 ? 1 : 0;
}

int cmdPlugTest(const char* spec, bool asInstrument, bool demoVariant,
                bool recVariant, bool monVariant = false)
{
    cadence::Engine::setDebugLoopback(true);

    cadence::Engine engine;
    if (demoVariant)
        engine.loadDeviceSettings(juce::File(
            "/Users/ivan/Music/Cadence/settings.json")); // app-identical device
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: device: %s\n", engine.lastErrorUtf8());
        cadence::Engine::setDebugLoopback(false);
        return 1;
    }

    engine.newProject(48000);
    if (const char* iq = std::getenv("CADENCE_IQ"))
        engine.setInputQuantize(std::atof(iq));
    const auto track = engine.addTrack(
        asInstrument ? "Inst" : "Fx", 0,
        asInstrument ? cadence::Track::Type::Instrument
                     : cadence::Track::Type::Audio);

    // The transport auto-stops at the end of an empty project — give it a
    // clip so playback actually runs through the plugin. The "rec" variant
    // instead records loopback MIDI through the instrument plugin.
    juce::File toneFile("/tmp/cadence_plugtest_tone.wav");
    if (recVariant)
    {
        engine.setTrackArmed(track, true);
        if (engine.startRecord().failed())
        {
            std::fprintf(stderr, "error: record failed\n");
            cadence::Engine::setDebugLoopback(false);
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        engine.stopRecord();
        engine.seek(0.0); // replay the take from the top
        std::printf("plugtest: recorded %d note(s)\n",
                    engine.trackClipCount(track) > 0
                        ? engine.clipNoteCount(engine.clipIdAt(track, 0))
                        : 0);
        if (engine.trackClipCount(track) > 0)
        {
            const auto cid = engine.clipIdAt(track, 0);
            for (int n = 0; n < engine.clipNoteCount(cid) && n < 6; ++n)
            {
                int pitch = 0;
                float vel = 0;
                double start = 0, len = 0;
                engine.clipNoteAt(cid, n, &pitch, &vel, &start, &len);
                std::printf("plugtest: note %d start %.4f\n", n, start);
            }
        }
    }
    else if (monVariant)
    {
        // Live monitoring with the transport STOPPED: hold one note via the
        // debug injection and require SUSTAINED output across reads — the
        // old gating rendered only blocks that carried new MIDI events, so
        // notes died after one block (~10 ms).
        engine.setTrackArmed(track, true);
        std::printf("mon: instrument live=%d\n",
                    (int) engine.trackInstrumentLive(track));
        engine.debugPushLiveMidi(64, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const float p1 = engine.meterTrackPeak(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const float p2 = engine.meterTrackPeak(0);
        engine.debugPushLiveMidi(64, false);
        std::printf("plugtest: monitoring peaks %.4f / %.4f (sustained: %s)\n",
                    p1, p2, (p1 > 0.001f && p2 > 0.001f) ? "yes" : "NO");
        return (p1 > 0.001f && p2 > 0.001f) ? 0 : 1;
    }
    else if (writeToneWav(toneFile, 3.0, 440.0) != 0
             || engine.addClip(track, toneFile, 0.0, 1.0) < 0)
    {
        std::fprintf(stderr, "error: could not build test clip\n");
        cadence::Engine::setDebugLoopback(false);
        return 1;
    }

    // Demo-like variant: two more audio tracks like the app's demo project.
    if (demoVariant)
    {
        const auto t2 = engine.addTrack("Extra1", 3);
        engine.addClip(t2, toneFile, 2.0, 0.8);
        const auto t3 = engine.addTrack("Extra2", 5);
        engine.addClip(t3, toneFile, 4.0, 0.9);
    }

    engine.scanPluginsNow();
    int pluginIndex;
    if (spec[0] == ':')
    {
        // ":Name" — list order shifts between processes; names are stable.
        const auto wanted = juce::String::fromUTF8(spec + 1);
        pluginIndex = -1;
        int fallback = -1;
        for (int i = 0; i < engine.pluginCount(); ++i)
        {
            if (juce::String(engine.pluginNameAt(i)).containsIgnoreCase(wanted))
            {
                // Prefer the VST3 flavour when a name exists in both formats.
                if (std::strcmp(engine.pluginFormatAt(i), "VST3") == 0)
                {
                    pluginIndex = i;
                    break;
                }
                if (fallback < 0)
                    fallback = i;
            }
        }
        if (pluginIndex < 0)
            pluginIndex = fallback;
        if (pluginIndex < 0)
        {
            std::fprintf(stderr, "error: no plugin matching '%s'\n", spec + 1);
            cadence::Engine::setDebugLoopback(false);
            return 1;
        }
    }
    else
    {
        pluginIndex = std::atoi(spec);
        if (pluginIndex < 0 || pluginIndex >= engine.pluginCount())
        {
            std::fprintf(stderr, "error: plugin index %d out of range (%d found)\n",
                         pluginIndex, engine.pluginCount());
            cadence::Engine::setDebugLoopback(false);
            return 1;
        }
    }
    const std::string pname = engine.pluginNameAt(pluginIndex);
    const std::string pformat = engine.pluginFormatAt(pluginIndex);
    std::printf("plugtest: [%d] %s (%s%s) as %s\n", pluginIndex,
                pname.c_str(), pformat.c_str(),
                engine.pluginIsInstrumentAt(pluginIndex) ? ",inst" : "",
                asInstrument ? "instrument" : "fx");

    const juce::Result r = asInstrument
        ? engine.setTrackInstrumentPlugin(track, pluginIndex)
        : engine.addTrackFx(track, pluginIndex);
    if (r.failed())
    {
        std::fprintf(stderr, "error: could not add plugin: %s\n",
                     r.getErrorMessage().toRawUTF8());
        cadence::Engine::setDebugLoopback(false);
        return 1;
    }

    engine.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    engine.pause();
    cadence::Engine::setDebugLoopback(false);

    const double pos = engine.positionSeconds();
    const float peak = engine.meterTrackPeak(0); // read-and-reset, post-fx
    std::printf("plugtest: %s — %.2f s rendered, track0 peak %.4f\n",
                pos > 0.1 ? "ok" : "NO PROGRESS", pos, peak);
    if (recVariant)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        engine.renderToFileEx(juce::File("/tmp/cadence_plugtest_16.wav"),
                              999, 16);
        engine.renderToFileEx(juce::File("/tmp/cadence_plugtest_24.wav"),
                              999, 24);
        std::printf("plugtest: export rms16=%.4f rms24=%.4f\n",
                    rmsInRange(formats, juce::File("/tmp/cadence_plugtest_16.wav"),
                               0.05, 1.0),
                    rmsInRange(formats, juce::File("/tmp/cadence_plugtest_24.wav"),
                               0.05, 1.0));
    }
    if (demoVariant)
        std::printf("plugtest: track1 peak %.4f, track2 peak %.4f\n",
                    engine.meterTrackPeak(1), engine.meterTrackPeak(2));
    return (pos > 0.1 && peak > 0.001f) ? 0 : 1;
}

// EZDrums end-to-end: arm a track hosting the drum machine, record, and
// verify the plugin's OWN pattern landed as a normal (editable) MIDI clip
// — the "the machine records itself" contract.
int cmdEzDrumsTest()
{
    // NO debug loopback: the loopback synthesizes its own notes into every
    // armed MIDI capture — this test must measure ONLY what EZDrums
    // generated. The relaxed record gate lets an armed EZDrums track
    // record without any MIDI input.
    cadence::Engine engine;
    if (engine.initialise().failed())
    {
        std::fprintf(stderr, "error: device: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    engine.newProject(48000);
    const auto track = engine.addTrack("Drums", 0,
                                       cadence::Track::Type::Instrument);
    int idx = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
        if (engine.pluginNameAt(i) == juce::String("EZDrums"))
        {
            idx = i;
            break;
        }
    if (idx < 0)
    {
        std::printf("ezdrmtest: EZDrums no encontrado\n");
        return 1;
    }
    engine.setTrackInstrumentPlugin(track, idx);
    engine.setTrackArmed(track, true);

    if (engine.startRecord().failed())
    {
        std::printf("ezdrmtest: startRecord FALLÓ (¿gate?): %s\n",
                    engine.lastErrorUtf8());
        return 1;
    }
    // 120 bpm: a bar = 2 s. Record 2.5 bars.
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    engine.stopRecord();
    engine.seek(0.0);

    if (engine.trackClipCount(track) < 1)
    {
        std::printf("ezdrmtest: FAIL — ningún clip tras grabar\n");
        return 1;
    }
    const auto clip = engine.clipIdAt(track, 0);
    const int notes = engine.clipNoteCount(clip);
    std::printf("ezdrmtest: clip nuevo con %d notas\n", notes);
    if (notes < 20)
    {
        std::printf("ezdrmtest: FAIL — pocas notas (Rock 1 × 2.5 compases "
                    "≈ 37)\n");
        return 1;
    }

    // Notes must be GM drums and land ON the sixteenth grid (swing 0):
    // 120 bpm → sixteenth = 0.125 s. Tolerance 25 ms (block rounding).
    const std::array<int, 9> gm = { 36, 38, 39, 42, 46, 41, 48, 51, 49 };
    int badPitch = 0, offGrid = 0;
    for (int i = 0; i < notes; ++i)
    {
        int pitch = 0;
        float vel = 0.f;
        double startS = 0.0, lenS = 0.0;
        engine.clipNoteAt(clip, i, &pitch, &vel, &startS, &lenS);
        bool known = false;
        for (const int p : gm)
            if (p == pitch)
                known = true;
        if (! known)
            badPitch++;
        const double mod = std::fmod(startS, 0.125);
        const double dist = std::min(mod, 0.125 - mod);
        if (dist > 0.025)
            offGrid++;
    }
    if (badPitch > 0 || offGrid > 2)
    {
        std::printf("ezdrmtest: FAIL — %d notas fuera del mapa GM, %d fuera "
                    "de la rejilla\n",
                    badPitch, offGrid);
        for (int i = 0; i < juce::jmin(notes, 24); ++i)
        {
            int pitch = 0;
            float vel = 0.f;
            double startS = 0.0, lenS = 0.0;
            engine.clipNoteAt(clip, i, &pitch, &vel, &startS, &lenS);
            std::printf("  nota[%02d] pitch=%d vel=%.2f t=%.3f len=%.3f\n",
                        i, pitch, vel, startS, lenS);
        }
        return 1;
    }

    // Audible: offline render of the clip has real energy.
    engine.renderToFileEx(juce::File("/tmp/ezdrums_render.wav"), 20, 16);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::AudioFormatReader* r = formats.createReaderFor(
        juce::File("/tmp/ezdrums_render.wav"));
    if (r == nullptr)
    {
        std::printf("ezdrmtest: FAIL — render ilegible\n");
        return 1;
    }
    const int n = (int) juce::jmin<std::int64_t>(r->lengthInSamples,
                                                 (std::int64_t) r->sampleRate * 3);
    juce::AudioBuffer<float> buf(2, n);
    r->read(&buf, 0, n, 0, true, true);
    delete r;
    float peak = 0.f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            peak = juce::jmax(peak, std::abs(buf.getSample(c, i)));
    std::printf("ezdrmtest: pico render %.3f\n", peak);
    if (peak < 0.05f)
    {
        std::printf("ezdrmtest: FAIL — el render está mudo\n");
        return 1;
    }
    std::printf("EZDRUMS OK\n");
    return 0;
}


// EZArpeggio end-to-end: a sustained chord clip arpeggiated at 1/16 by the
// MIDI insert — the offline render must contain ~32 onsets (vs the single
// chord attack without the insert).
int cmdEzArpTest()
{
    cadence::Engine engine;
    engine.newProject(44100);
    const auto track = engine.addTrack("Arp", 0,
                                       cadence::Track::Type::Instrument);
    int synth = -1, arp = -1;
    for (int i = 0; i < engine.pluginCount(); ++i)
    {
        const juce::String n = engine.pluginNameAt(i);
        if (n == juce::String("EZSynth"))
            synth = i;
        if (n == juce::String("EZArpeggio"))
            arp = i;
    }
    if (synth < 0 || arp < 0)
    {
        std::printf("ezarptest: plugins no encontrados\n");
        return 1;
    }
    engine.setTrackInstrumentPlugin(track, synth);
    engine.addMidiClip(track, 0.0, 4.0);
    const auto clip = engine.clipIdAt(track, 0);
    for (const int p : { 60, 64, 67 }) // C major, held the whole clip
        engine.midiClipAddNote(clip, p, 0.9, 0.0, 4.0);

    auto countOnsets = [&](const char* path) {
        engine.renderToFileEx(juce::File(path), 10, 16);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::AudioFormatReader* r = formats.createReaderFor(juce::File(path));
        if (r == nullptr)
            return -1;
        const int n = (int) r->lengthInSamples;
        juce::AudioBuffer<float> buf(1, n);
        r->read(&buf, 0, n, 0, true, false);
        delete r;
        // RMS over 25 ms hops; onset = hop rising above 4x the noise floor.
        const int hop = (int) (0.025 * 44100);
        std::vector<float> rms;
        for (int s = 0; s + hop <= n; s += hop)
        {
            double e = 0.0;
            for (int i = s; i < s + hop; ++i)
                e += (double) buf.getSample(0, i) * buf.getSample(0, i);
            rms.push_back((float) std::sqrt(e / hop));
        }
        float floor_ = 1e9f;
        for (const float v : rms)
            floor_ = juce::jmin(floor_, v);
        int onsets = 0;
        bool inOnset = false;
        for (const float v : rms)
        {
            const bool hot = v > juce::jmax(0.004f, floor_ * 4.f);
            if (hot && ! inOnset)
                ++onsets;
            inOnset = hot;
        }
        return onsets;
    };

    const int plain = countOnsets("/tmp/ezarp_plain.wav");

    if (engine.setTrackMidiFx(track, arp).failed())
    {
        std::printf("ezarptest: FAIL — setTrackMidiFx\n");
        return 1;
    }
    const int arpOnsets = countOnsets("/tmp/ezarp_on.wav");

    // THE REAL PROOF OF ARPEGGIATION: score each chord tone by its FULL
    // harmonic family (f, 2f, 3f) in consecutive 1/16-aligned windows —
    // the dominant note must CYCLE (C-E-G-C-E-G...) with the arp; a plain
    // sustained chord keeps one dominant.
    auto pitchTransitions = [&](const char* path) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::AudioFormatReader* r = formats.createReaderFor(juce::File(path));
        if (r == nullptr)
            return -1;
        const int n = (int) r->lengthInSamples;
        juce::AudioBuffer<float> buf(1, n);
        r->read(&buf, 0, n, 0, true, false);
        delete r;
        constexpr double base[3] = { 261.63, 329.63, 392.0 }; // C4 E4 G4
        const int win = (int) (0.125 * 44100);
        int last = -1, transitions = 0;
        for (int s = (int) (0.125 * 44100); s + win < n; s += win)
        {
            int best = 0;
            double bestP = -1.0, total = 0.0;
            double scores[3] = { 0, 0, 0 };
            for (int k = 0; k < 3; ++k)
            {
                double fam = 0.0;
                for (int h = 1; h <= 3; ++h)
                {
                    const double f = base[k] * h;
                    const double coeff = 2.0 * std::cos(
                        2.0 * 3.14159265358979 * f / 44100.0);
                    double s1 = 0.0, s2 = 0.0;
                    for (int i = s; i < s + win; ++i)
                    {
                        const double x = (double) buf.getSample(0, i);
                        const double s0 = x + coeff * s1 - s2;
                        s2 = s1;
                        s1 = s0;
                    }
                    fam += s1 * s1 + s2 * s2 - coeff * s1 * s2;
                }
                scores[k] = fam;
                total += fam;
                if (fam > bestP)
                {
                    bestP = fam;
                    best = k;
                }
            }
            (void) total;
            // Confidence: the winner must own most of the family energy.
            if (bestP < total * 0.5)
                continue;
            if (last >= 0 && best != last)
                ++transitions;
            last = best;
        }
        return transitions;
    };
    const int transitions = pitchTransitions("/tmp/ezarp_on.wav");
    const int transitionsPlain = pitchTransitions("/tmp/ezarp_plain.wav");

    std::printf("ezarptest: onsets sin arp=%d con arp=%d · transiciones de "
                "pitch: sin arp=%d con arp=%d (se esperan muchas)\n",
                plain, arpOnsets, transitionsPlain, transitions);
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (const char* path : { "/tmp/ezarp_plain.wav", "/tmp/ezarp_on.wav" })
            if (auto* r = formats.createReaderFor(juce::File(path)))
            {
                const int n = (int) r->lengthInSamples;
                juce::AudioBuffer<float> b(1, n);
                r->read(&b, 0, n, 0, true, false);
                delete r;
                float peak = 0.f;
                for (int i = 0; i < n; ++i)
                    peak = juce::jmax(peak, std::abs(b.getSample(0, i)));
                std::printf("  %s pico=%.4f\n", path, peak);
            }
    }
    const bool ok = transitions >= 20 && transitions > transitionsPlain * 2;
    std::printf("%s\n", ok ? "EZARP OK" : "EZARP FAIL");
    return ok ? 0 : 1;
}


// MIDI project import end-to-end: build a Type-1 SMF (tempo 90, 3/4, two
// note tracks, one on channel 10), import it, verify tracks/tempo/notes;
// then import a stems folder and verify the audio tracks appear.
int cmdMidImportTest()
{
    cadence::Engine engine;
    engine.newProject(44100);

    // 1) Write a test SMF.
    {
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote(480);

        juce::MidiMessageSequence meta;
        meta.addEvent(juce::MidiMessage::tempoMetaEvent(60.0 / 90.0 * 1e6)); // 90 bpm
        meta.addEvent(juce::MidiMessage::timeSignatureMetaEvent(3, 4));
        meta.addEvent(juce::MidiMessage::textMetaEvent(3, "Conductor"));
        meta.updateMatchedPairs();
        mf.addTrack(meta);

        juce::MidiMessageSequence melody; // "Melodia": C-D-E, one per bar
        const int ppq = 480;
        for (int bar = 0; bar < 3; ++bar)
        {
            const int pitch = 60 + bar * 2;
            melody.addEvent(juce::MidiMessage::noteOn(1, pitch, 0.9f),
                            bar * 3 * ppq);
            melody.addEvent(juce::MidiMessage::noteOff(1, pitch),
                            bar * 3 * ppq + (int) (2.5 * ppq));
        }
        melody.addEvent(juce::MidiMessage::textMetaEvent(3, "Melodia"));
        melody.updateMatchedPairs();
        mf.addTrack(melody);

        juce::MidiMessageSequence drums; // ch 10: kick per bar
        for (int bar = 0; bar < 3; ++bar)
        {
            drums.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f),
                           bar * 3 * ppq);
            drums.addEvent(juce::MidiMessage::noteOff(10, 36),
                           bar * 3 * ppq + ppq / 2);
        }
        drums.updateMatchedPairs();
        mf.addTrack(drums);

        juce::File out("/tmp/midimport_test.mid");
        out.deleteFile();
        juce::FileOutputStream os(out);
        mf.writeTo(os, 1);
        os.flush();
    }

    if (engine.importMidiProject(juce::File("/tmp/midimport_test.mid")).failed())
    {
        std::printf("midimporttest: FAIL — importMidiProject: %s\n",
                    engine.lastErrorUtf8());
        return 1;
    }
    const int tracks = engine.trackCount();
    double bpm = engine.tempoBpm();
    const int num = engine.timeSigNumerator(), den = engine.timeSigDenominator();
    std::printf("midimporttest: %d pistas, %.0f bpm, %d/%d\n", tracks, bpm,
                num, den);
    if (tracks != 2 || std::abs(bpm - 90.0) > 1.0 || num != 3 || den != 4)
    {
        std::printf("midimporttest: FAIL — proyecto mal importado\n");
        return 1;
    }
    int totalNotes = 0, drumTracks = 0;
    for (int t = 0; t < tracks; ++t)
    {
        const auto id = engine.trackIdAt(t);
        const int clips = engine.trackClipCount(id);
        if (clips < 1)
        {
            std::printf("midimporttest: FAIL — pista sin clip\n");
            return 1;
        }
        totalNotes += engine.clipNoteCount(engine.clipIdAt(id, 0));
        if (juce::String(engine.trackName(id))
                .containsIgnoreCase("bater"))
            ++drumTracks;
    }
    std::printf("midimporttest: %d notas, %d pista(s) de batería\n",
                totalNotes, drumTracks);
    if (totalNotes != 6 || drumTracks != 1)
    {
        std::printf("midimporttest: FAIL — notas/batería\n");
        return 1;
    }

    // 2) Stems folder.
    {
        juce::File dir("/tmp/midimport_stems");
        dir.deleteRecursively();
        dir.createDirectory();
        juce::WavAudioFormat wav;
        for (const char* name : { "Bajo", "Pad" })
        {
            juce::File f(dir.getChildFile(juce::String(name) + ".wav"));
            f.deleteFile();
            // createWriterFor TAKES OWNERSHIP of the stream (deletes it
            // with the writer) — the stream must be heap-allocated.
            std::unique_ptr<juce::AudioFormatWriter> w(
                wav.createWriterFor(new juce::FileOutputStream(f), 44100.0,
                                    1, 16, {}, 0));
            juce::AudioBuffer<float> b(1, 44100);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample(0, i, 0.2f * std::sin(2.0 * 3.14159265358979
                                                  * 220.0 * i / 44100.0));
            w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
            w.reset();
        }
        if (engine.importStemsFolder(dir).failed())
        {
            std::printf("midimporttest: FAIL — importStemsFolder\n");
            return 1;
        }
        const int after = engine.trackCount();
        std::printf("midimporttest: %d pistas tras los stems\n", after);
        if (after != tracks + 2)
        {
            std::printf("midimporttest: FAIL — stems\n");
            return 1;
        }
    }
    std::printf("MIDIMPORT OK\n");
    return 0;
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

int cmdVaeTest(const juce::File& modelRoot)
{
    auto loadOne = [&] (const juce::File& dir, bool drums) -> int {
        cadence::magenta::MusicVAE model;
        juce::String error;
        const auto t0 = std::chrono::steady_clock::now();
        if (! model.load(dir, error))
        {
            std::fprintf(stderr, "load failed for %s: %s\n",
                         dir.getFullPathName().toRawUTF8(), error.toRawUTF8());
            return 1;
        }
        const double loadSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        std::printf("== %s (%s, %d steps, %.1f s)\n",
                    dir.getFileName().toRawUTF8(),
                    drums ? "drums" : "melody", model.numSteps(), loadSeconds);

        auto printSeq = [&] (const char* tag, const cadence::magenta::Sequence& seq) {
            std::printf("%s: %zu notes\n", tag, seq.notes.size());
            for (const auto& n : seq.notes)
                std::printf("  %.0f %.0f %.0f %.2f%s\n", (double) n.pitch,
                            n.startStep, n.lengthSteps, (double) n.velocity,
                            n.isDrum ? " drum" : "");
        };

        auto sampled = model.sample(0.9, 7);
        printSeq("sample(t=0.9,seed=7)", sampled);

        // Deterministic decode with z = 0: this is the reference check
        // against the TensorFlow.js implementation.
        std::vector<float> z((std::size_t) 256, 0.0f);
        auto zero = model.decode(z, 0.0, 0);
        printSeq("decode(z=0,t=0)", zero);

        std::vector<float> zconst((std::size_t) 256, 0.7f);
        auto constant = model.decode(zconst, 0.0, 0);
        printSeq("decode(z=0.7,t=0)", constant);

        // Encode a fixed melody/drum pattern, then reconstruct exactly.
        cadence::magenta::Sequence fixed;
        fixed.numSteps = model.numSteps();
        fixed.stepsPerQuarter = 4;
        if (drums)
        {
            const int pitches[] = { 36, 42, 38, 42, 36, 46, 38, 42 };
            for (int i = 0; i < 16; ++i)
            {
                cadence::magenta::Note n;
                n.pitch = pitches[i % 8];
                n.startStep = i * 4.0;
                n.lengthSteps = 1.0;
                n.velocity = 0.8f;
                n.isDrum = true;
                fixed.notes.push_back(n);
            }
        }
        else
        {
            const int pitches[] = { 60, 62, 63, 67, 65, 63, 62, 60 };
            for (int i = 0; i < 16; ++i)
            {
                cadence::magenta::Note n;
                n.pitch = pitches[i % 8];
                n.startStep = i * 4.0;
                n.lengthSteps = 3.0;
                n.velocity = 0.8f;
                fixed.notes.push_back(n);
            }
        }
        auto encoded = model.encode(fixed);
        std::printf("encode dims: %zu\n", encoded.size());
        std::printf("encode head:");
        for (std::size_t i = 0; i < juce::jmin<std::size_t>(8, encoded.size()); ++i)
            std::printf(" %.7f", (double) encoded[i]);
        std::printf("\n");
        printSeq("reconstruct", model.decode(encoded, 0.0, 0));
        std::printf("\n");
        return 0;
    };

    const auto mel = modelRoot.getChildFile("mel_4bar_med_lokl");
    const auto drums = modelRoot.getChildFile("drums_4bar_med_lokl");
    if (mel.isDirectory())
        loadOne(mel, false);
    if (drums.isDirectory())
        loadOne(drums, true);
    return (mel.isDirectory() || drums.isDirectory()) ? 0 : 1;
}

int cmdAiTest(const juce::File& modelDir)
{
    // Build a small C-minor arrangement: a pad track with a i-VI-III-VII
    // progression, so key/chord detection and generation have real context.
    cadence::Project project;
    project.sampleRate = 48000.0;
    project.tempoBpm = 96.0;
    project.nextTrackId = 2;
    project.nextClipId = 2;

    cadence::Track track;
    track.id = 1;
    track.type = cadence::Track::Type::Instrument;
    track.name = "Lead";
    track.instrument.name = "EZSynth";

    cadence::Clip clip;
    clip.id = 1;
    clip.startSeconds = 0.0;
    clip.lengthSeconds = 8.0;
    auto midi = std::make_shared<cadence::MidiClipData>();
    // i-VI-III-VII in C minor = Cm, Ab, Eb, Bb. The VI, III and VII are
    // major triads in the natural minor scale, so each chord needs its own
    // quality. Bars follow the project tempo (2.5 s at 96 BPM), not a guessed
    // duration.
    const int chordRoots[] = { 48, 56, 51, 58 }; // C, Ab, Eb, Bb
    const int chordThirds[] = { 3, 4, 4, 4 };    // minor, major, major, major
    const double barSeconds = (60.0 / project.tempoBpm) * 4.0;
    clip.lengthSeconds = 4.0 * barSeconds;
    for (int bar = 0; bar < 4; ++bar)
    {
        const int root = chordRoots[bar];
        for (int interval : { 0, chordThirds[bar], 7 })
        {
            cadence::MidiNote note;
            note.pitch = root + interval;
            note.startSeconds = bar * barSeconds;
            note.lengthSeconds = barSeconds;
            note.velocity = 0.7f;
            midi->notes.push_back(note);
        }
    }
    clip.midi = midi;
    track.clips.push_back(clip);
    project.tracks.push_back(track);

    cadence::ai::Service service;
    service.setModelDir(modelDir);
    std::printf("model dir: %s\n", modelDir.getFullPathName().toRawUTF8());
    std::printf("melody model: %s, drums model: %s\n",
                service.modelAvailable(cadence::ai::Mode::Melody) ? "yes" : "no",
                service.modelAvailable(cadence::ai::Mode::Drums) ? "yes" : "no");

    int failures = 0;
    auto check = [&failures] (bool ok, const char* what) {
        if (! ok)
        {
            std::fprintf(stderr, "ai-test: FAIL: %s\n", what);
            ++failures;
        }
    };

    cadence::ai::Result melodyResult;
    for (const auto mode : { cadence::ai::Mode::Melody,
                             cadence::ai::Mode::Bass,
                             cadence::ai::Mode::Harmony,
                             cadence::ai::Mode::Drums })
    {
        cadence::ai::Request request;
        request.trackId = 1;
        request.anchorSeconds = 0.0;
        request.bars = 4;
        request.mode = mode;
        request.variations = 3;
        request.seed = 12345;
        request.prompt = mode == cadence::ai::Mode::Melody
                             ? juce::String::fromUTF8("más oscuro, con arpegios")
                             : juce::String();
        const auto result = service.generate(project, request);
        if (mode == cadence::ai::Mode::Melody)
            melodyResult = result;
        check(result.ok, "generation returned an error");
        check(! result.notes.empty(), "generation produced no notes");
        check(result.seed != 0, "generation did not report a seed");
        std::printf("\n[%d] ok=%d provider=%s key=%s chords=%s start=%.2f notes=%zu seed=%d\n",
                    (int) mode, result.ok ? 1 : 0, result.provider.toRawUTF8(),
                    result.key.toRawUTF8(), result.chords.toRawUTF8(),
                    result.startSeconds, result.notes.size(), result.seed);
        if (result.error.isNotEmpty())
            std::printf("  error: %s\n", result.error.toRawUTF8());
        if (result.promptNote.isNotEmpty())
            std::printf("  prompt: %s\n", result.promptNote.toRawUTF8());
        if (result.contextNote.isNotEmpty())
            std::printf("  heard: %s\n", result.contextNote.toRawUTF8());
        int shown = 0;
        for (const auto& n : result.notes)
        {
            if (shown++ >= 12) break;
            std::printf("  %6.3f %5.3f pitch=%3d vel=%.2f%s\n",
                        n.startSeconds, n.lengthSeconds, n.pitch,
                        (double) n.velocity, n.isDrum ? " drum" : "");
        }
    }

    // The synthetic fixture is a C-minor i-VI-III-VII progression, so the
    // analysis must not drift to a relative key or enharmonic sharps.
    check(melodyResult.key == "C minor", "wrong detected key");
    check(melodyResult.chords == "Cm | Ab | Eb | Bb",
          "wrong detected chord progression");
    check(melodyResult.promptNote.containsIgnoreCase("minor")
              && melodyResult.promptNote.containsIgnoreCase("arpeggio"),
          "prompt interpretation lost the minor/arpeggio intent");

    // A prompt can retarget the mode: the request says melody but the text
    // asks for drums, so the result must switch to the drum path.
    cadence::ai::Request drumPrompt;
    drumPrompt.trackId = 1;
    drumPrompt.mode = cadence::ai::Mode::Melody;
    drumPrompt.bars = 4;
    drumPrompt.variations = 1;
    drumPrompt.seed = 999;
    drumPrompt.prompt = juce::String::fromUTF8("haz una batería con groove");
    const auto drumPromptResult = service.generate(project, drumPrompt);
    check(drumPromptResult.ok, "prompt mode override failed to generate");
    check(drumPromptResult.promptNote.containsIgnoreCase("mode drums"),
          "prompt mode override was not reported");
    bool promptAllDrums = ! drumPromptResult.notes.empty();
    for (const auto& n : drumPromptResult.notes)
        promptAllDrums = promptAllDrums && n.isDrum;
    check(promptAllDrums, "prompt mode override did not produce drum notes");

    // Long-form generation: 30 s at 96 BPM is 12 bars, i.e. three 4-bar model
    // windows that must continue from the previous phrase and cover the full
    // requested duration.
    cadence::ai::Request longRequest;
    longRequest.trackId = 1;
    longRequest.mode = cadence::ai::Mode::Melody;
    longRequest.bars = 4;             // legacy field; lengthSeconds wins
    longRequest.lengthSeconds = 30.0;
    longRequest.variations = 2;
    longRequest.seed = 777;
    longRequest.prompt = juce::String::fromUTF8("desarrolla una melodía larga");
    const auto longResult = service.generate(project, longRequest);
    check(longResult.ok, "long-form generation failed");
    check(longResult.clipLengthSeconds >= 29.9,
          "long-form clip is shorter than 30 s");
    check(longResult.notes.size() >= 16,
          "long-form generation produced too few notes");
    double lastStart = 0.0;
    int chunkNotes[3] = { 0, 0, 0 };
    for (const auto& n : longResult.notes)
    {
        lastStart = juce::jmax(lastStart, n.startSeconds);
        const int chunk = juce::jlimit(0, 2, (int) (n.startSeconds / 10.0));
        ++chunkNotes[chunk];
    }
    check(lastStart > 25.0, "long-form generation stopped early");
    check(chunkNotes[0] > 0 && chunkNotes[1] > 0 && chunkNotes[2] > 0,
          "long-form generation left one of the three phrases empty");
    check(longResult.contextNote.containsIgnoreCase("bars"),
          "long-form context was not reported");
    std::printf("\nlong form: ok=%d length=%.2fs bars=%s notes=%zu "
                "phrases=%d/%d/%d last=%.2fs\n",
                longResult.ok ? 1 : 0, longResult.clipLengthSeconds,
                longResult.contextNote.toRawUTF8(),
                longResult.notes.size(), chunkNotes[0], chunkNotes[1],
                chunkNotes[2], lastStart);

    // Audio "listening": a project with only a C-minor chord loop and an empty
    // target instrument track. Generation must hear the audio, take its key
    // and report the context instead of falling back to C major.
    const auto chordFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("cadence_ai_chord.wav");
    const std::vector<int> cMinorChord { 48, 60, 63, 67, 70 }; // C3 + Cm7
    check(writeChordWav(chordFile, 8.0, cMinorChord) == 0,
          "could not write the audio-listening fixture");

    juce::AudioFormatManager audioFormats;
    audioFormats.registerBasicFormats();
    cadence::AudioSourceData audioData;
    const auto loaded = cadence::loadAudioSourceData(
        audioFormats, chordFile, 48000.0, audioData);
    check(loaded.wasOk(), "could not decode the audio-listening fixture");

    cadence::Project audioProject;
    audioProject.sampleRate = 48000.0;
    audioProject.tempoBpm = 120.0;
    audioProject.nextTrackId = 3;
    audioProject.nextClipId = 2;
    const cadence::SourceId sourceId = 1;
    audioProject.sources[sourceId] =
        std::make_shared<const cadence::AudioSourceData>(std::move(audioData));
    audioProject.sourceFiles[sourceId] = chordFile.getFullPathName();

    cadence::Track audioTrack;
    audioTrack.id = 1;
    audioTrack.type = cadence::Track::Type::Audio;
    audioTrack.name = "Chord loop";
    cadence::Clip audioClip;
    audioClip.id = 1;
    audioClip.sourceId = sourceId;
    audioClip.startSeconds = 0.0;
    audioClip.lengthSeconds = 8.0;
    audioTrack.clips.push_back(audioClip);
    audioProject.tracks.push_back(audioTrack);

    cadence::Track targetTrack;
    targetTrack.id = 2;
    targetTrack.type = cadence::Track::Type::Instrument;
    targetTrack.name = "Lead";
    targetTrack.instrument.name = "EZSynth";
    audioProject.tracks.push_back(targetTrack);

    cadence::ai::Request listenRequest;
    listenRequest.trackId = 2;
    listenRequest.mode = cadence::ai::Mode::Melody;
    listenRequest.lengthSeconds = 8.0;
    listenRequest.variations = 1;
    listenRequest.seed = 321;
    listenRequest.useModel = false;
    const auto listenResult = service.generate(audioProject, listenRequest);
    check(listenResult.ok, "audio-context generation failed");
    check(listenResult.key == "C minor",
          "audio context did not drive key detection");
    check(listenResult.contextNote.containsIgnoreCase("heard"),
          "audio context was not reported");
    check(listenResult.contextNote.containsIgnoreCase("audio"),
          "audio context note is missing the audio mention");
    std::printf("audio listen: key=%s chords=%s heard=%s\n",
                listenResult.key.toRawUTF8(), listenResult.chords.toRawUTF8(),
                listenResult.contextNote.toRawUTF8());

    // End-to-end through the Engine: async job + apply + undo.
    cadence::Engine engine;
    engine.newProject(48000.0);
    const auto engineTrack = engine.addTrack("AI Lead", 0,
                                             cadence::Track::Type::Instrument);
    engine.addMidiClip(engineTrack, 0.0, 8.0);
    engine.setAiModelDir(modelDir);

    cadence::AiGenerationRequest req;
    req.trackId = engineTrack;
    req.anchorSeconds = 0.0;
    req.mode = cadence::AiMode::Melody;
    req.bars = 4;
    req.variations = 2;
    req.seed = 42;
    req.prompt = "menos notas, en Do menor";

    if (! engine.startAiGeneration(req))
    {
        std::fprintf(stderr, "could not start async generation\n");
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::seconds(120);
    while (engine.aiGenerationState() == cadence::Engine::kAiRunning
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));

    const auto result = engine.aiGenerationResult();
    std::printf("\nengine state=%d ok=%d provider=%s key=%s notes=%zu\n",
                engine.aiGenerationState(), result.ok ? 1 : 0,
                result.provider.toRawUTF8(), result.key.toRawUTF8(),
                result.notes.size());
    if (! result.ok)
    {
        std::fprintf(stderr, "generation failed: %s\n", result.error.toRawUTF8());
        return 1;
    }

    cadence::ClipId applied = -1;
    if (engine.applyAiGeneration(&applied).failed())
    {
        std::fprintf(stderr, "apply failed: %s\n", engine.lastErrorUtf8());
        return 1;
    }
    std::printf("applied clip %lld with %d notes; undo=%d\n",
                (long long) applied, engine.clipNoteCount(applied),
                engine.canUndo() ? 1 : 0);
    if (! engine.undo())
    {
        std::fprintf(stderr, "undo failed\n");
        return 1;
    }
    std::printf("after undo clip exists=%d\n", engine.clipIsMidi(applied) ? 1 : 0);
    const bool asyncOk = result.ok && engine.clipNoteCount(applied) == 0;
    if (failures == 0 && asyncOk)
        std::printf("\nai-test: all checks passed\n");
    return failures == 0 && asyncOk ? 0 : 1;
}

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

    if (cmd == "plugins")
        return cmdPlugins();

    if (cmd == "devplay")
    {
        // Switch to UR22 + play a tone in the SAME process to verify
        // the audio actually routes through the UR22.
        cadence::Engine engine;
        engine.initialise();
        engine.newProject(48000);
        const auto track = engine.addTrack("Tone", 0);
        writeToneWav(juce::File("/tmp/cadence_devplay.wav"), 3.0, 440.0);
        engine.addClip(track, juce::File("/tmp/cadence_devplay.wav"), 0.0, 1.0);

        engine.setDeviceConfig("Steinberg UR22", "", 48000, 256);
        std::printf("output: %s buffer: %d\n",
                    engine.currentOutputDeviceName().toRawUTF8(),
                    engine.currentBufferSizeSamples());

        engine.play();
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        engine.stop();
        std::printf("devplay: done — did you hear the tone through the UR22?\n");
        return 0;
    }

    if (cmd == "devswitch")
        return cmdTestDeviceSwitch();

    if (cmd == "mastertest")
        return cmdMasterTest();

    if (cmd == "devrestart")
        return cmdDeviceRestart();

    if (cmd == "churntest")
        return cmdChurnTest();

    if (cmd == "chordtest")
        return cmdChordTest();

    if (cmd == "notetest")
        return cmdNoteTest();

    if (cmd == "polytest")
        return cmdPolyTest();

    if (cmd == "stucktest")
        return cmdStuckTest();

    if (cmd == "chordrectest")
        return cmdChordRecTest();

    if (cmd == "ezqtest")
        return cmdEzQTest();

    if (cmd == "ezinsttest")
        return cmdEzInstTest();

    if (cmd == "ezsynthtest")
        return cmdEzSynthTest();
    if (cmd == "ezsynthuitest")
        return cmdEzSynthUiTest();

    if (cmd == "ezorqtest")
        return cmdEzorqTest();

    if (cmd == "ezorqtest")
        return cmdEzorqTest();

    if (cmd == "orqlive" && argc >= 3)
        return cmdOrqLive(argv[2]);

    if (cmd == "chromatic" && argc >= 2)
        return cmdChromaticAudit(argc, argv);

    if (cmd == "chromaticall")
        return cmdChromaticAll(argc > 2 ? std::atoi(argv[2]) : 0,
                               argc > 3 ? std::atoi(argv[3]) : -1);

    if (cmd == "ezorqscan" && argc >= 3)
    {
        const auto out = juce::File::getSpecialLocation(
                             juce::File::userApplicationDataDirectory)
                             .getChildFile("EZMusicProduction/Orchestra");
        const int n = ezorq::scanLibrary(
            juce::File(juce::String::fromUTF8(argv[2])), out);
        std::printf("ezorqscan: %d programas en %s\n", n,
                    out.getFullPathName().toRawUTF8());
        return n > 0 ? 0 : 1;
    }

    if (cmd == "qcapture" && argc >= 4)
        return cmdQCapture(argv[2], argv[3]);

    if (cmd == "midifixtest")
        return cmdMidiFixTest();

    if (cmd == "loaddiag" && argc >= 3)
        return cmdLoadDiag(argv[2]);

    if (cmd == "plugtest")
    {
        const bool asInstrument = argc > 3 && std::strcmp(argv[3], "inst") == 0;
        const bool demo = argc > 4 && std::strcmp(argv[4], "demo") == 0;
        const bool rec = argc > 4 && std::strcmp(argv[4], "rec") == 0;
        const bool mon = argc > 5 && std::strcmp(argv[5], "mon") == 0;
        return cmdPlugTest(argc > 2 ? argv[2] : const_cast<char*>("0"),
                           asInstrument, demo, rec, mon);
    }

    if (cmd == "ai-test")
    {
        const auto dir = argc >= 3
            ? juce::File(juce::String::fromUTF8(argv[2]))
            : cadence::ai::defaultModelDir();
        return cmdAiTest(dir);
    }

    if (cmd == "vae-test")
    {
        if (argc < 3)
        {
            printUsage();
            return 1;
        }
        return cmdVaeTest(juce::File(juce::String::fromUTF8(argv[2])));
    }

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

    if (cmd == "ezdrmtest")
        return cmdEzDrumsTest();

    if (cmd == "midimporttest")
        return cmdMidImportTest();

    if (cmd == "ezarptest")
        return cmdEzArpTest();

    if (cmd == "ezdrmkit")
    {
        // Sample-kit probe: instantiate EZDrums, pick the first sample kit
        // (index = synth kits + 0), prepare, hit the kick manually, render.
        auto* proc = ezdrums::create();
        juce::AudioParameterChoice* kit =
            dynamic_cast<juce::AudioParameterChoice*>(proc->getParameters()[0]);
        std::printf("kits: %d\n", kit->choices.size());
        const int sampleFirst = 8; // 8 synth kits precede the sample kits
        kit->setValueNotifyingHost(kit->convertTo0to1((float) sampleFirst));
        proc->prepareToPlay(44100.0, 512);
        juce::AudioBuffer<float> buf(2, 44100);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 38, 0.9f), 2205);
        proc->processBlock(buf, midi);
        float peak = 0.f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                peak = juce::jmax(peak, std::abs(buf.getSample(c, i)));
        std::printf("ezdrmkit: pico con kit de samples = %.4f %s\n", peak,
                    peak > 0.05f ? "OK" : "FAIL (mudo)");

        // Kit character check: kick decay time to -24 dB across synth kits.
        auto kickDecayMs = [&](int kitIdx) {
            kit->setValueNotifyingHost(kit->convertTo0to1((float) kitIdx));
            proc->prepareToPlay(44100.0, 512);
            juce::AudioBuffer<float> b(2, 88200);
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
            proc->processBlock(b, m);
            // RMS over 10 ms hops; find where the envelope falls under
            // -24 dB of the loudest hop (raw-sample thresholds cross zero
            // every half period — useless for decay).
            float loudest = 0.f;
            int loudHop = 0;
            for (int h = 0; h < 88; ++h)
            {
                double r = 0.0;
                for (int i = h * 441; i < (h + 1) * 441; ++i)
                    r += (double) b.getSample(0, i) * b.getSample(0, i);
                r /= 441.0;
                if (r > loudest)
                {
                    loudest = r;
                    loudHop = h;
                }
            }
            const double thr = loudest * 0.063 * 0.063;
            for (int h = loudHop; h < 88; ++h)
            {
                double r = 0.0;
                for (int i = h * 441; i < (h + 1) * 441; ++i)
                    r += (double) b.getSample(0, i) * b.getSample(0, i);
                if (r / 441.0 < thr)
                    return (double) (h * 441) / 44100.0 * 1000.0;
            }
            return 2000.0;
        };
        const double d909 = kickDecayMs(1), dTrap = kickDecayMs(4);
        std::printf("ezdrmkit: kick -24dB 909=%.0f ms vs trap=%.0f ms "
                    "(%s)\n",
                    d909, dTrap,
                    dTrap > d909 * 1.4 ? "kits diferenciados OK"
                                       : "FAIL: suenan igual");
        delete proc;
        return (peak > 0.05f && dTrap > d909 * 1.4) ? 0 : 1;
    }

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

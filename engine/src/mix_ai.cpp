#include "mix_ai.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <functional>
#include <limits>
#include <regex>
#include <utility>

namespace cadence::mixai {
namespace {

constexpr double kPi = 3.14159265358979323846;

float amplitudeDb(double x)
{
    return x <= 0.0 ? kFloorDb : juce::jmax(kFloorDb, (float) (20.0 * std::log10(x)));
}

float powerDb(double x)
{
    return x <= 0.0 ? kFloorDb : juce::jmax(kFloorDb, (float) (10.0 * std::log10(x)));
}

float roundHalf(float db)
{
    return std::round(db * 2.0f) * 0.5f;
}

juce::String signedNumber(float value, int decimals = 1)
{
    return (value >= 0.0f ? "+" : "") + juce::String(value, decimals);
}

// ---------- true peak ----------

class TruePeak
{
public:
    static constexpr int kPhases = 4;
    static constexpr int kTaps = 16;

    TruePeak()
    {
        const int n = kPhases * kTaps;
        const double center = (n - 1) * 0.5;
        const double beta = 8.0;
        for (int phase = 0; phase < kPhases; ++phase)
        {
            double taps[kTaps] {};
            for (int j = 0; j < kTaps; ++j)
            {
                const int k = j * kPhases + phase;
                const double x = (k - center) / kPhases;
                const double sinc = std::abs(x) < 1.0e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double r = 2.0 * k / (n - 1) - 1.0;
                const double w = besselI0(beta * std::sqrt(juce::jmax(0.0, 1.0 - r * r)))
                               / besselI0(beta);
                taps[j] = sinc * w;
            }
            double sum = 0.0;
            for (double v : taps)
                sum += v;
            for (int j = 0; j < kTaps; ++j)
                kernel_[phase][j] = (float) (taps[j] / sum);
        }
    }

    void push(const float* left, const float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float values[2] { left[i], right[i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                for (int j = kTaps - 1; j > 0; --j)
                    history_[ch][(size_t) j] = history_[ch][(size_t) j - 1];
                history_[ch][0] = values[ch];
                for (int phase = 0; phase < kPhases; ++phase)
                {
                    float acc = 0.0f;
                    for (int j = 0; j < kTaps; ++j)
                        acc += kernel_[phase][j] * history_[ch][(size_t) j];
                    peak_ = juce::jmax(peak_, std::abs(acc));
                }
            }
        }
    }

    float peak() const { return peak_; }

private:
    static double besselI0(double x)
    {
        double sum = 1.0;
        double term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x * 0.5) / k;
            sum += term * term;
        }
        return sum;
    }

    std::array<std::array<float, kTaps>, kPhases> kernel_ {};
    std::array<std::array<float, kTaps>, 2> history_ {};
    float peak_ = 0.0f;
};

// ---------- simple levels ----------

struct Levels
{
    float peak = 0.0f;
    double sumSqL = 0.0;
    double sumSqR = 0.0;
    double sumLR = 0.0;
    std::uint64_t samples = 0;
    std::uint64_t clipped = 0;

    void push(const float* left, const float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = left[i];
            const float r = right[i];
            peak = juce::jmax(peak, std::abs(l), std::abs(r));
            if (std::abs(l) >= 1.0f)
                ++clipped;
            if (std::abs(r) >= 1.0f)
                ++clipped;
            sumSqL += (double) l * (double) l;
            sumSqR += (double) r * (double) r;
            sumLR += (double) l * (double) r;
            ++samples;
        }
    }

    double rms() const
    {
        if (samples == 0)
            return 0.0;
        return std::sqrt((sumSqL + sumSqR) / (2.0 * (double) samples));
    }

    float correlation() const
    {
        const double denom = std::sqrt(sumSqL * sumSqR);
        if (denom <= 1.0e-20)
            return 0.0f;
        return (float) juce::jlimit(-1.0, 1.0, sumLR / denom);
    }
};

// ---------- BS.1770 loudness ----------

struct Biquad
{
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double tick(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

std::array<Biquad, 2> kWeighting(double sampleRate)
{
    Biquad shelf;
    {
        const double f0 = 1681.974450955533;
        const double gain = 3.999843853973347;
        const double q = 0.7071752369554196;
        const double k = std::tan(kPi * f0 / sampleRate);
        const double vh = std::pow(10.0, gain / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }

    Biquad highPass;
    {
        const double f0 = 38.13547087602444;
        const double q = 0.5003270373238773;
        const double k = std::tan(kPi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }

    return { shelf, highPass };
}

class LoudnessMeter
{
public:
    explicit LoudnessMeter(double sampleRate)
    {
        const auto k = kWeighting(juce::jmax(1.0, sampleRate));
        filters_[0] = k;
        filters_[1] = k;
        stepLen_ = juce::jmax(1, (int) std::lround(sampleRate * 0.1));
    }

    void push(const float* left, const float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            double yl = left[i];
            double yr = right[i];
            for (int stage = 0; stage < 2; ++stage)
            {
                yl = filters_[0][stage].tick(yl);
                yr = filters_[1][stage].tick(yr);
            }
            const double e = yl * yl + yr * yr;
            stepEnergy_ += e;
            totalEnergy_ += e;
            ++totalSamples_;
            ++stepFill_;
            if (stepFill_ == stepLen_)
            {
                steps_.push_back(stepEnergy_);
                stepEnergy_ = 0.0;
                stepFill_ = 0;
            }
        }
    }

    float integrated() const { return integratedSince(0); }

    float shortTermMax() const { return maxWindow(30); }

    float momentaryMax() const { return maxWindow(4); }

    float integratedSince(std::size_t step) const
    {
        std::vector<double> blocks = windows(4, step);
        if (blocks.empty())
        {
            if (step == 0 && totalSamples_ > 0)
            {
                blocks.push_back(totalEnergy_ / (double) totalSamples_);
            }
            else if (step < steps_.size())
            {
                double sum = 0.0;
                for (std::size_t i = step; i < steps_.size(); ++i)
                    sum += steps_[i];
                const double len = (double) (steps_.size() - step) * stepLen_;
                if (len > 0.0)
                    blocks.push_back(sum / len);
            }
        }

        std::vector<double> absolute;
        absolute.reserve(blocks.size());
        for (double z : blocks)
            if (z > 0.0 && loudness(z) > -70.0)
                absolute.push_back(z);
        if (absolute.empty())
            return kFloorDb;

        double mean = 0.0;
        for (double z : absolute)
            mean += z;
        mean /= (double) absolute.size();
        const double relative = loudness(mean) - 10.0;

        double gatedSum = 0.0;
        std::size_t gatedCount = 0;
        for (double z : absolute)
        {
            if (loudness(z) > relative)
            {
                gatedSum += z;
                ++gatedCount;
            }
        }
        if (gatedCount == 0)
            return kFloorDb;
        return juce::jmax(kFloorDb, (float) loudness(gatedSum / (double) gatedCount));
    }

private:
    static double loudness(double meanSquare)
    {
        return -0.691 + 10.0 * std::log10(meanSquare);
    }

    std::vector<double> windows(std::size_t count, std::size_t from) const
    {
        std::vector<double> result;
        if (from >= steps_.size() || steps_.size() - from < count)
            return result;
        const double len = (double) (count * stepLen_);
        for (std::size_t i = from; i + count <= steps_.size(); ++i)
        {
            double sum = 0.0;
            for (std::size_t j = 0; j < count; ++j)
                sum += steps_[i + j];
            result.push_back(sum / len);
        }
        return result;
    }

    float maxWindow(std::size_t count) const
    {
        auto values = windows(count, 0);
        if (values.empty() && totalSamples_ > 0)
            values.push_back(totalEnergy_ / (double) totalSamples_);
        double best = 0.0;
        for (double v : values)
            best = juce::jmax(best, v);
        return best <= 0.0 ? kFloorDb : juce::jmax(kFloorDb, (float) loudness(best));
    }

    std::array<std::array<Biquad, 2>, 2> filters_ {};
    int stepLen_ = 4800;
    int stepFill_ = 0;
    double stepEnergy_ = 0.0;
    std::vector<double> steps_;
    double totalEnergy_ = 0.0;
    std::size_t totalSamples_ = 0;
};

// ---------- five-band energy ----------

struct Butterworth
{
    double b[3] {};
    double a[2] {};
    double z[2] {};

    static Butterworth create(double freq, double sampleRate, bool highPass)
    {
        Butterworth f;
        freq = juce::jmin(freq, sampleRate * 0.45);
        const double w = 2.0 * kPi * freq / sampleRate;
        const double sn = std::sin(w);
        const double cs = std::cos(w);
        const double alpha = sn / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + alpha;
        if (highPass)
        {
            f.b[0] = (1.0 + cs) * 0.5 / a0;
            f.b[1] = -(1.0 + cs) / a0;
            f.b[2] = (1.0 + cs) * 0.5 / a0;
        }
        else
        {
            f.b[0] = (1.0 - cs) * 0.5 / a0;
            f.b[1] = (1.0 - cs) / a0;
            f.b[2] = (1.0 - cs) * 0.5 / a0;
        }
        f.a[0] = -2.0 * cs / a0;
        f.a[1] = (1.0 - alpha) / a0;
        return f;
    }

    double tick(double x)
    {
        const double y = b[0] * x + z[0];
        z[0] = b[1] * x - a[0] * y + z[1];
        z[1] = b[2] * x - a[1] * y;
        return y;
    }
};

struct Lr4
{
    Butterworth first;
    Butterworth second;

    static Lr4 create(double freq, double sampleRate, bool highPass)
    {
        const auto s = Butterworth::create(freq, sampleRate, highPass);
        return { s, s };
    }

    double tick(double x) { return second.tick(first.tick(x)); }
};

class BandMeter
{
public:
    explicit BandMeter(double sampleRate)
    {
        const double sr = juce::jmax(1.0, sampleRate);
        for (int band = 0; band < 5; ++band)
        {
            if (band > 0)
            {
                hp_[band] = Lr4::create(kCrossovers[band - 1], sr, true);
                hpOn_[band] = true;
            }
            if (band < 4)
            {
                lp_[band] = Lr4::create(kCrossovers[band], sr, false);
                lpOn_[band] = true;
            }
        }
    }

    void push(const float* left, const float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const double mid = 0.5 * ((double) left[i] + (double) right[i]);
            for (int band = 0; band < 5; ++band)
            {
                double y = mid;
                if (hpOn_[band])
                    y = hp_[band].tick(y);
                if (lpOn_[band])
                    y = lp_[band].tick(y);
                energy_[band] += y * y;
            }
            ++samples_;
        }
    }

    std::array<float, 5> energyDb() const
    {
        std::array<float, 5> result {};
        for (int i = 0; i < 5; ++i)
            result[i] = samples_ == 0 ? kFloorDb : powerDb(energy_[i] / (double) samples_);
        return result;
    }

private:
    static constexpr double kCrossovers[4] { 150.0, 500.0, 2000.0, 6000.0 };
    std::array<Lr4, 5> hp_ {};
    std::array<Lr4, 5> lp_ {};
    std::array<bool, 5> hpOn_ {};
    std::array<bool, 5> lpOn_ {};
    std::array<double, 5> energy_ {};
    std::uint64_t samples_ = 0;
};

// ---------- spectrum ----------

constexpr int kThirdOctaves = Spectrum::kThirdOctaves;
constexpr int kOctaves = Spectrum::kOctaves;
constexpr int kFrame = 8192;
constexpr int kHop = kFrame / 2;
constexpr float kLowEndHz = 120.0f;

constexpr float kThirdOctaveHz[kThirdOctaves] {
    20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
    200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f,
    1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f,
    10000.0f, 12500.0f, 16000.0f, 20000.0f
};

constexpr float kOctaveHz[kOctaves] {
    31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

int octaveOfThird(int third)
{
    return juce::jlimit(0, kOctaves - 1, juce::jmax(0, third - 1) / 3);
}

int thirdOfHz(float hz)
{
    if (hz < 17.8f || hz > 22400.0f)
        return -1;
    const float pos = std::log2(hz / 20.0f) * 3.0f;
    return juce::jlimit(0, kThirdOctaves - 1, (int) std::lround(pos));
}

class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer(double sampleRate)
        : sampleRate_(juce::jmax(1.0, sampleRate)),
          fft_(13)
    {
        window_.resize(kFrame);
        double w2 = 0.0;
        for (int n = 0; n < kFrame; ++n)
        {
            const float w = 0.5f - 0.5f * std::cos((float) (2.0 * kPi * n / kFrame));
            window_[(size_t) n] = w;
            w2 += (double) w * (double) w;
        }
        scale_ = 2.0 / ((double) kFrame * w2);
        left_.assign(kFrame, 0.0f);
        right_.assign(kFrame, 0.0f);
        fftBuffer_.assign((size_t) kFrame, {});

        const int bins = kFrame / 2 + 1;
        bandOfBin_.assign((size_t) bins, -1);
        for (int k = 0; k < bins; ++k)
        {
            const float hz = (float) ((double) k * sampleRate_ / (double) kFrame);
            bandOfBin_[(size_t) k] = k == 0 ? -1 : thirdOfHz(hz);
        }
        lowBins_ = juce::jlimit(1, bins - 1,
                                (int) (kLowEndHz * (float) kFrame / (float) sampleRate_));
    }

    void push(const float* left, const float* right, int n)
    {
        int i = 0;
        while (i < n)
        {
            const int take = juce::jmin(kFrame - filled_, n - i);
            std::copy(left + i, left + i + take, left_.begin() + filled_);
            std::copy(right + i, right + i + take, right_.begin() + filled_);
            filled_ += take;
            fresh_ += take;
            i += take;
            if (filled_ == kFrame)
            {
                processFrame();
                std::copy(left_.begin() + kHop, left_.end(), left_.begin());
                std::copy(right_.begin() + kHop, right_.end(), right_.begin());
                filled_ = kFrame - kHop;
                fresh_ = 0;
            }
        }
    }

    void finish(Spectrum& spectrum, Stereo& stereo, Timeline& timeline)
    {
        if (fresh_ > 0)
        {
            std::fill(left_.begin() + filled_, left_.end(), 0.0f);
            std::fill(right_.begin() + filled_, right_.end(), 0.0f);
            processFrame();
        }

        const double frames = (double) juce::jmax<std::uint64_t>(1, frames_);
        for (int b = 0; b < kThirdOctaves; ++b)
        {
            spectrum.thirdOctaveDb[(size_t) b] = powerDb(mid_[(size_t) b] / frames);
            spectrum.sideThirdOctaveDb[(size_t) b] = powerDb(side_[(size_t) b] / frames);
        }

        std::array<double, kOctaves> octaves {};
        for (int b = 0; b < kThirdOctaves; ++b)
            octaves[(size_t) octaveOfThird(b)] += mid_[(size_t) b] / frames;
        for (int o = 0; o < kOctaves; ++o)
            spectrum.octaveDb[(size_t) o] = powerDb(octaves[(size_t) o]);

        double midTotal = 0.0;
        double sideTotal = 0.0;
        for (int b = 0; b < kThirdOctaves; ++b)
        {
            midTotal += mid_[(size_t) b];
            sideTotal += side_[(size_t) b];
        }
        stereo.width = midTotal + sideTotal > 0.0
                           ? (float) (sideTotal / (midTotal + sideTotal))
                           : 0.0f;
        stereo.lowCorrelation = lowLl_ > 0.0 && lowRr_ > 0.0
                                    ? (float) (lowCross_ / std::sqrt(lowLl_ * lowRr_))
                                    : 1.0f;

        const int lowBands = thirdOfHz(kLowEndHz) >= 0 ? thirdOfHz(kLowEndHz) : 8;
        double lowMid = 0.0;
        double lowSide = 0.0;
        for (int b = 0; b < lowBands; ++b)
        {
            lowMid += mid_[(size_t) b];
            lowSide += side_[(size_t) b];
        }
        if (lowMid > 0.0)
        {
            stereo.lowSideDb = juce::jlimit(-60.0f, 60.0f,
                                           powerDb(lowSide / frames) - powerDb(lowMid / frames));
        }
        else
        {
            stereo.lowSideDb = lowSide > 0.0 ? 60.0f : -60.0f;
        }

        timeline.hopSeconds = (double) kHop / sampleRate_;
        timeline.frames = timeline_;
    }

private:
    void transform(const std::vector<float>& source)
    {
        std::fill(fftBuffer_.begin(), fftBuffer_.end(),
                  juce::dsp::Complex<float> {});
        for (int n = 0; n < kFrame; ++n)
            fftBuffer_[(size_t) n] = { source[(size_t) n] * window_[(size_t) n], 0.0f };
        fft_.perform(fftBuffer_.data(), fftBuffer_.data(), false);
    }

    void processFrame()
    {
        transform(left_);
        const auto leftSpec = fftBuffer_;
        transform(right_);
        const auto rightSpec = fftBuffer_;

        std::array<double, kThirdOctaves> mid {};
        std::array<double, kThirdOctaves> side {};
        const int bins = kFrame / 2 + 1;
        for (int k = 0; k < bins; ++k)
        {
            const double lr = leftSpec[(size_t) k].real();
            const double li = leftSpec[(size_t) k].imag();
            const double rr = rightSpec[(size_t) k].real();
            const double ri = rightSpec[(size_t) k].imag();

            if (k > 0 && k <= lowBins_)
            {
                lowCross_ += lr * rr + li * ri;
                lowLl_ += lr * lr + li * li;
                lowRr_ += rr * rr + ri * ri;
            }

            const int band = bandOfBin_[(size_t) k];
            if (band < 0)
                continue;
            const double mr = (lr + rr) * 0.5;
            const double mi = (li + ri) * 0.5;
            const double sr = (lr - rr) * 0.5;
            const double si = (li - ri) * 0.5;
            mid[(size_t) band] += mr * mr + mi * mi;
            side[(size_t) band] += sr * sr + si * si;
        }

        std::array<double, kOctaves> octaves {};
        for (int b = 0; b < kThirdOctaves; ++b)
        {
            const double m = mid[(size_t) b] * scale_;
            const double s = side[(size_t) b] * scale_;
            mid_[(size_t) b] += m;
            side_[(size_t) b] += s;
            octaves[(size_t) octaveOfThird(b)] += m;
        }

        std::array<float, kOctaves> frame {};
        for (int o = 0; o < kOctaves; ++o)
            frame[(size_t) o] = powerDb(octaves[(size_t) o]);
        timeline_.push_back(frame);
        ++frames_;
    }

    double sampleRate_ = 48000.0;
    juce::dsp::FFT fft_;
    std::vector<float> window_;
    double scale_ = 0.0;
    std::vector<float> left_;
    std::vector<float> right_;
    int filled_ = 0;
    int fresh_ = 0;
    std::vector<juce::dsp::Complex<float>> fftBuffer_;
    std::vector<int> bandOfBin_;
    int lowBins_ = 1;
    std::array<double, kThirdOctaves> mid_ {};
    std::array<double, kThirdOctaves> side_ {};
    double lowCross_ = 0.0;
    double lowLl_ = 0.0;
    double lowRr_ = 0.0;
    std::uint64_t frames_ = 0;
    std::vector<std::array<float, kOctaves>> timeline_;
};

// ---------- roles ----------

Role parseRole(const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t == "kick") return Role::Kick;
    if (t == "drums" || t == "drum" || t == "beat" || t == "kit" || t == "fill")
        return Role::Drums;
    if (t == "perc" || t == "percussion" || t == "clave")
        return Role::Perc;
    if (t == "bass" || t == "808" || t == "sub")
        return Role::Bass;
    if (t == "keys" || t == "piano" || t == "comping" || t == "chords" || t == "organ"
        || t == "rhodes")
        return Role::Keys;
    if (t == "guitar" || t == "gtr")
        return Role::Guitar;
    if (t == "pad" || t == "pads")
        return Role::Pad;
    if (t == "lead" || t == "hook" || t == "melody" || t == "arp" || t == "counter"
        || t == "pluck")
        return Role::Lead;
    if (t == "vocal" || t == "vocals" || t == "vox" || t == "voice")
        return Role::Vocal;
    if (t == "strings")
        return Role::Strings;
    if (t == "brass" || t == "horns")
        return Role::Brass;
    if (t == "fx" || t == "sfx")
        return Role::Fx;
    if (t == "other")
        return Role::Other;
    return Role::Other;
}

// ---------- masking internals ----------

struct MaskInput
{
    Role role = Role::Other;
    const std::vector<std::array<float, kOctaves>>* frames = nullptr;
    const std::array<float, kOctaves>* octaveDb = nullptr;
    const std::array<float, kThirdOctaves>* thirdOctaveDb = nullptr;
};

struct BandHit
{
    float overlap = 0.0f;
    float score = 0.0f;
    float levelA = 0.0f;
    float levelB = 0.0f;
};

constexpr std::pair<int, float> kMaskBands[8] {
    { 1, 1.0f }, { 2, 1.0f }, { 3, 0.9f }, { 4, 0.7f },
    { 5, 0.7f }, { 6, 0.9f }, { 7, 0.8f }, { 8, 0.5f }
};
constexpr float kPresentDb = -70.0f;
constexpr float kContributorDb = 10.0f;
constexpr float kCloseDb = 6.0f;
constexpr float kMinMaskScore = 0.15f;

std::vector<std::array<float, kOctaves>> totalDb(const std::vector<MaskInput>& tracks,
                                                 std::size_t frames)
{
    std::vector<std::array<double, kOctaves>> sums(frames);
    for (const auto& t : tracks)
    {
        if (t.frames == nullptr || t.frames->empty())
            continue;
        for (std::size_t f = 0; f < frames; ++f)
            for (int b = 0; b < kOctaves; ++b)
                sums[f][(size_t) b] += std::pow(10.0, (double) (*t.frames)[f][(size_t) b] / 10.0);
    }
    std::vector<std::array<float, kOctaves>> result(frames);
    for (std::size_t f = 0; f < frames; ++f)
        for (int b = 0; b < kOctaves; ++b)
            result[f][(size_t) b] = powerDb(sums[f][(size_t) b]);
    return result;
}

BandHit bandHit(const MaskInput& a, const MaskInput& b,
                const std::vector<std::array<float, kOctaves>>& total,
                int band, float weight)
{
    BandHit hit;
    if (a.frames == nullptr || b.frames == nullptr)
        return hit;
    const std::size_t frames = juce::jmin(a.frames->size(), b.frames->size());
    std::uint32_t both = 0;
    std::uint32_t hits = 0;
    float sumA = 0.0f;
    float sumB = 0.0f;
    for (std::size_t f = 0; f < frames && f < total.size(); ++f)
    {
        const float la = (*a.frames)[f][(size_t) band];
        const float lb = (*b.frames)[f][(size_t) band];
        if (la < kPresentDb || lb < kPresentDb)
            continue;
        ++both;
        const float floor = total[f][(size_t) band] - kContributorDb;
        if (la >= floor && lb >= floor && std::abs(la - lb) <= kCloseDb)
        {
            ++hits;
            sumA += la;
            sumB += lb;
        }
    }
    if (hits == 0)
        return hit;
    hit.overlap = (float) hits / (float) both;
    const float presence = std::sqrt((float) both / (float) juce::jmax((std::size_t) 1, frames));
    hit.score = hit.overlap * presence * weight;
    hit.levelA = sumA / (float) hits;
    hit.levelB = sumB / (float) hits;
    return hit;
}

int cutCandidate(const MaskInput& a, const MaskInput& b, int band)
{
    const float oa = roleOwnership(a.role, band);
    const float ob = roleOwnership(b.role, band);
    if (std::abs(oa - ob) > 0.05f)
        return oa < ob ? 0 : 1;
    const auto share = [band](const MaskInput& t)
    {
        float peak = -std::numeric_limits<float>::max();
        if (t.octaveDb != nullptr)
            for (float v : *t.octaveDb)
                peak = juce::jmax(peak, v);
        const float here = t.octaveDb != nullptr ? (*t.octaveDb)[(size_t) band] : kFloorDb;
        return here - peak;
    };
    return share(a) <= share(b) ? 0 : 1;
}

std::vector<MaskingPair> rankMasking(const std::vector<MaskInput>& tracks)
{
    std::vector<MaskingPair> result;
    if (tracks.size() < 2)
        return result;
    std::size_t frames = std::numeric_limits<std::size_t>::max();
    for (const auto& t : tracks)
        frames = juce::jmin(frames, t.frames != nullptr ? t.frames->size() : (std::size_t) 0);
    if (frames == std::numeric_limits<std::size_t>::max() || frames == 0)
        return result;

    const auto total = totalDb(tracks, frames);
    for (std::size_t a = 0; a < tracks.size(); ++a)
    {
        for (std::size_t b = a + 1; b < tracks.size(); ++b)
        {
            bool hasBest = false;
            MaskingPair best;
            for (const auto& [band, weight] : kMaskBands)
            {
                const auto hit = bandHit(tracks[a], tracks[b], total, band, weight);
                if (hit.score <= 0.0f)
                    continue;
                if (! hasBest || hit.score > best.score)
                {
                    hasBest = true;
                    best.a = (int) a;
                    best.b = (int) b;
                    best.bandHz = kOctaveHz[band];
                    best.overlap = hit.overlap;
                    best.levelDiffDb = hit.levelA - hit.levelB;
                    best.score = hit.score;
                    best.cut = (int) a + cutCandidate(tracks[a], tracks[b], band);
                }
            }
            if (hasBest && best.score >= kMinMaskScore)
                result.push_back(best);
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const MaskingPair& x, const MaskingPair& y) { return x.score > y.score; });
    return result;
}

float cutGainDb(float score)
{
    return score > 0.5f ? -4.0f : -3.0f;
}

constexpr float kCutQ = 1.4f;

} // namespace

// ---------- public DSP ----------

struct DetailAnalyzer::Impl
{
    explicit Impl(double sampleRate)
        : levels(), loudness(sampleRate), bands(sampleRate), spectrum(sampleRate)
    {
    }

    Levels levels;
    TruePeak truePeak;
    LoudnessMeter loudness;
    BandMeter bands;
    SpectrumAnalyzer spectrum;
};

DetailAnalyzer::DetailAnalyzer(double sampleRate)
    : impl_(std::make_unique<Impl>(juce::jmax(1.0, sampleRate)))
{
}

DetailAnalyzer::~DetailAnalyzer() = default;

void DetailAnalyzer::push(const float* left, const float* right, int numSamples)
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    impl_->levels.push(left, right, numSamples);
    impl_->truePeak.push(left, right, numSamples);
    impl_->loudness.push(left, right, numSamples);
    impl_->bands.push(left, right, numSamples);
    impl_->spectrum.push(left, right, numSamples);
}

Detail DetailAnalyzer::finish()
{
    Detail detail;
    const auto& levels = impl_->levels;
    const float peakDb = amplitudeDb((double) levels.peak);
    const float rmsDb = amplitudeDb(levels.rms());
    const float truePeak = juce::jmax(impl_->truePeak.peak(), levels.peak);
    const auto bands = impl_->bands.energyDb();

    detail.stats.peakDb = peakDb;
    detail.stats.truePeakDb = amplitudeDb((double) truePeak);
    detail.stats.rmsDb = rmsDb;
    detail.stats.lufsIntegrated = impl_->loudness.integrated();
    detail.stats.lufsShortTermMax = impl_->loudness.shortTermMax();
    detail.stats.crestFactorDb = levels.peak > 0.0f ? peakDb - rmsDb : 0.0f;
    detail.stats.clippedSamples = levels.clipped;
    detail.stats.bandEnergy.low = bands[0];
    detail.stats.bandEnergy.lowMid = bands[1];
    detail.stats.bandEnergy.mid = bands[2];
    detail.stats.bandEnergy.highMid = bands[3];
    detail.stats.bandEnergy.high = bands[4];
    detail.stats.stereoCorrelation = levels.correlation();

    impl_->spectrum.finish(detail.spectrum, detail.stereo, detail.timeline);
    return detail;
}

Detail analyzeBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    DetailAnalyzer analyzer(sampleRate);
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return analyzer.finish();
    const float* left = buffer.getReadPointer(0);
    const float* right = channels > 1 ? buffer.getReadPointer(1) : left;
    analyzer.push(left, right, samples);
    return analyzer.finish();
}

bool Detail::isSilent() const
{
    return stats.lufsIntegrated <= -70.0f && stats.peakDb <= -60.0f;
}

// ---------- spectrum helpers ----------

std::array<float, Spectrum::kOctaves> Spectrum::octaveShape() const
{
    std::array<float, kOctaves> shape {};
    float peak = kFloorDb;
    for (float v : octaveDb)
        peak = juce::jmax(peak, v);
    for (int i = 0; i < kOctaves; ++i)
        shape[(size_t) i] = peak <= kFloorDb ? 0.0f : juce::jmax(-60.0f, octaveDb[(size_t) i] - peak);
    return shape;
}

int Spectrum::peakOctave() const
{
    int best = 5;
    float level = kFloorDb;
    for (int i = 0; i < kOctaves; ++i)
    {
        if (octaveDb[(size_t) i] > level)
        {
            level = octaveDb[(size_t) i];
            best = i;
        }
    }
    return best;
}

// ---------- roles ----------

const char* roleName(Role role)
{
    switch (role)
    {
        case Role::Kick: return "kick";
        case Role::Drums: return "drums";
        case Role::Perc: return "perc";
        case Role::Bass: return "bass";
        case Role::Keys: return "keys";
        case Role::Guitar: return "guitar";
        case Role::Pad: return "pad";
        case Role::Lead: return "lead";
        case Role::Vocal: return "vocal";
        case Role::Strings: return "strings";
        case Role::Brass: return "brass";
        case Role::Fx: return "fx";
        case Role::Other: break;
    }
    return "other";
}

Role inferRole(const juce::String& name, const juce::String& instrument,
               const juce::StringArray& hints)
{
    const auto lower = name.toLowerCase();
    juce::StringArray words;
    juce::String current;
    for (auto c : lower)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(c))
            current << juce::String::charToString(c);
        else if (current.isNotEmpty())
        {
            words.add(current);
            current.clear();
        }
    }
    if (current.isNotEmpty())
        words.add(current);

    const auto has = [&words](const juce::StringArray& keys)
    {
        for (const auto& w : words)
            for (const auto& k : keys)
                if (w == k || (k.length() > 3 && w.startsWith(k)))
                    return true;
        return false;
    };

    if (has({ "kick", "bd" })) return Role::Kick;
    if (has({ "808", "bass", "sub", "tumbao" })) return Role::Bass;
    if (has({ "drum", "drums", "snare", "hat", "hats", "beat", "kit", "breaks" }))
        return Role::Drums;
    if (has({ "perc", "conga", "congas", "bongo", "bongos", "shaker", "clave", "claves",
              "tamb", "timbale", "guiro", "cowbell" }))
        return Role::Perc;
    if (has({ "vox", "vocal", "vocals", "voice", "choir", "singer" })) return Role::Vocal;
    if (has({ "gtr", "guitar", "guitars", "nylon", "strum" })) return Role::Guitar;
    if (has({ "pad", "pads", "atmos", "drone", "texture" })) return Role::Pad;
    if (has({ "string", "strings", "violin", "viola", "cello", "orchestra" }))
        return Role::Strings;
    if (has({ "brass", "horn", "horns", "trumpet", "trombone", "sax" })) return Role::Brass;
    if (has({ "lead", "hook", "melody", "pluck", "arp", "bell", "bells", "flute", "whistle" }))
        return Role::Lead;
    if (has({ "keys", "piano", "rhodes", "organ", "ep", "chords", "montuno", "comp" }))
        return Role::Keys;
    if (has({ "fx", "sfx", "riser", "sweep", "noise", "impact" })) return Role::Fx;

    const auto inst = instrument.trim().toLowerCase();
    if (inst == "drum_machine" || inst == "drum_sampler")
        return Role::Drums;
    if (inst == "electric_piano")
        return Role::Keys;

    for (const auto& hint : hints)
    {
        const auto role = parseRole(hint);
        if (role != Role::Other)
            return role;
    }
    return Role::Other;
}

float roleBalanceDb(Role role)
{
    switch (role)
    {
        case Role::Kick:
        case Role::Drums:
        case Role::Vocal: return 0.0f;
        case Role::Bass: return -1.0f;
        case Role::Lead: return -2.0f;
        case Role::Keys:
        case Role::Guitar: return -5.0f;
        case Role::Brass: return -6.0f;
        case Role::Strings: return -7.0f;
        case Role::Pad:
        case Role::Perc: return -9.0f;
        case Role::Fx: return -12.0f;
        case Role::Other: return -6.0f;
    }
    return -6.0f;
}

float roleHighPassHz(Role role, bool* hasValue)
{
    float hz = 0.0f;
    switch (role)
    {
        case Role::Kick:
        case Role::Bass:
        case Role::Drums: hz = 0.0f; break;
        case Role::Perc: hz = 250.0f; break;
        case Role::Keys: hz = 100.0f; break;
        case Role::Guitar: hz = 90.0f; break;
        case Role::Pad:
        case Role::Lead:
        case Role::Fx: hz = 150.0f; break;
        case Role::Vocal: hz = 90.0f; break;
        case Role::Strings: hz = 70.0f; break;
        case Role::Brass: hz = 110.0f; break;
        case Role::Other: hz = 60.0f; break;
    }
    if (hasValue != nullptr)
        *hasValue = hz > 0.0f;
    return hz;
}

bool roleSpreads(Role role)
{
    return role == Role::Perc || role == Role::Guitar || role == Role::Keys || role == Role::Pad
        || role == Role::Strings || role == Role::Brass;
}

float roleOwnership(Role role, int octave)
{
    static constexpr float table[13][kOctaves] {
        // Kick
        { 0.8f, 1.0f, 0.6f, 0.3f, 0.2f, 0.2f, 0.5f, 0.5f, 0.2f, 0.1f },
        // Drums
        { 0.6f, 1.0f, 0.6f, 0.4f, 0.3f, 0.3f, 0.6f, 0.7f, 0.9f, 0.9f },
        // Perc
        { 0.1f, 0.1f, 0.2f, 0.3f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.6f },
        // Bass
        { 0.9f, 0.9f, 1.0f, 0.6f, 0.3f, 0.2f, 0.2f, 0.1f, 0.1f, 0.1f },
        // Keys
        { 0.1f, 0.1f, 0.3f, 0.5f, 0.7f, 0.6f, 0.4f, 0.3f, 0.2f, 0.2f },
        // Guitar
        { 0.1f, 0.1f, 0.3f, 0.5f, 0.6f, 0.7f, 0.6f, 0.4f, 0.3f, 0.2f },
        // Pad
        { 0.1f, 0.1f, 0.2f, 0.3f, 0.4f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f },
        // Lead
        { 0.1f, 0.1f, 0.2f, 0.4f, 0.6f, 0.9f, 0.9f, 0.8f, 0.5f, 0.3f },
        // Vocal
        { 0.1f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f, 1.0f, 0.6f, 0.4f },
        // Strings
        { 0.1f, 0.2f, 0.4f, 0.5f, 0.6f, 0.6f, 0.5f, 0.4f, 0.3f, 0.3f },
        // Brass
        { 0.1f, 0.1f, 0.3f, 0.5f, 0.6f, 0.7f, 0.6f, 0.4f, 0.2f, 0.1f },
        // Fx
        { 0.1f, 0.1f, 0.3f, 0.5f, 0.6f, 0.7f, 0.6f, 0.4f, 0.3f, 0.2f },
        // Other
        { 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f }
    };
    const int row = juce::jlimit(0, 12, (int) role);
    return table[row][juce::jlimit(0, kOctaves - 1, octave)];
}

// ---------- targets ----------

const char* profileName(ToneProfile profile)
{
    switch (profile)
    {
        case ToneProfile::Pop: return "pop";
        case ToneProfile::Electronic: return "electronic";
        case ToneProfile::Hiphop: return "hiphop";
        case ToneProfile::Rock: return "rock";
        case ToneProfile::Acoustic: return "acoustic";
        case ToneProfile::Orchestral: return "orchestral";
    }
    return "pop";
}

ToneProfile profileFromGenre(const juce::String& text)
{
    const auto t = text.toLowerCase();
    const auto has = [&t](std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (t.contains(w))
                return true;
        return false;
    };
    if (has({ "lo-fi", "lofi", "hip hop", "hip-hop", "hiphop", "trap", "rap", "boom bap",
              "reggaeton", "dembow", "reggaetón" }))
        return ToneProfile::Hiphop;
    if (has({ "edm", "house", "techno", "electronic", "synthwave", "trance", "dubstep",
              "drum and bass", "dnb", "electro", "electrónica", "electronica" }))
        return ToneProfile::Electronic;
    if (has({ "rock", "metal", "punk", "grunge" }))
        return ToneProfile::Rock;
    if (has({ "orchestral", "cinematic", "classical", "film score", "string quartet",
              "orquestal", "clásica", "clasica", "clásico", "clasico",
              "banda sonora", "música clásica", "musica clasica" }))
        return ToneProfile::Orchestral;
    if (has({ "folk", "acoustic", "singer-songwriter", "unplugged", "salsa", "son ",
              "bolero", "jazz", "neo soul", "neo-soul", "acústica", "acustica",
              "acústico", "acustico" }))
        return ToneProfile::Acoustic;
    return ToneProfile::Pop;
}

ToneProfile profileFromName(const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t == "electronic") return ToneProfile::Electronic;
    if (t == "hiphop" || t == "hip-hop" || t == "hip hop") return ToneProfile::Hiphop;
    if (t == "rock") return ToneProfile::Rock;
    if (t == "acoustic") return ToneProfile::Acoustic;
    if (t == "orchestral") return ToneProfile::Orchestral;
    return ToneProfile::Pop;
}

ToneCurve profileCurve(ToneProfile profile)
{
    switch (profile)
    {
        case ToneProfile::Pop: return { 7.0f, 3.0f, -5.0f, -8.0f };
        case ToneProfile::Electronic: return { 9.0f, 2.0f, -4.0f, -6.0f };
        case ToneProfile::Hiphop: return { 10.0f, 2.0f, -6.0f, -9.0f };
        case ToneProfile::Rock: return { 4.0f, 2.0f, -2.0f, -6.0f };
        case ToneProfile::Acoustic: return { 2.0f, 1.0f, -3.0f, -7.0f };
        case ToneProfile::Orchestral: return { 1.0f, 0.0f, -4.0f, -9.0f };
    }
    return { 7.0f, 3.0f, -5.0f, -8.0f };
}

float MixTargets::balanceDb(Role role) const
{
    for (const auto& [r, db] : roles)
        if (r == role)
            return db;
    return roleBalanceDb(role);
}

// ---------- moves ----------

const char* eqKindName(EqKind kind)
{
    switch (kind)
    {
        case EqKind::Bell: return "bell";
        case EqKind::LowShelf: return "low_shelf";
        case EqKind::HighShelf: return "high_shelf";
        case EqKind::HighPass: return "high_pass";
        case EqKind::LowPass: return "low_pass";
    }
    return "bell";
}

EqKind eqKindFromName(const juce::String& name)
{
    auto t = name.trim().toLowerCase().replaceCharacter(' ', '_');
    if (t == "low_shelf" || t == "lowshelf") return EqKind::LowShelf;
    if (t == "high_shelf" || t == "highshelf") return EqKind::HighShelf;
    if (t == "high_pass" || t == "highpass") return EqKind::HighPass;
    if (t == "low_pass" || t == "lowpass") return EqKind::LowPass;
    return EqKind::Bell;
}

juce::String hzLabel(float hz)
{
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, 1) + " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}

juce::String panLabel(float pan)
{
    if (std::abs(pan) < 0.005f)
        return "center";
    return juce::String(juce::roundToInt(std::abs(pan) * 100.0f))
         + (pan < 0.0f ? "% left" : "% right");
}

juce::String Move::describe(const std::function<juce::String(int)>& name) const
{
    const juce::String who = name(trackId);
    switch (kind)
    {
        case Kind::Gain:
            if (hasDelta)
                return who + ": fader " + signedNumber(deltaDb) + " dB";
            if (hasDb)
                return who + ": fader to " + signedNumber(db) + " dB";
            return who + ": fader unchanged";
        case Kind::Pan:
            return who + ": pan " + panLabel(pan);
        case Kind::Eq:
            switch (eqKind)
            {
                case EqKind::HighPass:
                    return who + ": high-pass at " + hzLabel(freqHz);
                case EqKind::LowPass:
                    return who + ": low-pass at " + hzLabel(freqHz);
                case EqKind::Bell:
                    return who + ": EQ " + signedNumber(gainDb) + " dB at "
                         + hzLabel(freqHz);
                case EqKind::LowShelf:
                    return who + ": low shelf " + signedNumber(gainDb) + " dB below "
                         + hzLabel(freqHz);
                case EqKind::HighShelf:
                    return who + ": high shelf " + signedNumber(gainDb) + " dB above "
                         + hzLabel(freqHz);
            }
            break;
        case Kind::Compressor:
        {
            juce::String s = who + ": compressor";
            if (hasThreshold)
                s << " " << juce::String(thresholdDb, 0) << " dB";
            if (hasRatio)
                s << " " << juce::String(ratio, 1) << ":1";
            return s;
        }
        case Kind::Limiter:
        {
            juce::String s = who + ": limiter";
            if (hasInputGain)
                s << " drive " << signedNumber(inputGainDb) << " dB";
            if (hasCeiling)
                s << " ceiling " << signedNumber(ceilingDb) << " dB";
            return s;
        }
    }
    return who;
}

// ---------- masking ----------

std::vector<MaskingPair> findMasking(const std::vector<TrackInput>& tracks)
{
    std::vector<int> sourceIndex;
    std::vector<MaskInput> inputs;
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const auto& t = tracks[(size_t) i];
        if (t.isBus || t.detail == nullptr || t.detail->isSilent())
            continue;
        sourceIndex.push_back(i);
        inputs.push_back({ t.role, &t.detail->timeline.frames, &t.detail->spectrum.octaveDb,
                           &t.detail->spectrum.thirdOctaveDb });
    }

    auto pairs = rankMasking(inputs);
    for (auto& p : pairs)
    {
        p.a = sourceIndex[(size_t) p.a];
        p.b = sourceIndex[(size_t) p.b];
        p.cut = sourceIndex[(size_t) p.cut];
    }
    return pairs;
}

// ---------- issues ----------

const char* severityName(Severity severity)
{
    switch (severity)
    {
        case Severity::High: return "high";
        case Severity::Medium: return "medium";
        case Severity::Low: return "low";
    }
    return "low";
}

const char* issueKindName(IssueKind kind)
{
    switch (kind)
    {
        case IssueKind::Clipping: return "clipping";
        case IssueKind::TruePeak: return "true_peak";
        case IssueKind::Loudness: return "loudness";
        case IssueKind::Phase: return "phase";
        case IssueKind::NarrowImage: return "narrow_image";
        case IssueKind::WideLowEnd: return "wide_low_end";
        case IssueKind::ToneBalance: return "tone_balance";
        case IssueKind::OverCompressed: return "over_compressed";
        case IssueKind::TrackHot: return "track_hot";
        case IssueKind::Balance: return "balance";
        case IssueKind::Rumble: return "rumble";
        case IssueKind::Masking: return "masking";
    }
    return "other";
}

std::vector<Issue> findIssues(const MasterInput& master, const std::vector<TrackInput>& tracks,
                              const MixTargets& targets,
                              const std::vector<MaskingPair>& masking)
{
    std::vector<Issue> out;
    if (master.detail == nullptr)
        return out;

    const auto push = [&out](Severity severity, IssueKind kind, int trackId,
                             const juce::String& message, std::vector<Move> moves)
    {
        Issue issue;
        issue.severity = severity;
        issue.kind = kind;
        issue.trackId = trackId;
        issue.message = message;
        issue.moves = std::move(moves);
        out.push_back(std::move(issue));
    };

    const auto& m = master.detail->stats;
    const bool silent = master.detail->isSilent();
    const float limitCeiling = roundHalf(targets.truePeakDb - 0.5f);
    const float gentleScale = targets.gentle ? 0.5f : 1.0f;

    if (! silent && (m.clippedSamples > 0 || m.truePeakDb > targets.truePeakDb + 0.05f))
    {
        const bool clipping = m.clippedSamples > 0;
        juce::String message = "master true peak " + signedNumber(m.truePeakDb)
                             + " dBTP is above the " + signedNumber(targets.truePeakDb)
                             + " dBTP ceiling";
        if (clipping)
            message << " and " << (int) m.clippedSamples << " samples clip";
        message << ": limit the master at " << signedNumber(limitCeiling) << " dB";

        std::vector<Move> moves;
        if (targets.allowDynamics)
        {
            Move limiter;
            limiter.kind = Move::Kind::Limiter;
            limiter.trackId = -1;
            limiter.hasCeiling = true;
            limiter.ceilingDb = limitCeiling;
            moves.push_back(limiter);
        }
        else
        {
            Move gain;
            gain.kind = Move::Kind::Gain;
            gain.trackId = -1;
            gain.hasDelta = true;
            gain.deltaDb = -roundHalf(juce::jmax(0.5f, m.truePeakDb - targets.truePeakDb))
                         * gentleScale;
            moves.push_back(gain);
        }

        push(clipping || m.truePeakDb > 0.0f ? Severity::High : Severity::Medium,
             clipping ? IssueKind::Clipping : IssueKind::TruePeak, -1, message,
             std::move(moves));
    }

    const float gap = m.lufsIntegrated - targets.lufs;
    if (! silent && std::abs(gap) > 1.5f)
    {
        std::vector<Move> moves;
        const bool hasLimiter = master.limiter.present;
        const float drive = master.limiter.inputGainDb;
        const float wanted = roundHalf(std::abs(gap)) * gentleScale;

        if (gap < 0.0f)
        {
            if (targets.allowDynamics && hasLimiter)
            {
                Move limiter;
                limiter.kind = Move::Kind::Limiter;
                limiter.trackId = -1;
                limiter.hasCeiling = true;
                limiter.ceilingDb = limitCeiling;
                limiter.hasInputGain = true;
                limiter.inputGainDb = juce::jmin(12.0f, drive + wanted);
                moves.push_back(limiter);
            }
            else if (m.truePeakDb + wanted <= targets.truePeakDb)
            {
                Move gain;
                gain.kind = Move::Kind::Gain;
                gain.trackId = -1;
                gain.hasDelta = true;
                gain.deltaDb = wanted;
                moves.push_back(gain);
            }
            else if (targets.allowDynamics)
            {
                Move limiter;
                limiter.kind = Move::Kind::Limiter;
                limiter.trackId = -1;
                limiter.hasCeiling = true;
                limiter.ceilingDb = limitCeiling;
                limiter.hasInputGain = true;
                limiter.inputGainDb = juce::jmin(12.0f, wanted);
                moves.push_back(limiter);
            }
            else
            {
                Move gain;
                gain.kind = Move::Kind::Gain;
                gain.trackId = -1;
                gain.hasDelta = true;
                gain.deltaDb = wanted;
                moves.push_back(gain);
            }
        }
        else
        {
            const float excess = wanted;
            const float fromDrive = juce::jlimit(0.0f, excess, hasLimiter ? drive : 0.0f);
            if (fromDrive > 0.0f)
            {
                Move limiter;
                limiter.kind = Move::Kind::Limiter;
                limiter.trackId = -1;
                limiter.hasInputGain = true;
                limiter.inputGainDb = drive - fromDrive;
                moves.push_back(limiter);
            }
            if (excess - fromDrive >= 0.5f)
            {
                Move gain;
                gain.kind = Move::Kind::Gain;
                gain.trackId = -1;
                gain.hasDelta = true;
                gain.deltaDb = -(excess - fromDrive);
                moves.push_back(gain);
            }
        }

        push(std::abs(gap) > 4.0f ? Severity::High : Severity::Medium, IssueKind::Loudness, -1,
             "master is " + signedNumber(m.lufsIntegrated) + " LUFS, "
                 + juce::String(std::abs(gap), 1) + " LU "
                 + (gap > 0.0f ? "louder" : "quieter") + " than the "
                 + signedNumber(targets.lufs) + " LUFS target",
             std::move(moves));
    }

    if (! silent && m.stereoCorrelation < 0.0f)
    {
        push(Severity::High, IssueKind::Phase, -1,
             "master L/R correlation is " + juce::String(m.stereoCorrelation, 2)
                 + ": parts cancel in mono; check polarity and wide stereo effects",
             {});
    }

    if (! targets.masterOnly)
    {
        for (const auto& t : tracks)
        {
            if (t.detail == nullptr || t.detail->isSilent())
                continue;
            if (t.detail->stats.stereoCorrelation < -0.3f)
            {
                push(Severity::High, IssueKind::Phase, t.trackId,
                     "'" + t.name + "' has L/R correlation "
                         + juce::String(t.detail->stats.stereoCorrelation, 2)
                         + ": out of phase, it vanishes in mono",
                     {});
            }
        }
    }

    const auto stereo = master.detail->stereo;
    if (! silent && m.stereoCorrelation > 0.95f && stereo.width < 0.03f)
    {
        std::vector<Move> moves;
        float side = -1.0f;
        int spreadCount = 0;
        for (const auto& t : tracks)
        {
            if (t.isBus || ! roleSpreads(t.role) || std::abs(t.pan) >= 0.05f)
                continue;
            if (t.detail == nullptr || t.detail->isSilent())
                continue;
            if (spreadCount >= 4)
                break;
            side = -side;
            Move pan;
            pan.kind = Move::Kind::Pan;
            pan.trackId = t.trackId;
            pan.pan = -0.35f * side * (targets.gentle ? 0.6f : 1.0f);
            moves.push_back(pan);
            ++spreadCount;
        }
        if (! targets.masterOnly)
            push(Severity::Low, IssueKind::NarrowImage, -1,
                 "the mix is almost mono (width "
                     + juce::String(juce::roundToInt(stereo.width * 100.0f))
                     + "%, correlation " + juce::String(m.stereoCorrelation, 2)
                     + "): spread supporting parts with pan or stereo effects",
                 std::move(moves));
    }

    if (! silent && (stereo.lowCorrelation < 0.7f || stereo.lowSideDb > -12.0f))
    {
        std::vector<Move> moves;
        juce::StringArray names;
        for (const auto& t : tracks)
        {
            if (t.isBus || t.detail == nullptr || t.detail->isSilent())
                continue;
            const auto& d = *t.detail;
            float peak = -std::numeric_limits<float>::max();
            for (float v : d.spectrum.octaveDb)
                peak = juce::jmax(peak, v);
            const float low = juce::jmax(d.spectrum.octaveDb[1], d.spectrum.octaveDb[2]);
            if (d.stereo.lowSideDb > -12.0f && low > peak - 12.0f)
            {
                names.add(t.name);
                if (std::abs(t.pan) > 0.05f)
                {
                    Move pan;
                    pan.kind = Move::Kind::Pan;
                    pan.trackId = t.trackId;
                    pan.pan = 0.0f;
                    moves.push_back(pan);
                }
            }
        }
        if (! targets.masterOnly)
        {
            juce::String message = "the low end (below 120 Hz) is not mono (correlation "
                                 + juce::String(stereo.lowCorrelation, 2) + ", side "
                                 + juce::String(stereo.lowSideDb, 0) + " dB)";
            if (! names.isEmpty())
                message << ": wide lows in " << names.joinIntoString(", ");
            message << ": keep bass and kick centered and mono";
            push(Severity::Medium, IssueKind::WideLowEnd, -1, message, std::move(moves));
        }
    }

    if (! silent && targets.allowEq)
    {
        const auto b = m.bandEnergy;
        const auto curve = profileCurve(targets.profile);
        const struct
        {
            const char* name;
            float actual;
            float reference;
            EqKind kind;
            float freq;
            float q;
        } checks[] {
            { "low end", b.low - b.mid, curve.low, EqKind::LowShelf, 100.0f, 0.0f },
            { "low-mids", b.lowMid - b.mid, curve.lowMid, EqKind::Bell, 300.0f, 1.0f },
            { "high-mids", b.highMid - b.mid, curve.highMid, EqKind::Bell, 3500.0f, 1.0f },
            { "highs", b.high - b.mid, curve.high, EqKind::HighShelf, 8000.0f, 0.0f }
        };
        for (const auto& c : checks)
        {
            const float delta = c.actual - c.reference;
            if (std::abs(delta) <= 4.0f)
                continue;
            float gain = delta > 0.0f
                             ? -roundHalf(juce::jmin(4.0f, delta - 2.0f))
                             : roundHalf(juce::jmin(3.0f, -delta - 2.0f));
            gain *= gentleScale;
            Move eq;
            eq.kind = Move::Kind::Eq;
            eq.trackId = -1;
            eq.eqKind = c.kind;
            eq.freqHz = c.freq;
            eq.gainDb = gain;
            eq.q = c.q;
            push(std::abs(delta) > 7.0f ? Severity::Medium : Severity::Low,
                 IssueKind::ToneBalance, -1,
                 juce::String(c.name) + " " + juce::String(std::abs(delta), 1) + " dB "
                     + (delta > 0.0f ? "above" : "below") + " a typical "
                     + profileName(targets.profile) + " balance (vs the mids)",
                 { eq });
        }
    }

    if (! silent && m.crestFactorDb < 8.0f)
    {
        std::vector<Move> moves;
        if (targets.allowDynamics && master.limiter.present && master.limiter.inputGainDb > 3.0f)
        {
            Move limiter;
            limiter.kind = Move::Kind::Limiter;
            limiter.trackId = -1;
            limiter.hasInputGain = true;
            limiter.inputGainDb = master.limiter.inputGainDb - 3.0f;
            moves.push_back(limiter);
        }
        push(Severity::Low, IssueKind::OverCompressed, -1,
             "master crest factor is " + juce::String(m.crestFactorDb, 1)
                 + " dB: over-compressed or over-limited, punch is lost",
             std::move(moves));
    }

    if (! targets.masterOnly)
    {
        for (const auto& t : tracks)
        {
            if (t.detail == nullptr || t.detail->isSilent())
                continue;
            const float peak = t.detail->stats.peakDb;
            if (peak > -0.5f)
            {
                Move gain;
                gain.kind = Move::Kind::Gain;
                gain.trackId = t.trackId;
                gain.hasDelta = true;
                gain.deltaDb = -roundHalf(peak + 6.0f) * gentleScale;
                push(Severity::Medium, IssueKind::TrackHot, t.trackId,
                     "'" + t.name + "' peaks at " + signedNumber(peak)
                         + " dBFS after its fader: no headroom; gain-stage it down",
                     { gain });
            }
        }
    }

    if (! targets.masterOnly)
    {
        std::vector<const TrackInput*> sources;
        for (const auto& t : tracks)
            if (! t.isBus && t.detail != nullptr && ! t.detail->isSilent())
                sources.push_back(&t);

        if (sources.size() > 1)
        {
            std::vector<float> anchors;
            anchors.reserve(sources.size());
            for (auto* t : sources)
                anchors.push_back(t->detail->stats.lufsIntegrated - targets.balanceDb(t->role));
            std::sort(anchors.begin(), anchors.end());
            const std::size_t mid = anchors.size() / 2;
            const float anchor = anchors.size() % 2 == 0
                                     ? (anchors[mid - 1] + anchors[mid]) * 0.5f
                                     : anchors[mid];
            for (auto* t : sources)
            {
                const float expected = anchor + targets.balanceDb(t->role);
                const float dev = t->detail->stats.lufsIntegrated - expected;
                if (std::abs(dev) > 6.0f)
                {
                    Move gain;
                    gain.kind = Move::Kind::Gain;
                    gain.trackId = t->trackId;
                    gain.hasDelta = true;
                    gain.deltaDb = -roundHalf(dev * 0.75f) * gentleScale;
                    push(Severity::Medium, IssueKind::Balance, t->trackId,
                         "'" + t->name + "' (" + roleName(t->role) + ") is "
                             + juce::String(std::abs(dev), 1) + " dB "
                             + (dev > 0.0f ? "louder" : "quieter")
                             + " than its usual place in the balance",
                         { gain });
                }
            }
        }

        if (targets.allowEq)
        {
            for (auto* t : sources)
            {
                bool hasHp = false;
                const float hp = roleHighPassHz(t->role, &hasHp);
                if (! hasHp)
                    continue;
                if (t->highPassHz >= hp * 0.5f)
                    continue;
                const auto& o = t->detail->spectrum.octaveDb;
                float peak = -std::numeric_limits<float>::max();
                for (float v : o)
                    peak = juce::jmax(peak, v);
                const float lows = juce::jmax(o[0], o[1]);
                if ((lows > peak - 15.0f && hp > 100.0f) || lows > peak - 10.0f)
                {
                    Move eq;
                    eq.kind = Move::Kind::Eq;
                    eq.trackId = t->trackId;
                    eq.eqKind = EqKind::HighPass;
                    eq.freqHz = hp;
                    eq.gainDb = 0.0f;
                    eq.q = 0.0f;
                    push(Severity::Low, IssueKind::Rumble, t->trackId,
                         "'" + t->name + "' (" + roleName(t->role) + ") carries energy below 80 Hz ("
                             + juce::String(peak - lows, 0)
                             + " dB under its loudest band) that only muddies the lows: high-pass it at "
                             + hzLabel(hp),
                         { eq });
                }
            }
        }
    }

    if (! targets.masterOnly && targets.allowEq)
    {
        int count = 0;
        for (const auto& p : masking)
        {
            if (p.score < 0.25f || count >= 5)
                continue;
            if (p.a < 0 || p.a >= (int) tracks.size() || p.b < 0 || p.b >= (int) tracks.size())
                continue;
            const auto& a = tracks[(size_t) p.a];
            const auto& b = tracks[(size_t) p.b];
            const auto* cut = p.cut == p.a ? &a : &b;
            const auto* keep = p.cut == p.a ? &b : &a;
            const float gain = cutGainDb(p.score);
            const bool lowPair = p.bandHz <= 125.0f
                              && (keep->role == Role::Kick || keep->role == Role::Drums)
                              && cut->role == Role::Bass;
            Move eq;
            eq.kind = Move::Kind::Eq;
            eq.trackId = cut->trackId;
            eq.eqKind = EqKind::Bell;
            eq.freqHz = p.bandHz;
            eq.gainDb = gain * gentleScale;
            eq.q = kCutQ;
            push(p.score > 0.5f ? Severity::Medium : Severity::Low, IssueKind::Masking,
                 cut->trackId,
                 "'" + a.name + "' and '" + b.name + "' collide around " + hzLabel(p.bandHz)
                     + " " + juce::String(juce::roundToInt(p.overlap * 100.0f))
                     + "% of the time: cut '" + cut->name + "' "
                     + juce::String(juce::roundToInt(-gain)) + " dB there"
                     + (lowPair ? " (or duck it with a compressor keyed by the kick)" : ""),
                 { eq });
            ++count;
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const Issue& a, const Issue& b)
    {
        return (int) a.severity < (int) b.severity;
    });
    for (int i = 0; i < (int) out.size(); ++i)
        out[(size_t) i].id = "i" + juce::String(i + 1);
    return out;
}

// ---------- request parsing ----------

MixTargets interpretRequest(const juce::String& prompt, MixTargets targets, juce::String& note)
{
    const auto text = prompt.trim();
    if (text.isEmpty())
    {
        note = "Using the current targets.";
        return targets;
    }

    const auto lower = text.toLowerCase();
    juce::StringArray understood;

    const std::string input = lower.toStdString();
    std::smatch match;

    // "-14 LUFS", "loudness -9", "lufs -10".
    static const std::regex lufsPattern(R"(([-+]?\d+(?:\.\d+)?)\s*(?:lufs|lu)\b)",
                                       std::regex::icase);
    if (std::regex_search(input, match, lufsPattern))
    {
        targets.lufs = juce::jlimit(-40.0f, 0.0f,
                                   (float) std::atof(match[1].str().c_str()));
        targets.source = "prompt";
        understood.add("target " + juce::String(targets.lufs, 1) + " LUFS");
    }
    else
    {
        static const std::regex loudnessPattern(
            R"((?:loudness|level)\s*(?:of|at|to)?\s*([-+]?\d+(?:\.\d+)?))",
            std::regex::icase);
        if (std::regex_search(input, match, loudnessPattern))
        {
            targets.lufs = juce::jlimit(-40.0f, 0.0f,
                                       (float) std::atof(match[1].str().c_str()));
            targets.source = "prompt";
            understood.add("target " + juce::String(targets.lufs, 1) + " LUFS");
        }
    }

    static const std::regex truePeakPattern(
        R"(([-+]?\d+(?:\.\d+)?)\s*(?:dbtp|db\s*tp))", std::regex::icase);
    static const std::regex truePeakAfterPattern(
        R"((?:true\s*peak|tp)\s*([-+]?\d+(?:\.\d+)?))", std::regex::icase);
    if (std::regex_search(input, match, truePeakPattern)
        || std::regex_search(input, match, truePeakAfterPattern))
    {
        targets.truePeakDb = juce::jlimit(-12.0f, 0.0f,
                                         (float) std::atof(match[1].str().c_str()));
        targets.source = "prompt";
        understood.add("ceiling " + juce::String(targets.truePeakDb, 1) + " dBTP");
    }

    bool profileNamed = false;
    for (const auto* name : { "pop", "electronic", "hiphop", "hip-hop", "rock", "acoustic",
                              "orchestral" })
    {
        if (lower.contains(name))
        {
            targets.profile = profileFromName(name);
            profileNamed = true;
            understood.add(juce::String(profileName(targets.profile)) + " balance");
            break;
        }
    }
    if (! profileNamed)
    {
        const auto genre = profileFromGenre(lower);
        if (genre != ToneProfile::Pop || lower.contains("pop"))
        {
            targets.profile = genre;
            understood.add(juce::String(profileName(targets.profile)) + " balance");
        }
    }

    if (lower.contains("master only") || lower.contains("mastering only")
        || lower.contains("only the master") || lower.contains("master bus only")
        || lower.contains("solo master") || lower.contains("solo máster")
        || lower.contains("sólo master") || lower.contains("sólo máster")
        || lower.contains("solo el master") || lower.contains("solo el máster")
        || lower.contains("bus master") || lower.contains("bus máster"))
    {
        targets.masterOnly = true;
        understood.add("master bus only");
    }
    if (lower.contains("gentle") || lower.contains("subtle") || lower.contains("light touch")
        || lower.contains("conservative") || lower.contains("slight")
        || lower.contains("suave") || lower.contains("ligero") || lower.contains("ligera")
        || lower.contains("sutil") || lower.contains("conservador"))
    {
        targets.gentle = true;
        understood.add("gentle moves");
    }
    if (lower.contains("no eq") || lower.contains("no equalizer")
        || lower.contains("no equalisation") || lower.contains("no equalization")
        || lower.contains("without eq") || lower.contains("sin eq")
        || lower.contains("sin ecualización") || lower.contains("sin ecualizacion")
        || lower.contains("sin ecualizar") || lower.contains("no ecualización")
        || lower.contains("no ecualizacion"))
    {
        targets.allowEq = false;
        understood.add("no EQ");
    }
    if (lower.contains("no compression") || lower.contains("no compressor")
        || lower.contains("no limiting") || lower.contains("no limiter")
        || lower.contains("no dynamics") || lower.contains("without compression")
        || lower.contains("sin compresión") || lower.contains("sin compresion")
        || lower.contains("sin compresor") || lower.contains("sin limitador")
        || lower.contains("sin dinámica") || lower.contains("sin dinamica")
        || lower.contains("no dinámica") || lower.contains("no dinamica"))
    {
        targets.allowDynamics = false;
        understood.add("no dynamics");
    }

    if (understood.isEmpty())
        note = "Using the current targets.";
    else
        note = "Understood: " + understood.joinIntoString(", ") + ".";
    return targets;
}

// ---------- JSON ----------

namespace {

juce::var detailToVar(const Detail& detail)
{
    auto* o = new juce::DynamicObject();
    const auto& s = detail.stats;
    auto* stats = new juce::DynamicObject();
    stats->setProperty("peak_db", s.peakDb);
    stats->setProperty("true_peak_db", s.truePeakDb);
    stats->setProperty("rms_db", s.rmsDb);
    stats->setProperty("lufs_integrated", s.lufsIntegrated);
    stats->setProperty("lufs_short_term_max", s.lufsShortTermMax);
    stats->setProperty("crest_factor_db", s.crestFactorDb);
    stats->setProperty("clipped_samples", (juce::int64) s.clippedSamples);
    auto* bands = new juce::DynamicObject();
    bands->setProperty("low", s.bandEnergy.low);
    bands->setProperty("low_mid", s.bandEnergy.lowMid);
    bands->setProperty("mid", s.bandEnergy.mid);
    bands->setProperty("high_mid", s.bandEnergy.highMid);
    bands->setProperty("high", s.bandEnergy.high);
    stats->setProperty("band_energy_db", juce::var(bands));
    stats->setProperty("stereo_correlation", s.stereoCorrelation);
    o->setProperty("stats", juce::var(stats));

    auto* spectrum = new juce::DynamicObject();
    juce::Array<juce::var> third, side, octaves, shape;
    for (float v : detail.spectrum.thirdOctaveDb)
        third.add(v);
    for (float v : detail.spectrum.sideThirdOctaveDb)
        side.add(v);
    for (float v : detail.spectrum.octaveDb)
        octaves.add(v);
    for (float v : detail.spectrum.octaveShape())
        shape.add(v);
    spectrum->setProperty("third_octave_db", third);
    spectrum->setProperty("side_third_octave_db", side);
    spectrum->setProperty("octave_db", octaves);
    spectrum->setProperty("octave_shape", shape);
    o->setProperty("spectrum", juce::var(spectrum));

    auto* stereo = new juce::DynamicObject();
    stereo->setProperty("width", detail.stereo.width);
    stereo->setProperty("low_correlation", detail.stereo.lowCorrelation);
    stereo->setProperty("low_side_db", detail.stereo.lowSideDb);
    o->setProperty("stereo", juce::var(stereo));

    auto* timeline = new juce::DynamicObject();
    juce::Array<juce::var> frames;
    const auto totalFrames = detail.timeline.frames.size();
    const int stride = totalFrames > 240
                           ? (int) std::ceil((double) totalFrames / 240.0)
                           : 1;
    for (std::size_t i = 0; i < totalFrames; i += (std::size_t) stride)
    {
        juce::Array<juce::var> row;
        for (float v : detail.timeline.frames[i])
            row.add(v);
        frames.add(row);
    }
    timeline->setProperty("hop_seconds", detail.timeline.hopSeconds * stride);
    timeline->setProperty("frames", frames);
    o->setProperty("timeline", juce::var(timeline));
    return juce::var(o);
}

juce::var issueToVarImpl(const Issue& issue)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", issue.id);
    o->setProperty("severity", severityName(issue.severity));
    o->setProperty("kind", issueKindName(issue.kind));
    if (issue.trackId >= 0)
        o->setProperty("track_id", issue.trackId);
    o->setProperty("message", issue.message);
    juce::Array<juce::var> moves;
    for (const auto& move : issue.moves)
        moves.add(moveToVar(move));
    o->setProperty("moves", moves);
    return juce::var(o);
}

juce::String moveName(const juce::DynamicObject& o)
{
    return o.getProperty("move").toString().toLowerCase();
}

bool number(const juce::DynamicObject& o, const char* key, float& value, bool* present = nullptr)
{
    if (! o.hasProperty(key))
    {
        if (present != nullptr)
            *present = false;
        return false;
    }
    const auto v = o.getProperty(key);
    if (! v.isDouble() && ! v.isInt() && ! v.isInt64())
        return false;
    value = (float) (double) v;
    if (present != nullptr)
        *present = true;
    return true;
}

int trackFrom(const juce::DynamicObject& o)
{
    float value = -1.0f;
    return number(o, "track_id", value) ? (int) value : -1;
}

} // namespace

juce::var moveToVar(const Move& move)
{
    auto* o = new juce::DynamicObject();
    if (move.trackId >= 0)
        o->setProperty("track_id", move.trackId);
    switch (move.kind)
    {
        case Move::Kind::Gain:
            o->setProperty("move", "gain");
            if (move.hasDelta)
                o->setProperty("delta_db", move.deltaDb);
            if (move.hasDb)
                o->setProperty("db", move.db);
            break;
        case Move::Kind::Pan:
            o->setProperty("move", "pan");
            o->setProperty("pan", move.pan);
            break;
        case Move::Kind::Eq:
            o->setProperty("move", "eq");
            o->setProperty("kind", eqKindName(move.eqKind));
            o->setProperty("freq_hz", move.freqHz);
            o->setProperty("gain_db", move.gainDb);
            if (move.eqKind == EqKind::Bell)
                o->setProperty("q", move.q);
            break;
        case Move::Kind::Compressor:
            o->setProperty("move", "compressor");
            if (move.hasThreshold) o->setProperty("threshold_db", move.thresholdDb);
            if (move.hasRatio) o->setProperty("ratio", move.ratio);
            if (move.hasAttack) o->setProperty("attack_ms", move.attackMs);
            if (move.hasRelease) o->setProperty("release_ms", move.releaseMs);
            if (move.hasMakeup) o->setProperty("makeup_db", move.makeupDb);
            if (move.hasKnee) o->setProperty("knee_db", move.kneeDb);
            break;
        case Move::Kind::Limiter:
            o->setProperty("move", "limiter");
            if (move.hasCeiling) o->setProperty("ceiling_db", move.ceilingDb);
            if (move.hasInputGain) o->setProperty("input_gain_db", move.inputGainDb);
            break;
    }
    return juce::var(o);
}

bool moveFromVar(const juce::var& value, Move& out, juce::String& error)
{
    auto* o = value.getDynamicObject();
    if (o == nullptr)
    {
        error = "move: expected an object";
        return false;
    }

    out = Move {};
    out.trackId = trackFrom(*o);
    const auto name = moveName(*o);

    if (name == "gain")
    {
        out.kind = Move::Kind::Gain;
        out.hasDelta = number(*o, "delta_db", out.deltaDb);
        out.hasDb = number(*o, "db", out.db);
        if (! out.hasDelta && ! out.hasDb)
        {
            error = "gain move needs delta_db or db";
            return false;
        }
    }
    else if (name == "pan")
    {
        out.kind = Move::Kind::Pan;
        if (! number(*o, "pan", out.pan))
        {
            error = "pan move needs pan";
            return false;
        }
        out.pan = juce::jlimit(-1.0f, 1.0f, out.pan);
    }
    else if (name == "eq")
    {
        out.kind = Move::Kind::Eq;
        out.eqKind = eqKindFromName(o->getProperty("kind").toString());
        if (! number(*o, "freq_hz", out.freqHz) || out.freqHz <= 0.0f)
        {
            error = "eq move needs freq_hz";
            return false;
        }
        number(*o, "gain_db", out.gainDb);
        if (out.eqKind == EqKind::Bell)
            number(*o, "q", out.q, nullptr);
        if (out.q <= 0.0f)
            out.q = 1.0f;
    }
    else if (name == "compressor")
    {
        out.kind = Move::Kind::Compressor;
        out.hasThreshold = number(*o, "threshold_db", out.thresholdDb);
        out.hasRatio = number(*o, "ratio", out.ratio);
        out.hasAttack = number(*o, "attack_ms", out.attackMs);
        out.hasRelease = number(*o, "release_ms", out.releaseMs);
        out.hasMakeup = number(*o, "makeup_db", out.makeupDb);
        out.hasKnee = number(*o, "knee_db", out.kneeDb);
    }
    else if (name == "limiter")
    {
        out.kind = Move::Kind::Limiter;
        out.hasCeiling = number(*o, "ceiling_db", out.ceilingDb);
        out.hasInputGain = number(*o, "input_gain_db", out.inputGainDb);
    }
    else
    {
        error = "unknown move '" + name + "'";
        return false;
    }
    return true;
}

bool movesFromVar(const juce::var& value, std::vector<Move>& out, juce::String& error)
{
    auto* array = value.getArray();
    if (array == nullptr)
    {
        error = "moves: expected an array";
        return false;
    }
    out.clear();
    for (const auto& item : *array)
    {
        Move move;
        if (! moveFromVar(item, move, error))
            return false;
        out.push_back(std::move(move));
    }
    return true;
}

juce::var issueToVar(const Issue& issue)
{
    return issueToVarImpl(issue);
}

juce::var reportToVar(const Report& report)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("ok", report.ok);
    if (report.error.isNotEmpty())
        o->setProperty("error", report.error);
    if (report.promptNote.isNotEmpty())
        o->setProperty("prompt_note", report.promptNote);

    auto* targets = new juce::DynamicObject();
    targets->setProperty("lufs", report.targets.lufs);
    targets->setProperty("true_peak_db", report.targets.truePeakDb);
    targets->setProperty("profile", profileName(report.targets.profile));
    targets->setProperty("source", report.targets.source);
    targets->setProperty("master_only", report.targets.masterOnly);
    targets->setProperty("allow_eq", report.targets.allowEq);
    targets->setProperty("allow_dynamics", report.targets.allowDynamics);
    targets->setProperty("gentle", report.targets.gentle);
    juce::Array<juce::var> roles;
    for (const auto& [role, db] : report.targets.roles)
    {
        auto* r = new juce::DynamicObject();
        r->setProperty("role", roleName(role));
        r->setProperty("balance_db", db);
        roles.add(juce::var(r));
    }
    targets->setProperty("roles", roles);
    o->setProperty("targets", juce::var(targets));
    o->setProperty("master", detailToVar(report.master));

    juce::Array<juce::var> channels;
    for (const auto& channel : report.channels)
    {
        auto* c = new juce::DynamicObject();
        c->setProperty("track_id", channel.trackId);
        c->setProperty("name", channel.name);
        c->setProperty("role", roleName(channel.role));
        c->setProperty("fader_db", channel.faderDb);
        c->setProperty("pan", channel.pan);
        c->setProperty("high_pass_active", channel.highPassActive);
        c->setProperty("detail", detailToVar(channel.detail));
        channels.add(juce::var(c));
    }
    o->setProperty("channels", channels);

    juce::Array<juce::var> masking;
    for (const auto& p : report.masking)
    {
        auto* m = new juce::DynamicObject();
        m->setProperty("a", p.a);
        m->setProperty("b", p.b);
        m->setProperty("band_hz", p.bandHz);
        m->setProperty("overlap", p.overlap);
        m->setProperty("level_diff_db", p.levelDiffDb);
        m->setProperty("score", p.score);
        m->setProperty("cut", p.cut);
        masking.add(juce::var(m));
    }
    o->setProperty("masking", masking);

    juce::Array<juce::var> issues;
    for (const auto& issue : report.issues)
        issues.add(issueToVar(issue));
    o->setProperty("issues", issues);

    juce::Array<juce::var> plan;
    for (const auto& stage : report.plan)
    {
        auto* s = new juce::DynamicObject();
        s->setProperty("id", stage.id);
        s->setProperty("label", stage.label);
        juce::Array<juce::var> moves;
        for (const auto& move : stage.moves)
            moves.add(moveToVar(move));
        s->setProperty("moves", moves);
        plan.add(juce::var(s));
    }
    o->setProperty("plan", plan);
    o->setProperty("duration_seconds", report.durationSeconds);

    juce::Array<juce::var> warnings;
    for (const auto& warning : report.warnings)
        warnings.add(warning);
    o->setProperty("warnings", warnings);
    return juce::var(o);
}

std::vector<PlanStage> buildPlan(const std::vector<Issue>& issues)
{
    PlanStage balance { "balance", "Balance" };
    PlanStage eq { "eq", "EQ" };
    PlanStage dynamics { "dynamics", "Dynamics" };
    PlanStage master { "master", "Master" };

    for (const auto& issue : issues)
    {
        for (const auto& move : issue.moves)
        {
            if (move.trackId < 0)
                master.moves.push_back(move);
            else if (move.kind == Move::Kind::Gain || move.kind == Move::Kind::Pan)
                balance.moves.push_back(move);
            else if (move.kind == Move::Kind::Eq)
                eq.moves.push_back(move);
            else
                dynamics.moves.push_back(move);
        }
    }

    std::vector<PlanStage> plan;
    for (auto* stage : { &balance, &eq, &dynamics, &master })
        if (! stage->moves.empty())
            plan.push_back(std::move(*stage));
    return plan;
}

} // namespace cadence::mixai

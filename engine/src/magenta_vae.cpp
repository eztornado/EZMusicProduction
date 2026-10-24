#include "magenta_vae.h"

#include <Accelerate/Accelerate.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace cadence::magenta
{
namespace
{
constexpr int kStepsPerQuarter = 4;
constexpr int kStepsPerBar = 16;

// Magenta's DEFAULT_DRUM_PITCH_CLASSES (core/constants.js). One class per
// instrument family in the 10-class encoder representation.
const int kDrumPitchClasses[9][1] = {
    { 36 }, { 38 }, { 42 }, { 46 }, { 45 },
    { 48 }, { 50 }, { 49 }, { 51 }
};

constexpr int kNumDrumClasses = 9;

int drumClassForPitch(int pitch)
{
    for (int c = 0; c < kNumDrumClasses; ++c)
        if (pitch == kDrumPitchClasses[c][0])
            return c;

    // Friendly aliases for the most common GM notes (open/closed hats,
    // ride, crash, toms) so transcribed or imported drums still encode.
    switch (pitch)
    {
        case 35: case 36: return 0;
        case 27: case 28: case 31: case 32: case 33: case 34:
        case 37: case 39: case 40: case 56: case 65: case 66:
        case 75: case 85: return 1;
        case 44: case 54: case 68: case 69: case 70: case 71:
        case 73: case 78: case 80: return 2;
        case 67: case 72: case 74: case 79: case 81: return 3;
        case 29: case 41: case 61: case 64: case 84: return 4;
        case 47: case 60: case 63: case 77: case 86: case 87: return 5;
        case 30: case 43: case 62: case 76: case 83: return 6;
        case 55: case 57: case 58: return 7;
        case 52: case 53: case 59: case 82: return 8;
        default: return -1;
    }
}

int pitchClassForDrumClass(int c)
{
    return kDrumPitchClasses[juce::jlimit(0, kNumDrumClasses - 1, c)][0];
}

std::size_t shapeProduct(const std::vector<int>& shape)
{
    std::size_t n = 1;
    for (int d : shape)
        n *= (std::size_t) juce::jmax(0, d);
    return n;
}
} // namespace

MusicVAE::MusicVAE() = default;
MusicVAE::~MusicVAE() = default;

bool MusicVAE::load(const juce::File& checkpointDir, juce::String& error)
{
    loaded_ = false;
    shards_.clear();
    vars_.clear();
    decoderKernels_.clear();
    decoderBiases_.clear();

    if (! checkpointDir.isDirectory())
    {
        error = "model folder not found: " + checkpointDir.getFullPathName();
        return false;
    }

    if (! loadConfig(checkpointDir, error))
        return false;
    if (! loadWeights(checkpointDir, error))
        return false;

    if (! require("encoder/cell_0/bidirectional_rnn/fw/multi_rnn_cell/cell_0/lstm_cell/kernel",
                  encoderFwKernel_, error)
        || ! require("encoder/cell_0/bidirectional_rnn/fw/multi_rnn_cell/cell_0/lstm_cell/bias",
                     encoderFwBias_, error)
        || ! require("encoder/cell_0/bidirectional_rnn/bw/multi_rnn_cell/cell_0/lstm_cell/kernel",
                     encoderBwKernel_, error)
        || ! require("encoder/cell_0/bidirectional_rnn/bw/multi_rnn_cell/cell_0/lstm_cell/bias",
                     encoderBwBias_, error)
        || ! require("encoder/mu/kernel", encoderMuKernel_, error)
        || ! require("encoder/mu/bias", encoderMuBias_, error)
        || ! require("decoder/z_to_initial_state/kernel", decoderInitKernel_, error)
        || ! require("decoder/z_to_initial_state/bias", decoderInitBias_, error)
        || ! require("decoder/output_projection/kernel", outputKernel_, error)
        || ! require("decoder/output_projection/bias", outputBias_, error))
        return false;

    hidden_ = encoderFwBias_.size / 4;
    if (hidden_ <= 0 || encoderFwKernel_.rows() != inputDepth_ + hidden_)
    {
        error = "unsupported encoder shape (expected input + hidden rows)";
        return false;
    }

    zDims_ = encoderMuBias_.size;
    if (zDims_ <= 0 || encoderMuKernel_.rows() != 2 * hidden_
        || encoderMuKernel_.cols() != zDims_)
    {
        error = "unsupported latent shape";
        return false;
    }

    // Decoder layers: cell_0, cell_1, ... until the manifest runs out.
    for (int layer = 0; layer < 8; ++layer)
    {
        const auto prefix = "decoder/multi_rnn_cell/cell_" + std::to_string(layer)
                          + "/lstm_cell/";
        Tensor k, b;
        if (! require((prefix + "kernel").c_str(), k, error))
        {
            if (layer == 0) return false;
            break;
        }
        if (! require((prefix + "bias").c_str(), b, error))
            return false;
        decoderKernels_.push_back(k);
        decoderBiases_.push_back(b);
    }

    if (decoderKernels_.empty())
    {
        error = "no decoder LSTM layers found";
        return false;
    }

    if (decoderInitKernel_.rows() != zDims_
        || decoderInitKernel_.cols() != (int) decoderKernels_.size() * 2 * hidden_
        || decoderInitBias_.size != decoderInitKernel_.cols())
    {
        error = "unsupported decoder initial-state shape";
        return false;
    }

    if (outputKernel_.rows() != hidden_ || outputKernel_.cols() != outputDepth_
        || outputBias_.size != (std::size_t) outputDepth_)
    {
        error = "unsupported output projection shape";
        return false;
    }

    loaded_ = true;
    return true;
}

bool MusicVAE::loadConfig(const juce::File& dir, juce::String& error)
{
    const auto file = dir.getChildFile("config.json");
    if (! file.existsAsFile())
    {
        error = "config.json missing in " + dir.getFullPathName();
        return false;
    }

    const auto parsed = juce::JSON::parse(file);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr || root->getProperty("type").toString() != "MusicVAE")
    {
        error = "not a MusicVAE checkpoint";
        return false;
    }

    auto converter = root->getProperty("dataConverter");
    auto* c = converter.getDynamicObject();
    if (c == nullptr)
    {
        error = "checkpoint has no dataConverter";
        return false;
    }

    const auto type = c->getProperty("type").toString();
    auto args = c->getProperty("args");
    auto* a = args.getDynamicObject();

    numSteps_ = a != nullptr ? (int) a->getProperty("numSteps") : 64;
    if (numSteps_ <= 0)
        numSteps_ = 64;

    if (type == "MelodyConverter")
    {
        kind_ = Kind::Melody;
        displayName_ = "melody";
        minPitch_ = a != nullptr ? (int) a->getProperty("minPitch") : 21;
        maxPitch_ = a != nullptr ? (int) a->getProperty("maxPitch") : 108;
        if (maxPitch_ <= minPitch_)
        {
            error = "invalid melody pitch range";
            return false;
        }
        inputDepth_ = maxPitch_ - minPitch_ + 1 + 2;
        outputDepth_ = inputDepth_;
    }
    else if (type == "DrumsConverter" || type == "DrumsOneHotConverter")
    {
        kind_ = Kind::Drums;
        displayName_ = "drums";
        // The Magenta Studio 4-bar drum checkpoint uses the 10-class
        // DrumsConverter input with a 512-class (9-bit) output. The output
        // depth is taken from the checkpoint below, so this stays correct
        // for both converter variants.
        inputDepth_ = kNumDrumClasses + 1;
        outputDepth_ = 1 << kNumDrumClasses;
    }
    else
    {
        error = "unsupported dataConverter: " + type;
        return false;
    }

    return true;
}

bool MusicVAE::loadWeights(const juce::File& dir, juce::String& error)
{
    const auto manifestFile = dir.getChildFile("weights_manifest.json");
    if (! manifestFile.existsAsFile())
    {
        error = "weights_manifest.json missing in " + dir.getFullPathName();
        return false;
    }

    const auto parsed = juce::JSON::parse(manifestFile);
    if (! parsed.isArray())
    {
        error = "invalid weights manifest";
        return false;
    }

    for (const auto& shardVar : *parsed.getArray())
    {
        auto* shard = shardVar.getDynamicObject();
        if (shard == nullptr)
            continue;

        std::vector<float> data;
        auto paths = shard->getProperty("paths");
        if (paths.isArray())
        {
            for (const auto& pathVar : *paths.getArray())
            {
                const auto f = dir.getChildFile(pathVar.toString());
                if (! f.existsAsFile())
                {
                    error = "weight shard missing: " + f.getFullPathName();
                    return false;
                }

                juce::FileInputStream in(f);
                if (! in.openedOk())
                {
                    error = "cannot open weight shard: " + f.getFullPathName();
                    return false;
                }

                const auto bytes = in.getTotalLength();
                if (bytes % (juce::int64) sizeof(float) != 0)
                {
                    error = "weight shard is not float32: " + f.getFullPathName();
                    return false;
                }

                const auto first = data.size();
                data.resize(first + (std::size_t) (bytes / (juce::int64) sizeof(float)));
                if (in.read(data.data() + first, (int) bytes) != (int) bytes)
                {
                    error = "short read on weight shard: " + f.getFullPathName();
                    return false;
                }
            }
        }

        auto weights = shard->getProperty("weights");
        if (! weights.isArray())
            continue;

        std::size_t offset = 0;
        for (const auto& wVar : *weights.getArray())
        {
            auto* w = wVar.getDynamicObject();
            if (w == nullptr)
                continue;

            Tensor t;
            auto shapeVar = w->getProperty("shape");
            if (shapeVar.isArray())
                for (const auto& d : *shapeVar.getArray())
                    t.shape.push_back((int) d);
            t.size = shapeProduct(t.shape);
            t.data = data.data() + offset;
            offset += t.size;

            if (offset > data.size())
            {
                error = "weight manifest exceeds shard size";
                return false;
            }

            const auto name = w->getProperty("name").toString().toStdString();
            vars_[name] = t;
        }

        shards_.push_back(std::move(data));
    }

    // Re-point every tensor at the final shard buffers. (Pointers into a
    // vector's heap block survive the outer vector reallocating; this is
    // just defensive for clarity.)
    std::size_t shardIndex = 0;
    std::size_t cursor = 0;
    for (const auto& shardVar : *parsed.getArray())
    {
        auto* shard = shardVar.getDynamicObject();
        if (shard == nullptr)
            continue;
        auto weights = shard->getProperty("weights");
        if (! weights.isArray())
            continue;

        auto& buffer = shards_[shardIndex++];
        cursor = 0;
        for (const auto& wVar : *weights.getArray())
        {
            auto* w = wVar.getDynamicObject();
            if (w == nullptr)
                continue;
            const auto name = w->getProperty("name").toString().toStdString();
            auto it = vars_.find(name);
            if (it != vars_.end())
                it->second.data = buffer.data() + cursor;
            cursor += shapeProduct(it != vars_.end() ? it->second.shape
                                                     : std::vector<int>{});
        }
    }

    return true;
}

const MusicVAE::Tensor* MusicVAE::find(const char* name) const
{
    auto it = vars_.find(name);
    return it == vars_.end() ? nullptr : &it->second;
}

bool MusicVAE::require(const char* name, Tensor& out, juce::String& error) const
{
    auto* t = find(name);
    if (t == nullptr || t->data == nullptr || t->shape.empty())
    {
        error = juce::String("checkpoint tensor missing: ") + name;
        return false;
    }
    out = *t;
    return true;
}

void MusicVAE::matvec(const Tensor& a, const float* x, std::vector<float>& y)
{
    // TensorFlow's dense layer is input * kernel, so the stored shape is
    // [inputDim, outputDim] and the product is A^T * x.
    const int inDim = a.rows();
    const int outDim = a.cols();
    y.resize((std::size_t) juce::jmax(0, outDim));
    if (inDim == 0 || outDim == 0)
        return;
    cblas_sgemv(CblasRowMajor, CblasTrans, inDim, outDim, 1.0f, a.data,
                outDim, x, 1, 0.0f, y.data(), 1);
}

float MusicVAE::sigmoid(float v) noexcept
{
    return 1.0f / (1.0f + std::exp(-v));
}

float MusicVAE::randomNormal(std::mt19937& rng)
{
    static thread_local std::normal_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}

int MusicVAE::sampleCategorical(const std::vector<float>& logits,
                                float temperature, std::mt19937& rng)
{
    if (logits.empty())
        return 0;

    if (temperature <= 1.0e-4f)
    {
        return (int) std::distance(logits.begin(),
                                   std::max_element(logits.begin(), logits.end()));
    }

    const float inv = 1.0f / temperature;
    float maxLogit = -std::numeric_limits<float>::infinity();
    for (float v : logits)
        maxLogit = juce::jmax(maxLogit, v * inv);

    double sum = 0.0;
    std::vector<double> probs(logits.size());
    for (std::size_t i = 0; i < logits.size(); ++i)
    {
        probs[i] = std::exp((double) (logits[i] * inv - maxLogit));
        sum += probs[i];
    }

    std::uniform_real_distribution<double> dist(0.0, sum);
    double r = dist(rng);
    double acc = 0.0;
    for (std::size_t i = 0; i < probs.size(); ++i)
    {
        acc += probs[i];
        if (r <= acc)
            return (int) i;
    }
    return (int) probs.size() - 1;
}

void MusicVAE::lstmStep(const Tensor& kernel, const Tensor& bias,
                        const float* x, int inputDim,
                        std::vector<float>& h, std::vector<float>& c,
                        std::vector<float>& work) const
{
    const int rows = inputDim + hidden_;
    const int gatesDim = 4 * hidden_;
    if (rows != kernel.rows() || gatesDim != kernel.cols())
        return;

    // tf.basicLSTMCell concatenates [x, h] and applies one kernel. The gate
    // order in TensorFlow is i, j (cell), f, o.
    work.resize((std::size_t) rows);
    std::memcpy(work.data(), x, (std::size_t) inputDim * sizeof(float));
    std::memcpy(work.data() + inputDim, h.data(),
                (std::size_t) hidden_ * sizeof(float));

    std::vector<float> gates((std::size_t) gatesDim);
    cblas_sgemv(CblasRowMajor, CblasTrans, rows, gatesDim, 1.0f,
                kernel.data, gatesDim, work.data(), 1, 0.0f, gates.data(), 1);

    for (int i = 0; i < gatesDim; ++i)
        gates[(std::size_t) i] += bias.data[i];

    for (int i = 0; i < hidden_; ++i)
    {
        const float inGate = sigmoid(gates[(std::size_t) i]);
        const float cellGate = std::tanh(gates[(std::size_t) (hidden_ + i)]);
        const float forgetGate = sigmoid(gates[(std::size_t) (2 * hidden_ + i)] + 1.0f);
        const float outGate = sigmoid(gates[(std::size_t) (3 * hidden_ + i)]);
        c[(std::size_t) i] = c[(std::size_t) i] * forgetGate + inGate * cellGate;
        h[(std::size_t) i] = outGate * std::tanh(c[(std::size_t) i]);
    }
}

void MusicVAE::buildInput(const Sequence& sequence, std::vector<float>& input) const
{
    const int n = juce::jmax(1, numSteps_);
    input.assign((std::size_t) n * (std::size_t) inputDepth_, 0.0f);

    if (kind_ == Kind::Melody)
    {
        std::vector<int> events((std::size_t) n, 0);
        auto notes = sequence.notes;
        std::sort(notes.begin(), notes.end(), [] (const Note& a, const Note& b) {
            if (a.startStep != b.startStep)
                return a.startStep < b.startStep;
            return a.pitch > b.pitch;
        });

        int lastStart = -1;
        for (auto note : notes)
        {
            int pitch = note.pitch;
            while (pitch < minPitch_ && pitch + 12 <= maxPitch_) pitch += 12;
            while (pitch > maxPitch_ && pitch - 12 >= minPitch_) pitch -= 12;
            if (pitch < minPitch_ || pitch > maxPitch_)
                continue;

            int start = (int) std::lround(note.startStep);
            int end = (int) std::lround(note.startStep + juce::jmax(1.0, note.lengthSteps));
            if (start < 0 || start >= n || start == lastStart)
                continue;
            end = juce::jlimit(start + 1, n, end);

            events[(std::size_t) start] = pitch - minPitch_ + 2;
            if (end < n)
                events[(std::size_t) end] = 1; // NOTE_OFF
            lastStart = start;
        }

        for (int s = 0; s < n; ++s)
        {
            const int label = juce::jlimit(0, inputDepth_ - 1, events[(std::size_t) s]);
            input[(std::size_t) s * inputDepth_ + label] = 1.0f;
        }
        return;
    }

    // Drums: 10-class input (0 = silence, 1..9 = instrument class).
    std::vector<int> labels((std::size_t) n, 0);
    for (const auto& note : sequence.notes)
    {
        const int start = (int) std::lround(note.startStep);
        if (start < 0 || start >= n)
            continue;
        const int c = drumClassForPitch(note.pitch);
        if (c >= 0)
            labels[(std::size_t) start] = c + 1;
    }

    for (int s = 0; s < n; ++s)
    {
        const int label = juce::jlimit(0, inputDepth_ - 1, labels[(std::size_t) s]);
        input[(std::size_t) s * inputDepth_ + label] = 1.0f;
    }
}

std::vector<float> MusicVAE::encode(const Sequence& inputSeq) const
{
    if (! loaded_)
        return {};

    const int n = juce::jmax(1, numSteps_);
    std::vector<float> input;
    buildInput(inputSeq, input);

    std::vector<float> fwH((std::size_t) hidden_, 0.0f), fwC((std::size_t) hidden_, 0.0f);
    std::vector<float> bwH((std::size_t) hidden_, 0.0f), bwC((std::size_t) hidden_, 0.0f);
    std::vector<float> work;
    std::vector<float> row((std::size_t) inputDepth_);

    for (int s = 0; s < n; ++s)
    {
        std::memcpy(row.data(), input.data() + (std::size_t) s * inputDepth_,
                    (std::size_t) inputDepth_ * sizeof(float));
        lstmStep(encoderFwKernel_, encoderFwBias_, row.data(), inputDepth_,
                 fwH, fwC, work);
    }

    for (int s = n - 1; s >= 0; --s)
    {
        std::memcpy(row.data(), input.data() + (std::size_t) s * inputDepth_,
                    (std::size_t) inputDepth_ * sizeof(float));
        lstmStep(encoderBwKernel_, encoderBwBias_, row.data(), inputDepth_,
                 bwH, bwC, work);
    }

    std::vector<float> finalState((std::size_t) (2 * hidden_));
    std::memcpy(finalState.data(), fwH.data(), (std::size_t) hidden_ * sizeof(float));
    std::memcpy(finalState.data() + hidden_, bwH.data(),
                (std::size_t) hidden_ * sizeof(float));

    std::vector<float> z;
    matvec(encoderMuKernel_, finalState.data(), z);
    for (std::size_t i = 0; i < z.size() && i < encoderMuBias_.size; ++i)
        z[i] += encoderMuBias_.data[i];
    return z;
}

Sequence MusicVAE::outputToSequence(const std::vector<int>& labels) const
{
    Sequence out;
    out.stepsPerQuarter = kStepsPerQuarter;
    out.numSteps = (int) labels.size();

    if (kind_ == Kind::Drums)
    {
        for (std::size_t s = 0; s < labels.size(); ++s)
        {
            const int label = labels[s];
            for (int c = 0; c < kNumDrumClasses; ++c)
            {
                if ((label & (1 << c)) != 0)
                {
                    Note note;
                    note.pitch = pitchClassForDrumClass(c);
                    note.startStep = (double) s;
                    note.lengthSteps = 1.0;
                    note.velocity = 0.8f;
                    note.isDrum = true;
                    out.notes.push_back(note);
                }
            }
        }
        return out;
    }

    int currentPitch = -1;
    int currentStart = 0;
    for (std::size_t s = 0; s < labels.size(); ++s)
    {
        const int label = labels[s];
        if (label == 1)
        {
            if (currentPitch >= 0)
            {
                Note note;
                note.pitch = currentPitch;
                note.startStep = currentStart;
                note.lengthSteps = juce::jmax(1.0, (double) s - currentStart);
                note.velocity = 0.8f;
                out.notes.push_back(note);
                currentPitch = -1;
            }
        }
        else if (label >= 2)
        {
            if (currentPitch >= 0)
            {
                Note note;
                note.pitch = currentPitch;
                note.startStep = currentStart;
                note.lengthSteps = juce::jmax(1.0, (double) s - currentStart);
                note.velocity = 0.8f;
                out.notes.push_back(note);
            }
            currentPitch = label - 2 + minPitch_;
            currentStart = (int) s;
        }
    }

    if (currentPitch >= 0)
    {
        Note note;
        note.pitch = currentPitch;
        note.startStep = currentStart;
        note.lengthSteps = juce::jmax(1.0, (double) labels.size() - currentStart);
        note.velocity = 0.8f;
        out.notes.push_back(note);
    }
    return out;
}

Sequence MusicVAE::decode(const std::vector<float>& z, double temperature,
                          std::uint32_t seed) const
{
    if (! loaded_ || (int) z.size() != zDims_)
        return {};

    const int layers = (int) decoderKernels_.size();
    const int n = juce::jmax(1, numSteps_);
    std::mt19937 rng(seed);

    // z -> initial (c, h) for every layer, then tanh.
    std::vector<float> init;
    matvec(decoderInitKernel_, z.data(), init);
    for (std::size_t i = 0; i < init.size() && i < decoderInitBias_.size; ++i)
        init[i] = std::tanh(init[i] + decoderInitBias_.data[i]);

    std::vector<std::vector<float>> c((std::size_t) layers,
                                      std::vector<float>((std::size_t) hidden_));
    std::vector<std::vector<float>> h((std::size_t) layers,
                                      std::vector<float>((std::size_t) hidden_));
    for (int l = 0; l < layers; ++l)
    {
        const std::size_t offC = (std::size_t) (2 * l) * hidden_;
        const std::size_t offH = (std::size_t) (2 * l + 1) * hidden_;
        std::memcpy(c[(std::size_t) l].data(), init.data() + offC,
                    (std::size_t) hidden_ * sizeof(float));
        std::memcpy(h[(std::size_t) l].data(), init.data() + offH,
                    (std::size_t) hidden_ * sizeof(float));
    }

    std::vector<float> prev((std::size_t) outputDepth_, 0.0f);
    std::vector<float> layerInput;
    std::vector<float> work;
    std::vector<float> logits;
    std::vector<int> labels;
    labels.reserve((std::size_t) n);

    for (int step = 0; step < n; ++step)
    {
        for (int l = 0; l < layers; ++l)
        {
            if (l == 0)
            {
                layerInput.resize((std::size_t) outputDepth_ + zDims_);
                std::memcpy(layerInput.data(), prev.data(),
                            (std::size_t) outputDepth_ * sizeof(float));
                std::memcpy(layerInput.data() + outputDepth_, z.data(),
                            (std::size_t) zDims_ * sizeof(float));
                lstmStep(decoderKernels_[0], decoderBiases_[0], layerInput.data(),
                         outputDepth_ + zDims_, h[0], c[0], work);
            }
            else
            {
                lstmStep(decoderKernels_[(std::size_t) l],
                         decoderBiases_[(std::size_t) l],
                         h[(std::size_t) (l - 1)].data(), hidden_,
                         h[(std::size_t) l], c[(std::size_t) l], work);
            }
        }

        matvec(outputKernel_, h[(std::size_t) (layers - 1)].data(), logits);
        for (std::size_t i = 0; i < logits.size() && i < outputBias_.size; ++i)
            logits[i] += outputBias_.data[i];

        const int label = juce::jlimit(0, outputDepth_ - 1,
                                       sampleCategorical(logits, (float) temperature, rng));
        labels.push_back(label);

        std::fill(prev.begin(), prev.end(), 0.0f);
        prev[(std::size_t) label] = 1.0f;
    }

    return outputToSequence(labels);
}

Sequence MusicVAE::sample(double temperature, std::uint32_t seed) const
{
    if (! loaded_)
        return {};
    std::mt19937 rng(seed);
    std::vector<float> z((std::size_t) zDims_);
    for (auto& v : z)
        v = randomNormal(rng);
    return decode(z, temperature, seed * 1664525u + 1013904223u);
}

Sequence MusicVAE::encodeDecode(const Sequence& input, double temperature,
                                float similarity, std::uint32_t seed) const
{
    if (! loaded_)
        return {};

    auto z = encode(input);
    if (z.empty())
        return {};

    similarity = juce::jlimit(0.0f, 1.0f, similarity);
    std::mt19937 rng(seed);
    const float noiseScale = std::sqrt(juce::jmax(0.0f, 1.0f - similarity * similarity));
    for (auto& v : z)
        v = v * similarity + randomNormal(rng) * noiseScale;

    return decode(z, temperature, seed * 1664525u + 1013904223u);
}

} // namespace cadence::magenta

#pragma once

// Magenta Studio — native inference for the MusicVAE checkpoints used by
// Generate / Interpolate / Groove.
//
// Magenta Studio (https://github.com/magenta/magenta-studio) runs these
// checkpoints in TensorFlow.js. The checkpoints are, however, plain float32
// weights plus a JSON description, so the exact same network can be executed
// here with a tiny amount of linear algebra. This keeps generation inside the
// audio engine: no Node sidecar, no WebView, no model server, and it works
// offline on both Apple Silicon and Intel.
//
// Supported checkpoints (the ones shipped with Magenta Studio's Generate):
//   - mel_4bar_med_lokl      (melody, 4 bars, 90-class one-hot)
//   - drums_4bar_med_lokl    (drums, 4 bars, 10-class in / 512-class out)
//
// The implementations mirror @magenta/music 1.23.x:
//   MusicVAE.encode  -> bidirectional LSTM encoder -> mu
//   MusicVAE.decode  -> 3-layer LSTM decoder with categorical sampling
//   MusicVAE.sample  -> random z ~ N(0, 1) -> decode
//   MusicVAE.similar -> encode and re-decode with latent noise
//
// Weights stay in their original float32 form. Loading one model needs
// roughly 137 MB of RAM; the service caches it between generations.

#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace cadence::magenta
{

struct Note
{
    int pitch = 60;
    double startStep = 0.0;   // 16th-note steps (4 per quarter)
    double lengthSteps = 1.0;
    float velocity = 0.8f;
    bool isDrum = false;
};

struct Sequence
{
    int stepsPerQuarter = 4;
    int numSteps = 64;
    std::vector<Note> notes;
};

enum class Kind
{
    Melody,
    Drums
};

class MusicVAE
{
public:
    MusicVAE();
    ~MusicVAE();

    MusicVAE(const MusicVAE&) = delete;
    MusicVAE& operator= (const MusicVAE&) = delete;

    // Loads a checkpoint directory (config.json + weights_manifest.json +
    // shards). Returns false and fills `error` on any shape/file mismatch.
    bool load(const juce::File& checkpointDir, juce::String& error);

    bool isLoaded() const noexcept { return loaded_; }
    Kind kind() const noexcept { return kind_; }
    int numSteps() const noexcept { return numSteps_; }
    int bars() const noexcept { return numSteps_ / 16; }
    int stepsPerQuarter() const noexcept { return 4; }
    const juce::String& displayName() const noexcept { return displayName_; }

    // A fresh sample from the prior. Temperature <= 0 selects argmax.
    Sequence sample(double temperature, std::uint32_t seed) const;

    // Encodes `input`, adds latent noise (0 = exact reconstruction, 1 = pure
    // prior) and decodes again. This is the "regenerate / similar" path.
    Sequence encodeDecode(const Sequence& input, double temperature,
                          float similarity, std::uint32_t seed) const;

    // Encodes a sequence to its 256-dim latent vector (mu).
    std::vector<float> encode(const Sequence& input) const;

    // Decodes a latent vector. Deterministic when temperature <= 0.
    Sequence decode(const std::vector<float>& z, double temperature,
                    std::uint32_t seed) const;

private:
    struct Tensor
    {
        std::vector<int> shape;
        const float* data = nullptr;
        std::size_t size = 0;

        int rows() const { return shape.size() > 0 ? shape[0] : 0; }
        int cols() const { return shape.size() > 1 ? shape[1] : 0; }
    };

    bool loadConfig(const juce::File& dir, juce::String& error);
    bool loadWeights(const juce::File& dir, juce::String& error);
    const Tensor* find(const char* name) const;
    bool require(const char* name, Tensor& out, juce::String& error) const;

    void buildInput(const Sequence& sequence, std::vector<float>& input) const;
    Sequence outputToSequence(const std::vector<int>& labels) const;

    // y = A * x (row-major A).
    static void matvec(const Tensor& a, const float* x, std::vector<float>& y);

    // One LSTM layer. `h`/`c` are in/out; `x` is the layer input.
    void lstmStep(const Tensor& kernel, const Tensor& bias,
                  const float* x, int inputDim,
                  std::vector<float>& h, std::vector<float>& c,
                  std::vector<float>& work) const;

    static float sigmoid(float v) noexcept;
    static float randomNormal(std::mt19937& rng);
    static int sampleCategorical(const std::vector<float>& logits,
                                 float temperature, std::mt19937& rng);

    bool loaded_ = false;
    Kind kind_ = Kind::Melody;
    juce::String displayName_;
    int numSteps_ = 64;
    int inputDepth_ = 0;
    int outputDepth_ = 0;
    int minPitch_ = 21;
    int maxPitch_ = 108;
    int hidden_ = 0;
    int zDims_ = 0;

    std::vector<std::vector<float>> shards_;
    std::map<std::string, Tensor> vars_;

    Tensor encoderFwKernel_, encoderFwBias_;
    Tensor encoderBwKernel_, encoderBwBias_;
    Tensor encoderMuKernel_, encoderMuBias_;
    Tensor decoderInitKernel_, decoderInitBias_;
    std::vector<Tensor> decoderKernels_, decoderBiases_;
    Tensor outputKernel_, outputBias_;
};

} // namespace cadence::magenta

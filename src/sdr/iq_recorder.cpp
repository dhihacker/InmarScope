#include "sdr/iq_recorder.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;

void put32(std::FILE* f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v), (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    std::fwrite(b, 1, 4, f);
}
void put16(std::FILE* f, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)(v), (uint8_t)(v >> 8)};
    std::fwrite(b, 1, 2, f);
}

} // namespace

bool IqRecorder::start(const std::string& path, double sampleRate)
{
    stop();
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) return false;
    path_ = path;
    sampleRate_ = sampleRate;
    dataBytes_ = 0;

    const uint16_t channels = 2;
    const uint16_t bits = 8;
    const uint32_t byteRate = (uint32_t)(sampleRate_ * channels * (bits / 8));
    const uint16_t blockAlign = (uint16_t)(channels * (bits / 8));

    // RIFF header
    std::fwrite("RIFF", 1, 4, f_);
    put32(f_, 36);                 // RIFF size placeholder (patched on stop)
    std::fwrite("WAVE", 1, 4, f_);

    // fmt chunk
    std::fwrite("fmt ", 1, 4, f_);
    put32(f_, 16);                 // fmt size
    put16(f_, 1);                  // PCM
    put16(f_, channels);
    put32(f_, (uint32_t)sampleRate);
    put32(f_, byteRate);
    put16(f_, blockAlign);
    put16(f_, bits);

    // data chunk
    std::fwrite("data", 1, 4, f_);
    put32(f_, 0);                  // data size placeholder (patched on stop)

    startTime_ = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    recording_.store(true);
    return true;
}

void IqRecorder::write(const float* iq, int nComplex)
{
    if (!f_ || nComplex <= 0) return;
    int nBytes = nComplex * 2; // two 8-bit bytes per complex sample
    std::vector<uint8_t> buf((size_t)nBytes);
    for (int i = 0; i < nComplex; ++i)
    {
        float fi = iq[i * 2] * 127.0f + 128.0f;
        float fq = iq[i * 2 + 1] * 127.0f + 128.0f;
        if (fi < 0.0f) fi = 0.0f; if (fi > 255.0f) fi = 255.0f;
        if (fq < 0.0f) fq = 0.0f; if (fq > 255.0f) fq = 255.0f;
        buf[(size_t)i * 2] = (uint8_t)fi;
        buf[(size_t)i * 2 + 1] = (uint8_t)fq;
    }
    std::fwrite(buf.data(), 1, (size_t)nBytes, f_);
    dataBytes_ += (uint32_t)nBytes;
}

void IqRecorder::stop()
{
    if (!f_) return;
    // Patch WAV header sizes
    std::fseek(f_, 4, SEEK_SET);
    put32(f_, 36 + dataBytes_);
    std::fseek(f_, 40, SEEK_SET);
    put32(f_, dataBytes_);
    std::fclose(f_);
    f_ = nullptr;
    recording_.store(false);
}

double IqRecorder::elapsed() const
{
    if (!recording_.load()) return 0.0;
    double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    return now - startTime_;
}

// IQ recorder: writes live IQ samples to a WAV file in the same 8-bit stereo
// format the built-in WAV player uses.  Matches the existing play-loop reader.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

class IqRecorder
{
public:
    IqRecorder() = default;
    ~IqRecorder() { stop(); }

    IqRecorder(const IqRecorder&) = delete;
    IqRecorder& operator=(const IqRecorder&) = delete;

    // Start recording.  sampleRate is the IQ sample rate (Hz).
    bool start(const std::string& path, double sampleRate);

    // Append nComplex interleaved I,Q float samples (2n floats).
    void write(const float* iq, int nComplex);

    // Finalize header and close.
    void stop();

    bool isRecording() const { return recording_.load(); }
    double elapsed() const; // seconds since start
    const std::string& path() const { return path_; }

private:
    std::atomic<bool> recording_{false};
    std::string path_;
    std::FILE* f_ = nullptr;
    double sampleRate_ = 0.0;
    double startTime_ = 0.0;
    uint32_t dataBytes_ = 0;
};

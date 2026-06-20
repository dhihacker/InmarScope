// Thread-safe log of decoded messages for the Messages panel.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct DecodedMessage
{
    double timeSec = 0.0;
    int channelId = 0;
    double freqMHz = 0.0;
    uint32_t aesId = 0;
    uint8_t gesId = 0;
    int downlink = 0;
    std::string reg;   // aircraft registration (ACARS)
    std::string label; // ACARS label
    std::string text;  // printable rendering of the payload
    std::string hex;   // hex rendering
};

class MessageLog
{
public:
    void add(const DecodedMessage& m)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        msgs_.push_back(m);
        if (msgs_.size() > kMax)
            msgs_.erase(msgs_.begin(), msgs_.begin() + (msgs_.size() - kMax));
        ++count_;
    }

    // Copy a snapshot for rendering.
    std::vector<DecodedMessage> snapshot()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return msgs_;
    }

    uint64_t count()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return count_;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        msgs_.clear();
    }

private:
    static constexpr size_t kMax = 2000;
    std::mutex mtx_;
    std::vector<DecodedMessage> msgs_;
    uint64_t count_ = 0;
};

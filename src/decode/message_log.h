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

// A decoded C-channel (voice/data) assignment.
struct CassignEntry
{
    int channelId = 0;
    uint8_t type = 0;      // 0x31 distress .. 0x34 non-safety
    uint32_t aesId = 0;
    uint8_t gesId = 0;
    double rxMHz = 0.0;    // aircraft receive (forward/downlink)
    double txMHz = 0.0;    // aircraft transmit (return/uplink)
};

class CassignLog
{
public:
    void add(const CassignEntry& e)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        items_.push_back(e);
        if (items_.size() > kMax)
            items_.erase(items_.begin(), items_.begin() + (items_.size() - kMax));
        ++count_;
    }

    std::vector<CassignEntry> snapshot()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return items_;
    }

    uint64_t count()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return count_;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        items_.clear();
    }

private:
    static constexpr size_t kMax = 1000;
    std::mutex mtx_;
    std::vector<CassignEntry> items_;
    uint64_t count_ = 0;
};

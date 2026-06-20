#include "sdr/sdrpp_server_source.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define CLOSESOCK ::close
#endif

#include <zstd.h>

#include <cstring>
#include <string>

// SDR++ server protocol constants.
namespace {
enum { PKT_COMMAND = 0, PKT_COMMAND_ACK = 1, PKT_BASEBAND = 2, PKT_BASEBAND_COMPRESSED = 3,
       PKT_VFO = 4, PKT_FFT = 5, PKT_ERROR = 6 };
enum { CMD_GET_UI = 0, CMD_UI_ACTION = 1, CMD_START = 2, CMD_STOP = 3, CMD_SET_FREQUENCY = 4,
       CMD_GET_SAMPLERATE = 5, CMD_SET_SAMPLE_TYPE = 6, CMD_SET_COMPRESSION = 7,
       CMD_SET_SAMPLERATE = 0x80, CMD_DISCONNECT = 0x81 };
enum { PCM_I8 = 0, PCM_I16 = 1, PCM_F32 = 2 };

#pragma pack(push, 1)
struct PacketHeader { uint32_t type; uint32_t size; };
#pragma pack(pop)

constexpr int kMaxPacket = 16 * 1024 * 1024;

#if defined(_WIN32)
struct WsaInit { WsaInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); } };
static WsaInit g_wsa;
#endif
} // namespace

SdrppServerSource::~SdrppServerSource()
{
    stop();
    if (dctx_)
        ZSTD_freeDCtx((ZSTD_DCtx*)dctx_);
}

bool SdrppServerSource::recvAll(void* buf, int len)
{
    uint8_t* p = (uint8_t*)buf;
    int got = 0;
    while (got < len)
    {
        int n = ::recv((socket_t)sock_, (char*)p + got, len - got, 0);
        if (n <= 0)
            return false;
        got += n;
    }
    return true;
}

bool SdrppServerSource::sendCommand(uint32_t cmd, const void* args, uint32_t argLen)
{
    uint8_t buf[64];
    PacketHeader* h = (PacketHeader*)buf;
    h->type = PKT_COMMAND;
    h->size = sizeof(PacketHeader) + 4 + argLen; // + CommandHeader(cmd) + args
    *(uint32_t*)(buf + sizeof(PacketHeader)) = cmd;
    if (argLen)
        std::memcpy(buf + sizeof(PacketHeader) + 4, args, argLen);
    int total = (int)h->size;
    int sent = 0;
    while (sent < total)
    {
        int n = ::send((socket_t)sock_, (const char*)buf + sent, total - sent, 0);
        if (n <= 0)
            return false;
        sent += n;
    }
    return true;
}

bool SdrppServerSource::start(int, SdrSampleCb cb, std::string& err)
{
    if (running_.load())
        return true;

    // Resolve + connect.
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0 || !res)
    {
        err = "Cannot resolve host " + host_;
        return false;
    }
    socket_t s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET)
    {
        freeaddrinfo(res);
        err = "socket() failed";
        return false;
    }
    if (::connect(s, res->ai_addr, (int)res->ai_addrlen) != 0)
    {
        freeaddrinfo(res);
        CLOSESOCK(s);
        err = "Cannot connect to " + host_ + ":" + portStr;
        return false;
    }
    freeaddrinfo(res);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    sock_ = (uintptr_t)s;

    if (!dctx_)
        dctx_ = ZSTD_createDCtx();
    rbuf_.resize(kMaxPacket);
    dbuf_.resize(kMaxPacket);
    cb_ = std::move(cb);

    // Handshake + config (fire-and-forget; the worker drains any ACKs).
    sendCommand(CMD_GET_UI, nullptr, 0);
    uint8_t st = (uint8_t)sampleType_;
    sendCommand(CMD_SET_SAMPLE_TYPE, &st, 1);
    uint8_t comp = compression_ ? 1 : 0;
    sendCommand(CMD_SET_COMPRESSION, &comp, 1);
    double f = centerFreq_;
    sendCommand(CMD_SET_FREQUENCY, &f, sizeof(double));
    sendCommand(CMD_START, nullptr, 0);

    running_.store(true);
    thread_ = std::thread([this]() { worker(); });
    return true;
}

void SdrppServerSource::stop()
{
    if (!running_.load() && sock_ == ~(uintptr_t)0)
        return;
    if (sock_ != ~(uintptr_t)0)
        sendCommand(CMD_STOP, nullptr, 0);
    running_.store(false);
    if (sock_ != ~(uintptr_t)0)
    {
        CLOSESOCK((socket_t)sock_); // unblocks recv
        sock_ = ~(uintptr_t)0;
    }
    if (thread_.joinable())
        thread_.join();
    cb_ = nullptr;
}

void SdrppServerSource::setCenterFreq(double hz)
{
    centerFreq_ = hz;
    if (running_.load())
        sendCommand(CMD_SET_FREQUENCY, &hz, sizeof(double));
}

void SdrppServerSource::handleBaseband(const uint8_t* data, int len)
{
    if (len < 8)
        return;
    uint16_t type = *(const uint16_t*)(data + 2);
    float scaler = *(const float*)(data + 4);
    const uint8_t* pcm = data + 8;
    int pcmLen = len - 8;

    if (type == PCM_F32)
    {
        int n = pcmLen / (int)(2 * sizeof(float));
        if (n > 0 && cb_)
            cb_((const float*)pcm, n);
    }
    else if (type == PCM_I16)
    {
        int n = pcmLen / (int)(2 * sizeof(int16_t));
        if (n <= 0)
            return;
        if ((int)fbuf_.size() < n * 2)
            fbuf_.resize((size_t)n * 2);
        const int16_t* s = (const int16_t*)pcm;
        float g = scaler / 32768.0f;
        for (int i = 0; i < n * 2; ++i)
            fbuf_[i] = s[i] * g;
        if (cb_)
            cb_(fbuf_.data(), n);
    }
    else if (type == PCM_I8)
    {
        int n = pcmLen / 2;
        if (n <= 0)
            return;
        if ((int)fbuf_.size() < n * 2)
            fbuf_.resize((size_t)n * 2);
        const int8_t* s = (const int8_t*)pcm;
        float g = scaler / 128.0f;
        for (int i = 0; i < n * 2; ++i)
            fbuf_[i] = s[i] * g;
        if (cb_)
            cb_(fbuf_.data(), n);
    }
}

void SdrppServerSource::worker()
{
    while (running_.load())
    {
        PacketHeader hdr;
        if (!recvAll(&hdr, sizeof(hdr)))
            break;
        if (hdr.size < sizeof(PacketHeader) || hdr.size > (uint32_t)kMaxPacket)
            break;
        int payload = (int)hdr.size - (int)sizeof(PacketHeader);
        if (payload > 0 && !recvAll(rbuf_.data(), payload))
            break;

        if (hdr.type == PKT_COMMAND)
        {
            uint32_t cmd = (payload >= 4) ? *(uint32_t*)rbuf_.data() : 0;
            if (cmd == CMD_SET_SAMPLERATE && payload >= 12)
                sampleRate_.store(*(double*)(rbuf_.data() + 4));
            else if (cmd == CMD_DISCONNECT)
                break;
        }
        else if (hdr.type == PKT_BASEBAND)
        {
            handleBaseband(rbuf_.data(), payload);
        }
        else if (hdr.type == PKT_BASEBAND_COMPRESSED)
        {
            size_t out = ZSTD_decompressDCtx((ZSTD_DCtx*)dctx_, dbuf_.data(), dbuf_.size(),
                                             rbuf_.data(), payload);
            if (!ZSTD_isError(out) && out > 0)
                handleBaseband(dbuf_.data(), (int)out);
        }
        // COMMAND_ACK / VFO / FFT / ERROR: ignored.
    }
    running_.store(false);
}

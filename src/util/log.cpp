#include "util/log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {

std::mutex  g_logMtx;
std::FILE*  g_logFile = nullptr;

void ensureOpen()
{
    if (g_logFile)
        return;
    g_logFile = std::fopen("log.txt", "ab");
}

} // namespace

void logWrite(const char* fmt, ...)
{
    std::lock_guard<std::mutex> lk(g_logMtx);
    ensureOpen();
    if (!g_logFile)
        return;

    // Timestamp prefix: [HH:MM:SS]
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::fprintf(g_logFile, "[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min, tm.tm_sec);

    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(g_logFile, fmt, args);
    va_end(args);

    std::fputc('\n', g_logFile);
    std::fflush(g_logFile);
}

#include "pch.h"
#include "log.h"
#include <stdarg.h>
#include <share.h>

static FILE* g_logFile = NULL;
static std::mutex g_logLock;
void LogOpen(const wchar_t* path)
{
    g_logFile = _wfsopen(path, L"w", _SH_DENYNO);
}

void Log(const char* fmt, ...)
{
    if (g_logFile == NULL)
        return;

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    SYSTEMTIME st;
    GetLocalTime(&st);
    g_logLock.lock();
    fprintf(g_logFile, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
    fflush(g_logFile);
    g_logLock.unlock();
}

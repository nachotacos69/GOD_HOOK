#include "log.h"

#if GEB_ENABLE_LOGGING
#include <pspiofilemgr.h>
#include <pspsysclib.h>
#include <stdarg.h>

#include "log_format.h"
#include "runtime.h"

#define LOG_PATH "ms0:/PSP/test.txt"
#define LOG_BUFFER_SIZE 768

typedef struct LogBuffer {
    char *data;
    unsigned int capacity;
    unsigned int length;
} LogBuffer;

static SceUID g_log_fd = -1;

static void log_format_char(void *context, int ch)
{
    LogBuffer *buffer = (LogBuffer *)context;

    if (ch == 0x200) {
        buffer->length = 0;
        return;
    }
    if (ch == 0x201 || ch < 0 || ch > 0xFF) {
        return;
    }
    if (buffer->length + 1 < buffer->capacity) {
        buffer->data[buffer->length++] = (char)ch;
    }
}

static int log_write_all(const char *data, unsigned int length)
{
    unsigned int written = 0;

    while (written < length) {
        int result = sceIoWrite(g_log_fd, data + written, length - written);
        if (result <= 0) {
            SceUID failed_fd = g_log_fd;
            g_log_fd = -1;
            if (failed_fd >= 0) {
                sceIoClose(failed_fd);
            }
            return result < 0 ? result : -1;
        }
        written += (unsigned int)result;
    }
    return (int)written;
}

int log_open(void)
{
    sceIoMkdir("ms0:/PSP", 0777);
    if (g_log_fd >= 0) {
        SceUID old_fd = g_log_fd;
        g_log_fd = -1;
        sceIoClose(old_fd);
    }
    g_log_fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    return g_log_fd < 0 ? (int)g_log_fd : 0;
}

int log_printf(const char *format, ...)
{
    char data[LOG_BUFFER_SIZE];
    LogBuffer buffer;
    va_list args;

    if (g_log_fd < 0 || format == 0) {
        return -1;
    }

    buffer.data = data;
    buffer.capacity = sizeof(data);
    buffer.length = 0;

    va_start(args, format);
    if (geb_runtime_is_ppsspp()) {
        int length = geb_log_vformat(data, sizeof(data), format, args);
        if (length < 0) {
            va_end(args);
            return -1;
        }
        buffer.length = (unsigned int)length;
    } else {
        prnt(log_format_char, &buffer, format, args);
    }
    va_end(args);

    data[buffer.length] = '\0';
    return log_write_all(data, buffer.length);
}

void log_close(void)
{
    if (g_log_fd >= 0) {
        sceIoClose(g_log_fd);
        g_log_fd = -1;
    }
}

int log_is_open(void)
{
    return g_log_fd >= 0;
}
#else
/* Stable never opens, truncates, formats or writes a log file. */
int log_open(void) { return 0; }
void log_close(void) { }
int log_is_open(void) { return 0; }
#endif

#include "logger.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

Logger g_logger = {NULL, PTHREAD_MUTEX_INITIALIZER, 1};

static const char *level_str(LogLevel level) {
    switch (level) {
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
    }
    return "?????";
}

int logger_init(const char *filepath, int to_stdout) {
    g_logger.to_stdout = to_stdout;
    if (filepath) {
        g_logger.file = fopen(filepath, "a");
        if (!g_logger.file) return -1;
    }
    return 0;
}

void logger_close(void) {
    if (g_logger.file) {
        fclose(g_logger.file);
        g_logger.file = NULL;
    }
}

void log_event(LogLevel level, const char *client_info, const char *fmt, ...) {
    char timestamp[32];
    char message[512];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    pthread_t tid = pthread_self();
    const char *ci = client_info ? client_info : "-";

    pthread_mutex_lock(&g_logger.lock);

    if (g_logger.to_stdout) {
        printf("[%s] [%s] [TID:%lu] [%s] %s\n",
               timestamp, level_str(level), (unsigned long)tid, ci, message);
        fflush(stdout);
    }
    if (g_logger.file) {
        fprintf(g_logger.file, "[%s] [%s] [TID:%lu] [%s] %s\n",
                timestamp, level_str(level), (unsigned long)tid, ci, message);
        fflush(g_logger.file);
    }

    pthread_mutex_unlock(&g_logger.lock);
}

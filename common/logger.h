#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>
#include <pthread.h>

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

typedef struct {
    FILE       *file;
    pthread_mutex_t lock;
    int         to_stdout;
} Logger;

extern Logger g_logger;

int  logger_init(const char *filepath, int to_stdout);
void logger_close(void);
void log_event(LogLevel level, const char *client_info, const char *fmt, ...);

#define LOG_INFO_EV(client, ...)  log_event(LOG_INFO,  client, __VA_ARGS__)
#define LOG_WARN_EV(client, ...)  log_event(LOG_WARN,  client, __VA_ARGS__)
#define LOG_ERROR_EV(client, ...) log_event(LOG_ERROR, client, __VA_ARGS__)

#endif

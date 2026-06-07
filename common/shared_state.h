#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <pthread.h>

#define MAX_VIS_CLIENTS 16

typedef enum {
    CS_IDLE        = 0,
    CS_CONNECTING  = 1,
    CS_CONNECTED   = 2,
    CS_CHECKING    = 3,
    CS_DOWNLOADING = 4,
    CS_UP_TO_DATE  = 5,
    CS_DONE        = 6,
    CS_ERROR       = 7
} ClientState;

typedef struct {
    int         id;
    ClientState state;
    int         version;
    float       progress;   /* 0.0 - 1.0 for download */
    char        ip[32];
} ClientInfo;

typedef struct {
    ClientInfo      clients[MAX_VIS_CLIENTS];
    int             count;
    int             latest_version;
    pthread_mutex_t lock;
} SharedState;

extern SharedState g_state;

void shared_state_init(int latest_version);
void shared_state_set(int id, ClientState state, int version, float progress, const char *ip);

#endif

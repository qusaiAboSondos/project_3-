#include "shared_state.h"
#include <string.h>

SharedState g_state = {
    .clients = {},
    .count = 0,
    .latest_version = 1,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

void shared_state_init(int latest_version) {
    pthread_mutex_lock(&g_state.lock);
    g_state.latest_version = latest_version;
    g_state.count = 0;
    pthread_mutex_unlock(&g_state.lock);
}

void shared_state_set(int id, ClientState state, int version, float progress, const char *ip) {
    pthread_mutex_lock(&g_state.lock);

    int idx = -1;
    for (int i = 0; i < g_state.count; i++) {
        if (g_state.clients[i].id == id) { idx = i; break; }
    }
    if (idx == -1 && g_state.count < MAX_VIS_CLIENTS) {
        idx = g_state.count++;
        g_state.clients[idx].id = id;
    }
    if (idx >= 0) {
        g_state.clients[idx].state    = state;
        g_state.clients[idx].version  = version;
        g_state.clients[idx].progress = progress;
        if (ip) {
            strncpy(g_state.clients[idx].ip, ip, sizeof(g_state.clients[idx].ip) - 1);
        }
    }

    pthread_mutex_unlock(&g_state.lock);
}

#ifndef SERVER_H
#define SERVER_H

#include "../common/config.h"

typedef struct {
    int  client_fd;
    int  client_id;
    char client_ip[64];
    int  client_port;
    int  latest_version;
    char update_file[256];
    int  buffer_size;
} ClientArgs;

void *handle_client(void *arg);
int   start_server(const Config *cfg);

#endif

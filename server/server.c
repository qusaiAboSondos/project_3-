#include "server.h"
#include "visualizer.h"
#include "../common/logger.h"
#include "../common/config.h"
#include "../common/shared_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MSG_UPDATE_AVAILABLE  "UPDATE_AVAILABLE"
#define MSG_UP_TO_DATE        "UP_TO_DATE"

static int g_client_id_counter = 0;
static pthread_mutex_t g_id_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- per-client thread ---------- */

void *handle_client(void *arg) {
    ClientArgs *ca = (ClientArgs *)arg;
    char client_info[80];
    snprintf(client_info, sizeof(client_info), "%s:%d", ca->client_ip, ca->client_port);

    LOG_INFO_EV(client_info, "Client connected");
    shared_state_set(ca->client_id, CS_CONNECTED, 0, 0.0f, ca->client_ip);

    /* 1. Receive client version */
    char buf[64] = {0};
    ssize_t n = recv(ca->client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        LOG_ERROR_EV(client_info, "Failed to receive version: %s", strerror(errno));
        goto cleanup;
    }
    buf[n] = '\0';

    int client_version = atoi(buf);
    LOG_INFO_EV(client_info, "Version request received: client_version=%d latest_version=%d",
                client_version, ca->latest_version);
    shared_state_set(ca->client_id, CS_CHECKING, client_version, 0.0f, ca->client_ip);

    /* 2. Compare versions and respond */
    if (client_version < ca->latest_version) {
        LOG_INFO_EV(client_info, "Update required, sending notification");
        shared_state_set(ca->client_id, CS_DOWNLOADING, client_version, 0.0f, ca->client_ip);

        /* 3. Send update file */
        FILE *f = fopen(ca->update_file, "rb");
        if (!f) {
            LOG_ERROR_EV(client_info, "Cannot open update file: %s", ca->update_file);
            goto cleanup;
        }

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        rewind(f);

        /* send "UPDATE_AVAILABLE:<size>\n" as one atomic message */
        char header[64];
        snprintf(header, sizeof(header), "%s:%ld\n", MSG_UPDATE_AVAILABLE, fsize);
        send(ca->client_fd, header, strlen(header), 0);

        char *transfer_buf = malloc(ca->buffer_size);
        if (!transfer_buf) { fclose(f); goto cleanup; }

        long sent_total = 0;
        size_t bytes_read;
        while ((bytes_read = fread(transfer_buf, 1, ca->buffer_size, f)) > 0) {
            ssize_t sent = send(ca->client_fd, transfer_buf, bytes_read, 0);
            if (sent < 0) {
                LOG_ERROR_EV(client_info, "Send error during file transfer: %s", strerror(errno));
                shared_state_set(ca->client_id, CS_ERROR, client_version, 0.0f, ca->client_ip);
                free(transfer_buf);
                fclose(f);
                goto cleanup;
            }
            sent_total += sent;
            float progress = fsize > 0 ? (float)sent_total / (float)fsize : 1.0f;
            shared_state_set(ca->client_id, CS_DOWNLOADING, client_version, progress, ca->client_ip);
        }

        free(transfer_buf);
        fclose(f);
        LOG_INFO_EV(client_info, "Update file sent successfully (%ld bytes)", sent_total);
        shared_state_set(ca->client_id, CS_DONE, ca->latest_version, 1.0f, ca->client_ip);

    } else {
        LOG_INFO_EV(client_info, "Client is up to date");
        send(ca->client_fd, MSG_UP_TO_DATE, strlen(MSG_UP_TO_DATE), 0);
        shared_state_set(ca->client_id, CS_UP_TO_DATE, client_version, 1.0f, ca->client_ip);
    }

cleanup:
    close(ca->client_fd);
    LOG_INFO_EV(client_info, "Connection closed");
    free(ca);
    return NULL;
}

/* ---------- main server loop ---------- */

int start_server(const Config *cfg) {
    int port          = config_get_int(cfg, "SERVER_PORT",   8080);
    int max_clients   = config_get_int(cfg, "MAX_CLIENTS",   10);
    int latest_ver    = config_get_int(cfg, "LATEST_VERSION", 1);
    int buffer_size   = config_get_int(cfg, "BUFFER_SIZE",   4096);
    const char *upfile = config_get(cfg, "UPDATE_FILE", "updates/update_v1.pkg");
    const char *logfile = config_get(cfg, "LOG_FILE",   "logs/server.log");

    logger_init(logfile, 1);
    shared_state_init(latest_ver);
    LOG_INFO_EV(NULL, "Server starting on port %d", port);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR_EV(NULL, "socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR_EV(NULL, "bind() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, max_clients) < 0) {
        LOG_ERROR_EV(NULL, "listen() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    LOG_INFO_EV(NULL, "Server listening. Latest version: %d", latest_ver);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            LOG_WARN_EV(NULL, "accept() failed: %s", strerror(errno));
            continue;
        }

        ClientArgs *ca = malloc(sizeof(ClientArgs));
        if (!ca) { close(client_fd); continue; }

        ca->client_fd      = client_fd;
        ca->latest_version = latest_ver;
        ca->buffer_size    = buffer_size;

        pthread_mutex_lock(&g_id_lock);
        ca->client_id = ++g_client_id_counter;
        pthread_mutex_unlock(&g_id_lock);

        shared_state_set(ca->client_id, CS_CONNECTING, 0, 0.0f, NULL);
        strncpy(ca->update_file, upfile, sizeof(ca->update_file) - 1);
        inet_ntop(AF_INET, &client_addr.sin_addr, ca->client_ip, sizeof(ca->client_ip));
        ca->client_port = ntohs(client_addr.sin_port);

        LOG_INFO_EV(NULL, "Accepted connection from %s:%d", ca->client_ip, ca->client_port);

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, handle_client, ca) != 0) {
            LOG_ERROR_EV(NULL, "pthread_create() failed: %s", strerror(errno));
            free(ca);
            close(client_fd);
        }
        pthread_attr_destroy(&attr);
    }

    close(server_fd);
    LOG_INFO_EV(NULL, "Server shut down");
    logger_close();
    return 0;
}

/* ---------- visualizer thread wrapper ---------- */

typedef struct { int argc; char **argv; } GlutArgs;

static void *visualizer_thread(void *arg) {
    GlutArgs *ga = (GlutArgs *)arg;
    visualizer_run(&ga->argc, ga->argv);
    return NULL;
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.txt";

    Config cfg;
    if (config_load(&cfg, config_file) != 0) {
        fprintf(stderr, "Failed to load config: %s\n", config_file);
        return 1;
    }

    /* start OpenGL visualizer only if DISPLAY is available */
    const char *display = getenv("DISPLAY");
    if (display && display[0] != '\0') {
        GlutArgs ga = { argc, argv };
        pthread_t vis_tid;
        pthread_create(&vis_tid, NULL, visualizer_thread, &ga);
        pthread_detach(vis_tid);
    } else {
        fprintf(stdout, "[INFO] No DISPLAY found, running without GUI.\n");
    }

    return start_server(&cfg);
}

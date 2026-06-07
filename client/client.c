#include "client.h"
#include "../common/logger.h"
#include "../common/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MSG_UPDATE_AVAILABLE "UPDATE_AVAILABLE"
#define MSG_UP_TO_DATE       "UP_TO_DATE"

/* reads current version from a local text file (e.g. "1") */
int getCurrentVersion(const char *version_file) {
    FILE *f = fopen(version_file, "r");
    if (!f) {
        LOG_WARN_EV("client", "Version file not found (%s), assuming version 0", version_file);
        return 0;
    }
    int ver = 0;
    fscanf(f, "%d", &ver);
    fclose(f);
    return ver;
}

/* saves the new version number locally after a successful update */
static void save_version(const char *version_file, int version) {
    FILE *f = fopen(version_file, "w");
    if (!f) return;
    fprintf(f, "%d\n", version);
    fclose(f);
}

int CheckForUpdate(const Config *cfg) {
    const char *host        = config_get(cfg, "SERVER_HOST",   "127.0.0.1");
    int         port        = config_get_int(cfg, "SERVER_PORT", 8080);
    int         buf_size    = config_get_int(cfg, "BUFFER_SIZE", 4096);
    const char *ver_file    = config_get(cfg, "VERSION_FILE",  "client/current_version.txt");
    const char *logfile     = config_get(cfg, "CLIENT_LOG",    "logs/client.log");

    logger_init(logfile, 1);

    int current_ver = getCurrentVersion(ver_file);
    LOG_INFO_EV("client", "Current version: %d", current_ver);

    /* --- connect --- */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR_EV("client", "socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);

    LOG_INFO_EV("client", "Connecting to %s:%d ...", host, port);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR_EV("client", "connect() failed: %s", strerror(errno));
        close(sock);
        return -1;
    }
    LOG_INFO_EV("client", "Connected to server");

    /* --- send version --- */
    char ver_str[16];
    snprintf(ver_str, sizeof(ver_str), "%d", current_ver);
    send(sock, ver_str, strlen(ver_str), 0);
    LOG_INFO_EV("client", "Sent version: %d", current_ver);

    /* --- receive server response (read until '\n') --- */
    char resp[256] = {0};
    ssize_t n;
    int resp_len = 0;
    /* read byte by byte until newline or buffer full */
    while (resp_len < (int)sizeof(resp) - 1) {
        n = recv(sock, resp + resp_len, 1, 0);
        if (n <= 0) break;
        if (resp[resp_len] == '\n') { resp[resp_len] = '\0'; break; }
        resp_len++;
    }
    if (resp_len == 0) {
        LOG_ERROR_EV("client", "No response from server");
        close(sock);
        return -1;
    }

    if (strcmp(resp, MSG_UP_TO_DATE) == 0) {
        printf("[CLIENT] Software is already up to date (version %d).\n", current_ver);
        LOG_INFO_EV("client", "Already up to date");
        close(sock);
        logger_close();
        return 0;
    }

    /* response format: "UPDATE_AVAILABLE:<size>" */
    long expected_size = 0;
    char *colon = strchr(resp, ':');
    if (colon) {
        *colon = '\0';
        expected_size = atol(colon + 1);
    }

    if (strcmp(resp, MSG_UPDATE_AVAILABLE) != 0) {
        LOG_ERROR_EV("client", "Unknown server response: %s", resp);
        close(sock);
        return -1;
    }

    LOG_INFO_EV("client", "Update available — file size: %ld bytes", expected_size);
    LOG_INFO_EV("client", "Update file size: %ld bytes", expected_size);

    /* --- receive and save update file --- */
    const char *save_path = config_get(cfg, "DOWNLOAD_PATH", "client/downloaded_update.pkg");
    FILE *out = fopen(save_path, "wb");
    if (!out) {
        LOG_ERROR_EV("client", "Cannot open output file: %s", save_path);
        close(sock);
        return -1;
    }

    char *buf = malloc(buf_size);
    if (!buf) { fclose(out); close(sock); return -1; }

    long received = 0;
    while (received < expected_size) {
        n = recv(sock, buf, buf_size, 0);
        if (n <= 0) {
            LOG_ERROR_EV("client", "Connection lost during download (received %ld/%ld)", received, expected_size);
            free(buf);
            fclose(out);
            close(sock);
            return -1;
        }
        fwrite(buf, 1, n, out);
        received += n;
    }

    free(buf);
    fclose(out);

    LOG_INFO_EV("client", "Download complete: %ld bytes saved to %s", received, save_path);
    printf("[CLIENT] Update downloaded successfully to: %s\n", save_path);
    printf("[CLIENT] Simulating update installation...\n");
    sleep(1);
    printf("[CLIENT] Update installed. New version active.\n");

    /* update local version file */
    int new_ver = config_get_int(cfg, "LATEST_VERSION", current_ver + 1);
    save_version(ver_file, new_ver);
    LOG_INFO_EV("client", "Version updated to %d", new_ver);

    close(sock);
    logger_close();
    return 0;
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.txt";

    Config cfg;
    if (config_load(&cfg, config_file) != 0) {
        fprintf(stderr, "Failed to load config: %s\n", config_file);
        return 1;
    }

    return CheckForUpdate(&cfg);
}

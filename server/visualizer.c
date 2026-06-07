#include <GL/glut.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../common/shared_state.h"

#define WIN_W 800
#define WIN_H 600
#define CARD_W 160
#define CARD_H 120
#define COLS 4

static void draw_text(float x, float y, const char *str) {
    glRasterPos2f(x, y);
    for (const char *c = str; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
}

static void draw_rect_filled(float x, float y, float w, float h,
                              float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
}

static void draw_rect_outline(float x, float y, float w, float h,
                               float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
}

static void state_color(ClientState s, float *r, float *g, float *b) {
    switch (s) {
        case CS_IDLE:        *r=0.3f; *g=0.3f; *b=0.3f; break;
        case CS_CONNECTING:  *r=0.9f; *g=0.7f; *b=0.1f; break;
        case CS_CONNECTED:   *r=0.2f; *g=0.6f; *b=0.9f; break;
        case CS_CHECKING:    *r=0.5f; *g=0.4f; *b=0.9f; break;
        case CS_DOWNLOADING: *r=0.1f; *g=0.8f; *b=0.4f; break;
        case CS_UP_TO_DATE:  *r=0.2f; *g=0.9f; *b=0.9f; break;
        case CS_DONE:        *r=0.1f; *g=0.9f; *b=0.1f; break;
        case CS_ERROR:       *r=0.9f; *g=0.1f; *b=0.1f; break;
    }
}

static const char *state_label(ClientState s) {
    switch (s) {
        case CS_IDLE:        return "Idle";
        case CS_CONNECTING:  return "Connecting...";
        case CS_CONNECTED:   return "Connected";
        case CS_CHECKING:    return "Checking version";
        case CS_DOWNLOADING: return "Downloading...";
        case CS_UP_TO_DATE:  return "Up to date";
        case CS_DONE:        return "Update done";
        case CS_ERROR:       return "Error";
    }
    return "?";
}

static void draw_client_card(int col, int row, ClientInfo *ci) {
    float pad = 20.0f;
    float x = pad + col * (CARD_W + pad);
    float y = WIN_H - 200.0f - row * (CARD_H + pad);

    float r, g, b;
    state_color(ci->state, &r, &g, &b);

    /* background */
    draw_rect_filled(x, y, CARD_W, CARD_H, 0.15f, 0.15f, 0.15f);
    /* top color strip */
    draw_rect_filled(x, y + CARD_H - 12, CARD_W, 12, r, g, b);
    /* outline */
    draw_rect_outline(x, y, CARD_W, CARD_H, r, g, b);

    char buf[64];
    glColor3f(0.9f, 0.9f, 0.9f);

    snprintf(buf, sizeof(buf), "Client #%d", ci->id);
    draw_text(x + 6, y + CARD_H - 26, buf);

    snprintf(buf, sizeof(buf), "IP: %s", ci->ip[0] ? ci->ip : "---");
    draw_text(x + 6, y + CARD_H - 42, buf);

    snprintf(buf, sizeof(buf), "Ver: %d", ci->version);
    draw_text(x + 6, y + CARD_H - 58, buf);

    glColor3f(r, g, b);
    draw_text(x + 6, y + CARD_H - 74, state_label(ci->state));

    /* progress bar (only during download) */
    if (ci->state == CS_DOWNLOADING) {
        float bar_w = CARD_W - 12;
        draw_rect_filled(x + 6, y + 10, bar_w, 12, 0.2f, 0.2f, 0.2f);
        draw_rect_filled(x + 6, y + 10, bar_w * ci->progress, 12, 0.1f, 0.85f, 0.3f);
        draw_rect_outline(x + 6, y + 10, bar_w, 12, 0.5f, 0.5f, 0.5f);

        snprintf(buf, sizeof(buf), "%.0f%%", ci->progress * 100.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        draw_text(x + 6 + bar_w / 2 - 10, y + 14, buf);
    }
}

static void draw_server_panel(void) {
    draw_rect_filled(0, WIN_H - 70, WIN_W, 70, 0.1f, 0.1f, 0.2f);
    draw_rect_outline(0, WIN_H - 70, WIN_W, 70, 0.3f, 0.5f, 0.9f);

    glColor3f(0.5f, 0.8f, 1.0f);
    draw_text(200, WIN_H - 25, "SOFTWARE UPDATE FRAMEWORK - SERVER MONITOR");

    pthread_mutex_lock(&g_state.lock);
    char buf[80];
    snprintf(buf, sizeof(buf), "Latest version: %d      Active clients: %d",
             g_state.latest_version, g_state.count);
    pthread_mutex_unlock(&g_state.lock);

    glColor3f(0.8f, 0.8f, 0.8f);
    draw_text(200, WIN_H - 48, buf);
}

static void draw_legend(void) {
    float x = 10, y = 100;
    glColor3f(0.7f, 0.7f, 0.7f);
    draw_text(x, y, "Legend:");

    ClientState states[] = {CS_CONNECTING, CS_DOWNLOADING, CS_UP_TO_DATE, CS_DONE, CS_ERROR};
    const char *labels[] = {"Connecting", "Downloading", "Up to date", "Done", "Error"};
    for (int i = 0; i < 5; i++) {
        float r, g, b;
        state_color(states[i], &r, &g, &b);
        draw_rect_filled(x, y - 18 - i * 18, 12, 12, r, g, b);
        glColor3f(0.8f, 0.8f, 0.8f);
        draw_text(x + 16, y - 15 - i * 18, labels[i]);
    }
}

static void display(void) {
    glClear(GL_COLOR_BUFFER_BIT);

    draw_server_panel();
    draw_legend();

    pthread_mutex_lock(&g_state.lock);
    int count = g_state.count;
    ClientInfo snapshot[MAX_VIS_CLIENTS];
    memcpy(snapshot, g_state.clients, count * sizeof(ClientInfo));
    pthread_mutex_unlock(&g_state.lock);

    for (int i = 0; i < count; i++) {
        int col = i % COLS;
        int row = i / COLS;
        draw_client_card(col, row, &snapshot[i]);
    }

    /* empty slot placeholders */
    if (count == 0) {
        glColor3f(0.4f, 0.4f, 0.4f);
        draw_text(WIN_W / 2 - 80, WIN_H / 2, "Waiting for client connections...");
    }

    glutSwapBuffers();
}

static void timer(int v) {
    (void)v;
    glutPostRedisplay();
    glutTimerFunc(100, timer, 0);
}

static void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
}

void visualizer_run(int *argc, char **argv) {
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("Software Update Framework - Server Monitor");

    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutTimerFunc(100, timer, 0);

    glutMainLoop();
}

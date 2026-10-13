#include "app_state.h"

AppState          g_state;
SemaphoreHandle_t g_stateMutex = nullptr;

void state_init() {
    if (g_stateMutex == nullptr) {
        g_stateMutex = xSemaphoreCreateMutex();
    }
    memset(&g_state, 0, sizeof(g_state));
}

void state_lock() {
    if (g_stateMutex) xSemaphoreTake(g_stateMutex, portMAX_DELAY);
}

void state_unlock() {
    if (g_stateMutex) xSemaphoreGive(g_stateMutex);
}

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

#include "displayport_buffer.h"

/* -------------------------------------------------------
   1) Implement the global buffer
--------------------------------------------------------*/
DisplayPortFrameBuffer g_dpFrameBuffer = {
    .activeBuffer = {0},
    .inactiveBuffer = {0},
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .inited = 0
};

/* -------------------------------------------------------
   2) Implement the buffer functions
--------------------------------------------------------*/
void dpBufferInitialize(void)
{
    if (!g_dpFrameBuffer.inited) {
        // Clear both buffers
        memset(g_dpFrameBuffer.activeBuffer,   0, sizeof(g_dpFrameBuffer.activeBuffer));
        memset(g_dpFrameBuffer.inactiveBuffer, 0, sizeof(g_dpFrameBuffer.inactiveBuffer));
        // You could also explicitly init the mutex if needed:
        // pthread_mutex_init(&g_dpFrameBuffer.lock, NULL);
        g_dpFrameBuffer.inited = 1;
    }
}

void dpClearInactive(void)
{
    if (!g_dpFrameBuffer.inited) {
        dpBufferInitialize();
    }
    pthread_mutex_lock(&g_dpFrameBuffer.lock);
    memset(g_dpFrameBuffer.inactiveBuffer, 0, sizeof(g_dpFrameBuffer.inactiveBuffer));
    pthread_mutex_unlock(&g_dpFrameBuffer.lock);
}

void dpWriteToInactive(uint8_t row, uint8_t col, uint16_t glyphID)
{
    if (!g_dpFrameBuffer.inited) {
        dpBufferInitialize();
    }
    if (row < DP_MAX_ROWS && col < DP_MAX_COLS) {
        pthread_mutex_lock(&g_dpFrameBuffer.lock);
        g_dpFrameBuffer.inactiveBuffer[row * DP_MAX_COLS + col] = glyphID;
        pthread_mutex_unlock(&g_dpFrameBuffer.lock);
    }
}

void dpPublishInactive(void)
{
    if (!g_dpFrameBuffer.inited) {
        dpBufferInitialize();
    }
    pthread_mutex_lock(&g_dpFrameBuffer.lock);
    memcpy(g_dpFrameBuffer.activeBuffer,
           g_dpFrameBuffer.inactiveBuffer,
           sizeof(g_dpFrameBuffer.activeBuffer));
    pthread_mutex_unlock(&g_dpFrameBuffer.lock);
}

/* -------------------------------------------------------
   3) The HOOK for DisplayPortProcess. 
      Signature from your original: void DisplayPortProcess(undefined4 param_1, int param_2)
      We'll call it the same so it overrides. Then we do:
        - parse sub-command
        - update our buffers
        - call the real function afterwards
--------------------------------------------------------*/

/* The real DisplayPortProcess pointer. We'll resolve it on first use. */
typedef void (*RealDisplayPortProcess_t)(uint32_t, int);
static RealDisplayPortProcess_t realDisplayPortProcess = NULL;

/* The hook function. We'll name it exactly the same as the real one:
   "DisplayPortProcess(...)" so it overrides. */
void DisplayPortProcess(uint32_t param_1, int param_2)
{
    // If not resolved yet, do so now.
    if (!realDisplayPortProcess) {
        realDisplayPortProcess =
            (RealDisplayPortProcess_t)dlsym(RTLD_NEXT, "DisplayPortProcess");
        if (!realDisplayPortProcess) {
            return;
        }
    }

    // If param_2 is invalid, skip hooking logic. Let the real function handle it.
    if (param_2) {
        // Sub-command is at offset 0x14
        uint8_t subCmd = *(uint8_t *)(param_2 + 0x14);

        // We do minimal checks. Then do our buffer logic:
        switch (subCmd) {
            case 2: // CLEAR_SCREEN
                dpClearInactive();
                break;

            case 3: // WRITE_STRING
            {
                /* 
                  The data typically: 
                  param_2 + 0x15 => row
                  param_2 + 0x16 => column
                  param_2 + 0x17 => attribute
                  param_2 + 0x18 => string (up to 30 bytes, null‐terminated)
                */
                uint32_t len = *(uint32_t *)(param_2 + 0x10);
                if (len >= 4) {
                    uint8_t row       = *(uint8_t *)(param_2 + 0x15);
                    uint8_t col       = *(uint8_t *)(param_2 + 0x16);
                    const uint8_t* strPtr = (const uint8_t*)(param_2 + 0x18);
                    uint8_t attribute = (*(uint8_t *)(param_2 + 0x17)) & 0x03; // Keep only lower 2 bits (0-3)
                    if (attribute == 3) attribute = 2; // Ensure 3 falls back to 2
                    


                    // We'll handle up to 30 chars or until null terminator.
                    uint32_t maxChars = (len > 3) ? (len - 3) : 0;
                    if (maxChars > 30) maxChars = 30;

                    for (uint32_t i = 0; i < maxChars; i++) {
                        if (strPtr[i] == 0) {
                            // stop on null terminator
                            break;
                        }
                        // glyph = character + (attribute * 256)
                        uint16_t glyphID = (uint16_t)strPtr[i] + ((uint16_t)attribute << 8);
                        dpWriteToInactive(row, col, glyphID);

                        // increment column, wrap row if needed
                        col++;
                        if (col >= DP_MAX_COLS) {
                            col = 0;
                            row++;
                            if (row >= DP_MAX_ROWS) {
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case 4: // DRAW_SCREEN
                dpPublishInactive();
                break;

            default:
                // sub‐commands 0,1 or unknown => do nothing special
                break;
        }
    }

    // Finally, call the real DisplayPortProcess with the original parameters
    realDisplayPortProcess(param_1, param_2);
}

#ifndef DISPLAYPORT_BUFFER_H
#define DISPLAYPORT_BUFFER_H

#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>  // Needed for the `bool` type

bool check_external_resource(int *outWidth, int *outHeight);

/*
  We assume the OSD is 20 rows x 53 columns => 1060 total cells.
  If your real layout is 53 rows x 20 columns, just swap them here,
  but keep 20*53=1060.
*/
#define DP_MAX_ROWS   22
#define DP_MAX_COLS   60
#define DP_FRAME_SIZE (DP_MAX_ROWS * DP_MAX_COLS)

/*
   Global double‐buffer structure:
    - activeBuffer:   read by DrawScreen
    - inactiveBuffer: updated by DisplayPortProcess sub‐commands
    - lock:           protects concurrent access
    - inited:         boolean flag to indicate initialization
*/
typedef struct {
    uint16_t activeBuffer[DP_FRAME_SIZE];
    uint16_t inactiveBuffer[DP_FRAME_SIZE];
    pthread_mutex_t lock;
    int inited;
} DisplayPortFrameBuffer;

/*
   Global instance, shared by:
    - DisplayPortProcessHook.c
    - MxDisplayPortDisplayPort_DrawScreen.c
*/
extern DisplayPortFrameBuffer g_dpFrameBuffer;

extern void* g_myCharItemBuffer[DP_FRAME_SIZE];

/* One‐time initialization of the buffers and mutex (if needed). */
void dpBufferInitialize(void);

/* Clear the entire INACTIVE buffer (e.g., command 2: CLEAR_SCREEN). */
void dpClearInactive(void);

/*
   Write a single glyph ID into the INACTIVE buffer at (row,col).
   If out of range, it does nothing safely.
*/
void dpWriteToInactive(uint8_t row, uint8_t col, uint16_t glyphID);

/*
   Publish the INACTIVE buffer into the ACTIVE buffer. (command 4: DRAW_SCREEN)
*/
void dpPublishInactive(void);

#endif /* DISPLAYPORT_BUFFER_H */

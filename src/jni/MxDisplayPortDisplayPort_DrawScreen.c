#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

#include "font_list.h"  

#include "displayport_buffer.h"
#include "ring_buffer.h" 
#include "o3-custom-fonts.h"

// Forward‐declared real function types:
typedef void* (*CharItemsAt_t)(int base, int col, int row);
typedef void* (*CharItemsCreate_t)(int base, int param_1, int col, int row);
typedef void (*SetConfig_t)(int);
typedef void (*GetScrollOffset_t)(int* outXY, int fontGroup, unsigned int glyphID);
typedef void (*OnSetScrollOffset_t)(int itemPtr, int xOff, int yOff);
typedef void (*orig_MxDisplayPortDisplayPort_DrawScreen_t)(int, int);



// Real function pointers resolved via dlsym(RTLD_NEXT)
static orig_MxDisplayPortDisplayPort_DrawScreen_t orig_func = NULL;
static CharItemsAt_t       real_CharItemsAt       = NULL;
static CharItemsCreate_t   real_CharItemsCreate   = NULL;
static SetConfig_t         real_setConfig         = NULL;
static GetScrollOffset_t   real_GetScrollOffset   = NULL;
static OnSetScrollOffset_t real_OnSetScrollOffset = NULL;

/* 
   A static "render" buffer for this DrawScreen method.
   We'll copy from g_dpFrameBuffer.activeBuffer once at 
   the start of each call to MxDisplayPortDisplayPort_DrawScreen.
*/
static uint16_t g_renderBuffer[DP_FRAME_SIZE];
static int g_renderBufferInitialized = 0;
// Our own pointer array:

void* g_myCharItemBuffer[DP_FRAME_SIZE] = { NULL };
int g_base = 0;
int g_parent = 0;



/*
  Hooked function:
    MxDisplayPortDisplayPort_DrawScreen(int param_1, int param_2)

  param_1 + 0x1268 => "enabled" flag
  param_1 + 0x70   => row count
  param_1 + 0x74   => col count
  param_1 + 0x84   => base pointer for CharItemsAt/Create
  param_1 + 0x112c => pointer for the FontGroup
*/

void getScrollOffset(uint16_t glyphID, int *offsetXY)
{
    int effectiveGlyph;
    int pageIndex;
    int localIndex;
    
    if (fontPages == 1) {
        effectiveGlyph = glyphID % 256;
        pageIndex = 0;
    } else {
        int totalGlyphs = fontPages * 256;
        effectiveGlyph = glyphID % totalGlyphs;
        pageIndex = effectiveGlyph / 256;
    }
    
    localIndex = effectiveGlyph % 256;
    int col = (localIndex % 16) + (pageIndex * 16);
    int row = localIndex / 16;
    offsetXY[0] = -col * g_curTileW;
    offsetXY[1] = -row * g_curTileH;
}

// Safe index check, returning pointer to the slot
static inline void** getCharItemSlot(int col, int row)
{
    // Linear index
    int index = row * DP_MAX_COLS + col;
    if (index < 0 || index >= DP_FRAME_SIZE)
        return NULL;
    return &g_myCharItemBuffer[index];
}


void* my_MxDisplayPortCharItems_At(int base /*ignored*/, int col, int row)
{
    // We ignore the 'base' pointer's old logic.
    void** slot = getCharItemSlot(col, row);
    if (!slot) {
        // index out of range => no item
        return NULL;
    }
    return *slot;
}


#include <stdint.h>
#include <dlfcn.h>

// Declaration of our replacement function.
// param_1: pointer to the container (originally passed as param_1 + 0x84 in the draw loop)
// param_2: the parent group pointer (or object) in which the new item will be added
// param_3: the column index
// param_4: the row index
void* my_MxDisplayPortCharItems_Create(int base, int parentGroup, int col, int row)
{
    void** slot = getCharItemSlot(col, row);
    if (!slot) {
        // Out of range
        return NULL;
    }

    // If something already exists at this slot, just return it
    if (*slot != NULL) {
        return *slot;
    }

    // ---------- Create the MxDisplayPortCharItem object ----------

    // 1) Resolve the needed symbols once
    static void* (*pEwNewObjectIndirect)(void* vmt, int) = NULL;
    static void (*pMxDisplayPortCharItems_getBounds)(
        void* outBounds, int param_1, int param_2, int col, int row
    ) = NULL;
    static void (*pCoreRectView__OnSetBounds)(void* obj, int x1, int y1, int x2, int y2) = NULL;
    static void (*pViewsGroup_OnSetGroup)(void* obj, int group) = NULL;
    static void (*pCoreGroup__Add)(int parentGroup, void* obj, int) = NULL;
    static void* vmt_MxDisplayPortCharItem = NULL;

    if (!pEwNewObjectIndirect) {
        pEwNewObjectIndirect = dlsym(RTLD_NEXT, "EwNewObjectIndirect");
        pMxDisplayPortCharItems_getBounds = dlsym(RTLD_NEXT, "MxDisplayPortCharItems_getBounds");
        pCoreRectView__OnSetBounds = dlsym(RTLD_NEXT, "CoreRectView__OnSetBounds");
        pViewsGroup_OnSetGroup = dlsym(RTLD_NEXT, "ViewsGroup_OnSetGroup");
        pCoreGroup__Add = dlsym(RTLD_NEXT, "CoreGroup__Add");
        vmt_MxDisplayPortCharItem = dlsym(RTLD_NEXT, "__vmt_MxDisplayPortCharItem");
    }

    // 2) Actually create the object
    void* newObj = pEwNewObjectIndirect(vmt_MxDisplayPortCharItem, 0);
    if (!newObj) {
        return NULL; // out of memory or something
    }

    // 3) Write it into our array
    *slot = newObj;

    // 4) Compute geometry via MxDisplayPortCharItems_getBounds
    int bounds[4] = {0, 0, 0, 0};
    pMxDisplayPortCharItems_getBounds(bounds, base, parentGroup, col, row);
    //printf("Create MX thingy row: %d col: %d\n", row, col);

    // 5) Apply geometry
    pCoreRectView__OnSetBounds(newObj, bounds[0], bounds[1], bounds[2], bounds[3]);

    // 6) Set the group pointer
    //   The original code does: group = *(int*)( param_1 + 0x10A4 );
    //   But we can do exactly what the original would do:
    int group = *(int*)(base + 0x10A4); 
    pViewsGroup_OnSetGroup(newObj, group);

    // 7) Add to the parent group
    pCoreGroup__Add(parentGroup, newObj, 0);

    // Return the new object
    return newObj;
}



void MxDisplayPortDisplayPort_DrawScreen(int param_1, int param_2)
{



    if (orig_func == NULL) {
        orig_func = (orig_MxDisplayPortDisplayPort_DrawScreen_t)dlsym(RTLD_NEXT, "MxDisplayPortDisplayPort_DrawScreen");
        if (!orig_func) {
            return;
        }
    }

    int font_width;
    int font_height; // Uninitialized, will be set by check_external_resource()
    if (!externalResourceValid) {  // Pass addresses using &
        return orig_func(param_1, param_2);
    }else{
    }



    // 1) Check if drawing is enabled
    if (*(char *)(param_1 + 0x1268) == '\0') {
        return orig_func(param_1, param_2);
    }



    // 2) Resolve real_setConfig if needed
    if (!real_setConfig) {
        real_setConfig = (SetConfig_t)dlsym(RTLD_NEXT, "MxDisplayPortDisplayPort_setConfig");
        if (!real_setConfig) {
            return orig_func(param_1, param_2);
        }
    }
    // 2a) Call setConfig
    real_setConfig(param_1);





    // 3) Determine row/col dimensions from param_1
    int rows = g_curRows;
    int cols = g_curCols;
    if (rows <= 0 || cols <= 0) {
        return orig_func(param_1, param_2);
    }
    // clamp to our buffer max if the firmware ever sets them too large
    if (rows > DP_MAX_ROWS) rows = DP_MAX_ROWS;
    if (cols > DP_MAX_COLS) cols = DP_MAX_COLS;

    // 4) Initialize our DP buffers if not done
    dpBufferInitialize(); // ensures g_dpFrameBuffer is inited

    // 5) If this is the first time, init our local renderBuffer too
    if (!g_renderBufferInitialized) {
        memset(g_renderBuffer, 0, sizeof(g_renderBuffer));
        g_renderBufferInitialized = 1;
    }

    // 6) Lock once, copy the entire ACTIVE buffer -> local render buffer
    pthread_mutex_lock(&g_dpFrameBuffer.lock);
    memcpy(g_renderBuffer, g_dpFrameBuffer.activeBuffer, sizeof(g_renderBuffer));
    pthread_mutex_unlock(&g_dpFrameBuffer.lock);


    /* 7) If recording is active and grid size equals FRAME_SIZE (1320), record the frame.
     *    (The ring buffer is declared in ring_buffer.h.)
     */
    if (recording_active && rows * cols <= FRAME_SIZE) {
        RingBufferEntry entry;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        entry.delta_time = (uint32_t)(((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000)) - recording_start_time);

        /* Copy the entire render buffer into the ring buffer entry.
         * Note: FRAME_SIZE is defined in ring_buffer.h as 1320.
         */
        memcpy(entry.osd_frame_data, g_renderBuffer, FRAME_SIZE * sizeof(uint16_t));

        /* Lock the ring buffer, update head/tail, and signal waiting threads. */
        pthread_mutex_lock(&ring_buffer.mutex);
        if (((ring_buffer.head + 1) % RING_BUFFER_SIZE) == ring_buffer.tail) {
            /* Buffer full: advance tail to overwrite the oldest entry */
            ring_buffer.tail = (ring_buffer.tail + 1) % RING_BUFFER_SIZE;
        }
        ring_buffer.entries[ring_buffer.head] = entry;
        ring_buffer.head = (ring_buffer.head + 1) % RING_BUFFER_SIZE;
        pthread_cond_signal(&ring_buffer.not_empty);
        pthread_mutex_unlock(&ring_buffer.mutex);
    }

    // 8) Loop over the local "renderBuffer" to set each cell
    //    This avoids 1320 lock/unlock calls.
    // Resolve external functions if needed
    if (!real_CharItemsAt) {
        real_CharItemsAt = (CharItemsAt_t)dlsym(RTLD_NEXT, "MxDisplayPortCharItems_At");
    }
    if (!real_CharItemsCreate) {
        real_CharItemsCreate = (CharItemsCreate_t)dlsym(RTLD_NEXT, "MxDisplayPortCharItems_Create");
    }
    if (!real_GetScrollOffset) {
        real_GetScrollOffset = (GetScrollOffset_t)dlsym(RTLD_NEXT, "MxDisplayPortFontGroup_GetScrollOffset");
    }
    if (!real_OnSetScrollOffset) {
        real_OnSetScrollOffset = (OnSetScrollOffset_t)dlsym(RTLD_NEXT, "ViewsGroup_OnSetScrollOffset");
    }

    // Check if any are still NULL
    if (!real_CharItemsAt || !real_CharItemsCreate ||
        !real_GetScrollOffset || !real_OnSetScrollOffset) 
    {
        return orig_func(param_1, param_2);
    }


    *(int*)(param_1 + 0x78) = g_curTileW; // tile width
    *(int*)(param_1 + 0x7C) = g_curTileH; // tile height
    *(int*)(param_1 + 0x70) = g_curRows;  // number of rows
    *(int*)(param_1 + 0x74) = g_curCols;  // number of columns

    //printf("Print rows %d and cols %d before draw\n", rows, cols);
    g_base = param_1 + 0x84;
    g_parent =  param_1;

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            //printf("start\n");
            // 8a) Get or create the "char item" object
            void* charItemObj = my_MxDisplayPortCharItems_At(param_1 + 0x84, col, row);
            if (!charItemObj) {
                //printf("%d %d this is happening1\n",row,col);
                charItemObj = my_MxDisplayPortCharItems_Create(param_1 + 0x84, param_1, col, row);
                if (!charItemObj) {
                    //printf("%d %d this is happening\n",row,col);
                    //return orig_func(param_1, param_2);
                    continue;
                }
            }
            //printf("done\n");
            // 8b) Retrieve glyph from our local buffer
            uint16_t glyphID = g_renderBuffer[row * cols + col];
            



            // 8c) Compute offset for the glyph
            int offsetXY[2] = {0,0};

            //real_GetScrollOffset(offsetXY, param_1 + 0x112c, (unsigned int)glyphID);
            getScrollOffset(glyphID , offsetXY);

            // 8d) Apply offset
            real_OnSetScrollOffset((int)charItemObj, offsetXY[0], offsetXY[1]);
        }
    }
    // Done
    return;
}

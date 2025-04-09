#define _GNU_SOURCE
#include "o3-custom-fonts.h"
#include "font_list.h"  // This gives access to g_fontList, fontSelection, etc.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include <time.h>
#include <dlfcn.h>
#include "displayport_buffer.h"

//---------------------------------------------------------------------
// Global variables for hooking (others are now in font_list.h)
//---------------------------------------------------------------------
BmpData g_bmpData = {0, 0, NULL};
static void*   g_customResource = NULL;
static uint32_t cachedCustomResourceID = 0;
static uint16_t *g_externalResourceLiteral = NULL;

// Function pointer variables for hooking:
static EwLoadResource_t         real_EwLoadResource = NULL;
static EwNewObjectIndirect_t    real_EwNewObjectIndirect = NULL;
static ResourcesExternBitmap__Init_t  real_ResourcesExternBitmap__Init = NULL;
static EwNewString_t            real_EwNewString = NULL;
static CoreView__GetRoot_t      real_CoreView__GetRoot = NULL;
static EwCreateBitmap_t         real_EwCreateBitmap = NULL;
static EwLockBitmap_t           real_EwLockBitmap = NULL;
static EwUnlockBitmap_t         real_EwUnlockBitmap = NULL;
static EwSetRectH_t             real_EwSetRectH = NULL;
static EwSetRectW_t             real_EwSetRectW = NULL;
static ResourcesExternBitmap_OnSetName_t real_ResourcesExternBitmap_OnSetName = NULL;
static EwFreeBitmap_t           real_EwFreeBitmap = NULL;
static ResourcesExternBitmap__Done_t real_ResourcesExternBitmap__Done = NULL;
static MxDisplayPortCharItems_ClearAll_t real_MxDisplayPortCharItems_ClearAll = NULL;
static CoreGroup__Remove_t      real_CoreGroup__Remove = NULL;


static void* vmt_ResourcesExternBitmap = NULL;

// Local hooking variables:
static bool g_symbolsResolved = false;
static int g_specialHandle = 0;
static int g_specialWidth = 0;
static int g_specialHeight = 0;
static int* g_lockedPtr = NULL;
static int g_lockedHandle = 0;

// Fixed frame size for recording:
static int actualFrameSize = 0;

// Keypress variables for font switching:
#define INPUT_FILENAME "/dev/input/event0"
static int key_fd = -1;
static struct pollfd key_pfd = { .fd = -1, .events = POLLIN };
static bool backButtonPressed = false;
static struct timespec backButtonTime;

//---------------------------------------------------------------------
// resolveSymbols(): One-time symbol resolution using dlsym()
//--------------------------------------------------------------------- 
static void resolveSymbols(void)
{
    if (g_symbolsResolved)
        return;
    real_EwLoadResource = (EwLoadResource_t)dlsym(RTLD_NEXT, "EwLoadResource");
    real_CoreView__GetRoot = (CoreView__GetRoot_t)dlsym(RTLD_NEXT, "CoreView__GetRoot");
    vmt_ResourcesExternBitmap = dlsym(RTLD_DEFAULT, "__vmt_ResourcesExternBitmap");
    real_EwNewObjectIndirect = (EwNewObjectIndirect_t)dlsym(RTLD_NEXT, "EwNewObjectIndirect");
    real_ResourcesExternBitmap__Init = (ResourcesExternBitmap__Init_t)dlsym(RTLD_NEXT, "ResourcesExternBitmap__Init");
    real_EwNewString = (EwNewString_t)dlsym(RTLD_NEXT, "EwNewString");
    real_ResourcesExternBitmap_OnSetName = (ResourcesExternBitmap_OnSetName_t)dlsym(RTLD_NEXT, "ResourcesExternBitmap_OnSetName");
    real_EwCreateBitmap = (EwCreateBitmap_t)dlsym(RTLD_NEXT, "EwCreateBitmap");
    real_EwLockBitmap = (EwLockBitmap_t)dlsym(RTLD_NEXT, "EwLockBitmap");
    real_EwUnlockBitmap = (EwUnlockBitmap_t)dlsym(RTLD_NEXT, "EwUnlockBitmap");
    real_EwSetRectH = (EwSetRectH_t)dlsym(RTLD_NEXT, "EwSetRectH");
    real_EwSetRectW = (EwSetRectW_t)dlsym(RTLD_NEXT, "EwSetRectW");
    real_EwFreeBitmap = (EwFreeBitmap_t)dlsym(RTLD_NEXT, "EwFreeBitmap");
    real_ResourcesExternBitmap__Done = (ResourcesExternBitmap__Done_t)dlsym(RTLD_NEXT, "ResourcesExternBitmap__Done");
    real_MxDisplayPortCharItems_ClearAll = (MxDisplayPortCharItems_ClearAll_t)dlsym(RTLD_NEXT, "MxDisplayPortCharItems_ClearAll");
    real_CoreGroup__Remove = (CoreGroup__Remove_t)dlsym(RTLD_NEXT, "CoreGroup__Remove");
    g_symbolsResolved = true;
}




void my_MxDisplayPortCharItems_ClearAll(int parentPtr)
{
    // Loop through your entire array
    for (int i = 0; i < DP_FRAME_SIZE; i++)
    {   
        // If this slot contains an item pointer
        if (g_myCharItemBuffer[i] != NULL)
        {
            // We want to remove that item from the parent. 
            // childPtr is cast to int because 
            // the original function signature is (int, int).
            int childPtr = (int)g_myCharItemBuffer[i];
            // Call the hooked function (which in turn calls the real function).
            // or you can call real_CoreGroup__Remove directly if you want to bypass the hook.
            real_CoreGroup__Remove(parentPtr, childPtr);
            // Clear the slot
            g_myCharItemBuffer[i] = NULL;
        }
    }
}



//---------------------------------------------------------------------
// convert_to_utf16(): Convert an ASCII string to UTF-16
//---------------------------------------------------------------------
uint16_t* convert_to_utf16(const char* str)
{
    size_t len = strlen(str);
    uint16_t* out = malloc((len + 1) * sizeof(uint16_t));
    if (!out)
        return NULL;
    for (size_t i = 0; i < len; i++) {
        out[i] = (uint16_t)str[i];
    }
    out[len] = 0;
    return out;
}

//---------------------------------------------------------------------
// checkKeypress(): Monitor input for font switching.
//---------------------------------------------------------------------
void checkKeypress(void)
{
    if (key_fd < 0) {
        key_fd = open(INPUT_FILENAME, O_RDONLY | O_NONBLOCK);
        if (key_fd < 0)
            return;
        else {
            key_pfd.fd = key_fd;
        }
    }
    int ret = poll(&key_pfd, 1, 0);
    if (ret > 0 && (key_pfd.revents & POLLIN)) {
        struct input_event ev;
        while (read(key_fd, &ev, sizeof(ev)) > 0) {
            if (ev.code == 201) {
                if (ev.value == 1) {
                    if (!backButtonPressed) {
                        backButtonPressed = true;
                        clock_gettime(CLOCK_MONOTONIC, &backButtonTime);
                    }
                } else if (ev.value == 0) {
                    backButtonPressed = false;
                }
            }
        }
    }
    if (backButtonPressed) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - backButtonTime.tv_sec) +
                         (now.tv_nsec - backButtonTime.tv_nsec) / 1e9;
        if (elapsed >= 5.0) {
            // Re-scan fonts and cycle the font selection.
            initFontList();  // re-scan directories

            const char* settingsPath = (g_fontCount > 0 &&
                    strstr(g_fontList[0].fullPath, "/storage/sdcard0/") != NULL)
                    ? "/storage/sdcard0/fonts/settings"
                    : "/opt/default_fonts/settings";
            int oldSel = 0, oldDef = -1;
            readSettingsFile(settingsPath, &oldSel, &oldDef);

            if (g_fontCount > 0)
                fontSelection = (fontSelection + 1) % g_fontCount;


            writeSettingsFile(settingsPath, fontSelection, oldDef);

            // Force reload of the custom resource and font data.
            g_customResource = NULL;

            if (g_parent) {
                my_MxDisplayPortCharItems_ClearAll(g_parent);
            }

            memset(g_myCharItemBuffer, 0, sizeof(g_myCharItemBuffer));
            freeCurrentFontData();
            clock_gettime(CLOCK_MONOTONIC, &backButtonTime);
        }
    }
}

//---------------------------------------------------------------------
// EwLoadResource Hook
//---------------------------------------------------------------------
int* EwLoadResource(int param_1, unsigned int param_2)
{
    resolveSymbols();
    if (!real_EwLoadResource)
        return NULL;
    int* origRes = real_EwLoadResource(param_1, param_2);
    if (!origRes)
        return NULL;
    
    // Only override if the original resource dimensions match the expected OSD font.
    int width  = *(int*)((uint8_t*)origRes + 0x20);
    int height = *(int*)((uint8_t*)origRes + 0x24);
    if (width != ORIGINAL_WIDTH || height != ORIGINAL_HEIGHT)
        return origRes; // Not our target.
    
    // Allow font switching.
    checkKeypress();
    
    // If a custom resource is already loaded and valid, reuse it.
    if (g_customResource) {
        uint32_t currentID = *(uint32_t*)g_customResource;
        if (currentID == cachedCustomResourceID){
            return (int*)g_customResource;
        }else{


            // Re-scan fonts and cycle the font selection.
            initFontList();  // re-scan directories
            // Force reload of the custom resource and font data.
            g_customResource = NULL;
            if (g_parent) {
                //my_MxDisplayPortCharItems_ClearAll(g_parent); crashes UI
            }
            memset(g_myCharItemBuffer, 0, sizeof(g_myCharItemBuffer));
            freeCurrentFontData();

        }
    }


    // Check if a valid external font is available.
    int w = 0, h = 0;
    if (!check_external_resource(&w, &h))
        return origRes;

    // If our loaded font data is not present, load it based on the current selection.
    if (!g_bmpData.data) {
        if (!loadFont(&g_fontList[fontSelection], &g_bmpData)) {
            externalResourceValid = false;
            return origRes;
        }
    }
        
    

    
    
    // Create the custom resource.
    if (!vmt_ResourcesExternBitmap)
        return origRes;
    int parent = real_CoreView__GetRoot(origRes);
    g_customResource = real_EwNewObjectIndirect((int*)vmt_ResourcesExternBitmap, 0);
    if (!g_customResource)
        return origRes;
    real_ResourcesExternBitmap__Init(g_customResource, (uint32_t)parent, 0, 0);
    
    // Build a template BMP path using our templates (e.g., template1page.bmp, etc.)
    int pagesWanted = fontPages;
    if (pagesWanted < 1) pagesWanted = 1;
    if (pagesWanted > 4) pagesWanted = 4;
    char templatePath[128];

    snprintf(templatePath, sizeof(templatePath),
             "file:////opt/font_templates/template%dpage%s.bmp?width=%d,height=%d",
             pagesWanted, 
             (h == 864) ? "SD" : (h == 624) ? "HD" : (h == 576) ? "UHD" : "",
             w, h);

    g_externalResourceLiteral = convert_to_utf16(templatePath);
    if (!g_externalResourceLiteral) {
        externalResourceValid = false;
        return origRes;
    }
    uint32_t managedFn = real_EwNewString((unsigned short*)g_externalResourceLiteral);
    free(g_externalResourceLiteral);
    g_externalResourceLiteral = NULL;
    
    real_ResourcesExternBitmap_OnSetName(g_customResource, (unsigned short*)managedFn);
    cachedCustomResourceID = *(uint32_t*)g_customResource;
    return (int*)g_customResource;
}

//---------------------------------------------------------------------
// EwCreateBitmap Hook
//---------------------------------------------------------------------
int EwCreateBitmap(int a0, int w, int h, int a3, int a4)
{
    resolveSymbols();
    if (!real_EwCreateBitmap)
        return 0;
    int handle = real_EwCreateBitmap(a0, w, h, a3, a4);
    if (externalResourceValid && handle != 0) {
        g_specialHandle = handle;
        g_specialWidth  = w;
        g_specialHeight = h;
    }
    return handle;
}

//---------------------------------------------------------------------
// EwLockBitmap Hook
//---------------------------------------------------------------------
int* EwLockBitmap(int bitmapHandle, int x, int y, int w, int h, int something, int something2)
{
    resolveSymbols();
    if (!real_EwLockBitmap)
        return NULL;
    int* locked = real_EwLockBitmap(bitmapHandle, x, y, w, h, something, something2);
    if (bitmapHandle == g_specialHandle && locked) {
        g_lockedPtr = locked;
        g_lockedHandle = bitmapHandle;
    }
    return locked;
}

//---------------------------------------------------------------------
// EwUnlockBitmap Hook
//---------------------------------------------------------------------
void EwUnlockBitmap(int* lockedPtr)
{
    resolveSymbols();
    if (!real_EwUnlockBitmap)
        return;
    if (lockedPtr && lockedPtr == g_lockedPtr &&
        g_lockedHandle == g_specialHandle && g_specialHandle != 0)
    {
        int basePtr = lockedPtr[0];
        int stride  = lockedPtr[2];
        int w = g_specialWidth;
        int h = g_specialHeight;
        if (!g_bmpData.data) {
            if (!loadFont(&g_fontList[fontSelection], &g_bmpData))
                goto finalize;
        }
        if (g_bmpData.width != w || g_bmpData.height != h){
            goto finalize;
        }

        for (int row = 0; row < h; row++) {
            uint32_t* rowPtr = (uint32_t*)(basePtr + stride * row);
            int bmpRow = g_bmpData.height - 1 - row;
            uint8_t* bmpScan = &g_bmpData.data[bmpRow * g_bmpData.width * 4];
            for (int col = 0; col < w; col++) {
                uint8_t B = bmpScan[col * 4 + 0];
                uint8_t G = bmpScan[col * 4 + 1];
                uint8_t R = bmpScan[col * 4 + 2];
                uint8_t A = bmpScan[col * 4 + 3];
                uint32_t pix = (A << 24) | (R << 16) | (G << 8) | B;
                rowPtr[col] = pix;
            }
        }
    }
finalize:
    real_EwUnlockBitmap(lockedPtr);
    if (lockedPtr == g_lockedPtr) {
        g_lockedPtr = NULL;
        g_lockedHandle = 0;
    }
}
//---------------------------------------------------------------------
// EwSetRectH and EwSetRectW Hooks
//---------------------------------------------------------------------
void EwSetRectH(void* rect, int p1, int p2, int p3, int p4, int height)
{
    resolveSymbols();
    if (!real_EwSetRectH)
        return;
    if (height == ORIGINAL_HEIGHT && externalResourceValid)
        height = 16 * g_curTileH;
    real_EwSetRectH(rect, p1, p2, p3, p4, height);
}

void EwSetRectW(void* rect, int p1, int p2, int p3, int p4, int width)
{
    resolveSymbols();
    if (!real_EwSetRectW)
        return;
    if (width == ORIGINAL_WIDTH && externalResourceValid)
        width = 16 * g_curTileW * fontPages;
    real_EwSetRectW(rect, p1, p2, p3, p4, width);
}

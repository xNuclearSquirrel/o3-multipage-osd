#define _GNU_SOURCE
#include "o3-custom-fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>  // for opendir, readdir, closedir
#include <sys/stat.h> // for stat, if needed

#include <ctype.h>

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include <time.h>

#define INPUT_FILENAME "/dev/input/event0"
//---------------------------------------------------------------------
// Global Definitions & Constants (Internal)
//---------------------------------------------------------------------

// The external file path is defined once.
static const char *externalBmpPath = NULL;
static const char* FONTS_DIR = "/storage/sdcard0/fonts/";

//---------------------------------------------------------------------
// Global State for External Resource
//---------------------------------------------------------------------
bool externalResourceValid   = false;
int fontPages = 1;
char names[MAX_NAMES][MAX_STRING_LENGTH];
static char externalBmpPathBuffer[MAX_STRING_LENGTH] = {0};


static int fd = -1;
static struct pollfd pfd = { .fd = -1, .events = POLLIN };
static bool backButtonPressed = false;
static struct timespec backButtonTime;
int fontSelection = 0;
static int fontcount = 0;  // global



// These will hold the external file’s dimensions (read from the BMP header).
static int  g_externalWidth  = 0;
static int  g_externalHeight = 0;
// The “managed” filename string (UTF‑16 encoded) that is passed to the system.
static uint16_t *g_externalResourceLiteral = NULL;

// When ourthe custom (external) resource has been created, it is cached.
static void *g_customResource = NULL;
static uint32_t cachedCustomResourceID = 0;

// The special bitmap creation and lock/unlock hooks use these globals.
static int g_specialHandle = 0;
static int g_specialWidth  = 0;
static int g_specialHeight = 0;
static int* g_lockedPtr   = NULL;
static int  g_lockedHandle = 0;

// Cached BMP image data – once loaded it is reused.
static BmpData g_bmpData = {0, 0, NULL};


//---------------------------------------------------------------------
// Function Pointer Variables for Runtime‑Resolved Functions
//---------------------------------------------------------------------
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

static EwFreeBitmap_t real_EwFreeBitmap = NULL;
static ResourcesExternBitmap__Done_t real_ResourcesExternBitmap__Done = NULL;


// A pointer to the virtual method table for external bitmaps.
static void *vmt_ResourcesExternBitmap = NULL;


/**
 * Attempts to read two integers from the file: sel def
 * Example file content: "3 -1" or "5 2"
 *
 * Returns true on success, false if file doesn't exist,
 * can't be opened, or doesn't contain two valid integers.
 */
bool readSettingsFile(const char* path, int* outSel, int* outDef)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;  // File missing or not readable

    int s, d;
    int n = fscanf(f, "%d %d", &s, &d);
    fclose(f);

    if (n != 2)
        return false;  // Could not parse exactly two ints

    *outSel = s;
    *outDef = d;
    return true;
}

/**
 * Writes two integers to the file as: "sel def\n"
 * Overwrites any existing file. Returns true if successful.
 */
bool writeSettingsFile(const char* path, int sel, int def)
{
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    fprintf(f, "%d %d\n", sel, def);
    fclose(f);
    return true;
}



//---------------------------------------------------------------------
// Helper: Convert an ASCII string into a newly allocated UTF‑16 string.
// (Assumes 7‑bit ASCII.)
//---------------------------------------------------------------------
uint16_t* convert_to_utf16(const char* str) {
    size_t len = strlen(str);
    uint16_t* out = malloc((len + 1) * sizeof(uint16_t));
    if (!out) return NULL;
    for (size_t i = 0; i < len; i++) {
        out[i] = (uint16_t)str[i];
    }
    out[len] = 0;
    return out;
}

//---------------------------------------------------------------------
// Helper: Load the BMP file into memory (if not already loaded).
// Returns true on success. Does some checks (not really necessary because of check_external_resource())
//---------------------------------------------------------------------
bool LoadBmp32(const char* path, BmpData* out) {
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return false;
    uint8_t header[54];
    if (fread(header, 1, 54, fp) < 54) {
        fclose(fp);
        return false;
    }
    if (header[0] != 'B' || header[1] != 'M') {
        fclose(fp);
        return false;
    }
    int32_t w = *(int32_t*)&header[18];
    int32_t h = *(int32_t*)&header[22];
    int16_t bpp = *(int16_t*)&header[28];
    uint32_t compression = *(uint32_t*)&header[30];
    uint32_t pixelOffset = *(uint32_t*)&header[10];
    if (bpp != 32 || compression != 0) {
        fclose(fp);
        return false;
    }
    size_t imageSize = (size_t)w * (size_t)h * 4;
    uint8_t* data = malloc(imageSize);
    if (!data) {
        fclose(fp);
        return false;
    }
    fseek(fp, pixelOffset, SEEK_SET);
    size_t readCount = fread(data, 1, imageSize, fp);
    fclose(fp);
    if (readCount < imageSize) {
        free(data);
        return false;
    }
    out->width  = w;
    out->height = h;
    out->data   = data;

    return true;
}


static void initializeFontSelectionIfNeeded(void)
{
    static bool done = false;
    if (done) return;  // Only once
    
    int tempSel = 0;
    int tempDef = -1; 
    bool ok = readSettingsFile("/storage/sdcard0/fonts/settings", &tempSel, &tempDef);
    if (ok) {
        if (tempDef >= 0)
            fontSelection = tempDef;
        else
            fontSelection = tempSel;
    } else {
        fontSelection = 0;  // fallback if file is missing/corrupt
    }

    // Now clamp to [0..count-1]
    if (fontcount > 0) {
        fontSelection = fontSelection % fontcount;
    }

    done = true;
}



//---------------------------------------------------------------------
// Helper: Check if there is a font.txt and take the filename from there.
// You can have several fonts on the sd, just put the name of the one you ant in font.txt.
// If empty use system font (as away to disable the mod); if nonexistent or wrong format use standard font (HD_Font.bmp).
// Also checks the BMP header for existence, dimensions, 32 bpp and no compression. Only non compressed 32bit (with alpha) v3 bmps will work.
// Can be made in magick or with photoshop.
// Returns true on success and sets *outWidth and *outHeight.
//---------------------------------------------------------------------




bool check_external_resource(int *outWidth, int *outHeight)
{
    
    
    // 1) Try to open the fonts directory.
    DIR *dirp = opendir(FONTS_DIR);
    if (!dirp) {
        // Attempt to create the directory if it doesn’t exist.
        int result = mkdir(FONTS_DIR, 0755); 
        if (result != 0)
        {
            // If creation fails, return false.
            // You could log an error with errno if desired.
            return false;
        }
        // Directory is created, but it’s presumably empty, so return false.
        return false;
    }


    int count = 0;
    struct dirent *dp;

    // 2) Read directory entries. Collect .bmp filenames into 'names[]'
    while ((dp = readdir(dirp)) != NULL && count < MAX_NAMES)
    {
        // Skip "." and ".." entries, or any directory.
        if (dp->d_name[0] == '.')
            continue;

        // We only care about files that end with ".bmp" (case-sensitive).
        const char *ext = strstr(dp->d_name, ".bmp");
        if (ext && ext[4] == '\0')  // ensure ".bmp" is at the end
        {
            // Store the entire filename (including extension).
            strncpy(names[count], dp->d_name, MAX_STRING_LENGTH - 1);
            names[count][MAX_STRING_LENGTH - 1] = '\0';
            count++;
        }
    }

    closedir(dirp);

    fontcount = count;
    // If no .bmp files were found, return false.
    if (count == 0) {
        return false;
    }

    initializeFontSelectionIfNeeded();

    // 3) Pick the file based on fontSelection.
    fontSelection = fontSelection % fontcount;

    snprintf(externalBmpPathBuffer, MAX_STRING_LENGTH,
             "%s/%s",FONTS_DIR, names[fontSelection]);

    // 4) Open the chosen BMP file and check its header.
    FILE *fp = fopen(externalBmpPathBuffer, "rb");
    if (!fp) {
        return false;
    }

    uint8_t header[54];
    if (fread(header, 1, 54, fp) < 54) {
        fclose(fp);
        return false;
    }
    // Basic BMP signature check
    if (header[0] != 'B' || header[1] != 'M') {
        fclose(fp);
        return false;
    }
    int32_t w = *(int32_t *)&header[18];
    int32_t h = *(int32_t *)&header[22];
    int16_t bpp = *(int16_t *)&header[28];
    uint32_t compression = *(uint32_t *)&header[30];
    fclose(fp);

    // 5) Validate that it’s a 32‑bpp uncompressed BMP.
    if (compression != 0) {
        return false;
    }
    if (bpp != 32) {
        return false;
    }

    // 6) Check our tile constraints.
    // We expect 'w' to be multiple of (16 * TILE_WIDTH),
    // and 'h' to equal (16 * TILE_HEIGHT).
    if ((w % (16 * TILE_WIDTH) != 0) || (h != (16 * TILE_HEIGHT))) {
        return false;
    }

    // # of font pages:
    fontPages = w / TILE_WIDTH / 16;

    // 7) If all checks pass, set externalBmpPath and output dims.
    externalBmpPath = externalBmpPathBuffer;
    *outWidth  = w;
    *outHeight = h;
    return true;
}



void checkKeypress(void) {
    if (fd < 0) {
        fd = open(INPUT_FILENAME, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            return;
        } else {
            pfd.fd = fd;
        }
    }
    int ret = poll(&pfd, 1, 0);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        struct input_event ev;
        while (read(fd, &ev, sizeof(ev)) > 0) {
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
        // read oldSel, oldDef from file if it exists
            int oldSel = 0;
            int oldDef = -1;
            bool ok = readSettingsFile("/storage/sdcard0/fonts/settings", &oldSel, &oldDef);

            if (fontcount > 0) {
                fontSelection = (fontSelection + 1) % fontcount;
            }
            

            if (!ok) {
                oldDef = -1;  // if file missing/corrupt, we won't preserve a default
            }

            // write the new sel + old def
            writeSettingsFile("/storage/sdcard0/fonts/settings", fontSelection, oldDef);

            // Force reload the BMP so we see the new font 
            g_customResource = NULL;
            free(g_bmpData.data);
            g_bmpData.data = NULL;

            clock_gettime(CLOCK_MONOTONIC, &backButtonTime);
        }
    }

}

//---------------------------------------------------------------------
// Hook: EwLoadResource – Replace the original resource with our external
// one if the original is the standard resource and the external BMP file
// is valid. There might be a better way to do this, this method really justs looks for a resource with 416x390 dimensions because only the font file has it.
// But it might be safer to check instead if param_1 is the HD font object. Would be a bit complicated though. (The managed filename is built dynamically using the file’s
// dimensions.). This function will get called over and over again, but we load the external font only once or if the resource is replaced by the UI.
//---------------------------------------------------------------------
int* EwLoadResource(int param_1, unsigned int param_2)
{

    // Resolve our own hook first.
    if (!real_EwLoadResource) {
        real_EwLoadResource = (EwLoadResource_t)dlsym(RTLD_NEXT, "EwLoadResource");
        if (!real_EwLoadResource) {
            return NULL;
        }
    }
    int* origResource = real_EwLoadResource(param_1, param_2);
    if (!origResource)
        return NULL;

    // Resolve other required functions inline.
    if (!real_CoreView__GetRoot) {
        real_CoreView__GetRoot = (CoreView__GetRoot_t)dlsym(RTLD_NEXT, "CoreView__GetRoot");
        if (!real_CoreView__GetRoot) {
            return origResource;
        }
    }
    if (!vmt_ResourcesExternBitmap) {
        vmt_ResourcesExternBitmap = dlsym(RTLD_DEFAULT, "__vmt_ResourcesExternBitmap");
        if (!vmt_ResourcesExternBitmap) {
            return origResource;
        }
    }
    if (!real_EwNewObjectIndirect) {
        real_EwNewObjectIndirect = (EwNewObjectIndirect_t)dlsym(RTLD_NEXT, "EwNewObjectIndirect");
        if (!real_EwNewObjectIndirect) {
            return origResource;
        }
    }
    if (!real_ResourcesExternBitmap__Init) {
        real_ResourcesExternBitmap__Init = (ResourcesExternBitmap__Init_t)dlsym(RTLD_NEXT, "ResourcesExternBitmap__Init");
        if (!real_ResourcesExternBitmap__Init) {
            return origResource;
        }
    }
    if (!real_EwNewString) {
        real_EwNewString = (EwNewString_t)dlsym(RTLD_NEXT, "EwNewString");
        if (!real_EwNewString) {
            return origResource;
        }
    }
    if (!real_ResourcesExternBitmap_OnSetName) {
        real_ResourcesExternBitmap_OnSetName = (ResourcesExternBitmap_OnSetName_t)dlsym(RTLD_NEXT, "ResourcesExternBitmap_OnSetName");
        if (!real_ResourcesExternBitmap_OnSetName) {
            return origResource;
        }
    }
    
    // Read dimensions from the original resource.
    int width  = *(int*)((uint8_t*)origResource + 0x20);
    int height = *(int*)((uint8_t*)origResource + 0x24);
    
    // Only process the resource if it matches the standard dimensions.
    if (width == ORIGINAL_WIDTH && height == ORIGINAL_HEIGHT) {
        checkKeypress();
        // If our custom resource was already created, check its ID. The first 4 byte are some identifier I think. 
        // The problem is when the goggles lose connections I think the canvas resets and the font resource is gone.
        // The poitner is still pointing to a structure, but no OSD is rendered. However the ID also changes, so we can force a reload every time that happens.
        if (g_customResource != NULL) {
            uint32_t currentCustomID = *(uint32_t*)g_customResource;
            if (currentCustomID == cachedCustomResourceID) {
                //As long as the resource exists and the ID is the same, reuse it.
                return (int*)g_customResource;
            }
        }
        

        // Check the external resource once every time we reload it. You could remove the SD mit flight and it will just default to the system font.
        if (check_external_resource(&g_externalWidth, &g_externalHeight)) {
            externalResourceValid = true;
            char buf[MAX_STRING_LENGTH];
            // Build a managed string like: "file://<path>?width=<w>,height=<h>"
            snprintf(buf, MAX_STRING_LENGTH, "file://%s?width=%d,height=%d", externalBmpPath, g_externalWidth, g_externalHeight);
            //strings are 16bit apparently
            g_externalResourceLiteral = convert_to_utf16(buf);

            if (!g_externalResourceLiteral) {
                externalResourceValid = false;
            }
        } else {
            externalResourceValid = false;
        }

        // If external resource is not valid, return the original.
        if (!externalResourceValid) {
            //if the check fails we default to the system font
            return origResource;
        }
        // Obtain a valid parent pointer from the original resource. Maybe it would work also without.
        int parent = real_CoreView__GetRoot(origResource);
        
        // Create the custom resource.
        g_customResource = real_EwNewObjectIndirect((int*)vmt_ResourcesExternBitmap, 0);
        if (!g_customResource) {
            return origResource;
        }
        real_ResourcesExternBitmap__Init(g_customResource, (uint32_t)parent, 0, 0);
        
        // Create the managed string from our dynamic literal. This is really important, ResourcesExternBitmap_OnSetName will crash with unmanaged strings.
        uint32_t managedFilename = real_EwNewString((unsigned short*)g_externalResourceLiteral);

        // Now set the resource name.
        real_ResourcesExternBitmap_OnSetName(g_customResource, (unsigned short *)managedFilename);
        //ResourcesExternBitmap_load() is automatically called by ResourcesExternBitmap_OnSetName

        //we should be able to free the memory of the string literal
        free(g_externalResourceLiteral);
        g_externalResourceLiteral = NULL;

        // Store resource ID to compare next time.
        cachedCustomResourceID = *(uint32_t*)g_customResource;
        return (int*)g_customResource;
    }
    //EwLoadResource is called a lot so in most cases (unless we think it's the OSD font because of it's dimensionds) we just return the loaded resource.
    return origResource;
}

//---------------------------------------------------------------------
// Hook: EwCreateBitmap – Detect creation of the special (external)
// bitmap. When the GUI system creates a bitmap using the dimensions
// provided in our managed filename, we save the handle for later use.
// This is necassary because the built in bitmap loader doesn't handle transparency properly.
//---------------------------------------------------------------------
int EwCreateBitmap(int a0, int w, int h, int a3, int a4)
{
    if (!real_EwCreateBitmap) {
        real_EwCreateBitmap = (EwCreateBitmap_t)dlsym(RTLD_NEXT, "EwCreateBitmap");
        if (!real_EwCreateBitmap) {
            return 0;
        }
    }
    
    int handle = real_EwCreateBitmap(a0, w, h, a3, a4);
    
    
    if (externalResourceValid && w == g_externalWidth && h == g_externalHeight && handle != 0) {
        g_specialHandle = handle;
        g_specialWidth  = w;
        g_specialHeight = h;
    }
    return handle;
}

//---------------------------------------------------------------------
// Hook: EwLockBitmap – Track the locked pointer if it belongs to our
// external resource.
//---------------------------------------------------------------------
int* EwLockBitmap(int bitmapHandle, int x, int y, int w, int h, int something, int something2)
{
    if (!real_EwLockBitmap) {
        real_EwLockBitmap = (EwLockBitmap_t)dlsym(RTLD_NEXT, "EwLockBitmap");
        if (!real_EwLockBitmap) {
            return NULL;
        }
    }
    
    int* lockedInfo = real_EwLockBitmap(bitmapHandle, x, y, w, h, something, something2);
    if (bitmapHandle == g_specialHandle && lockedInfo) {
        g_lockedPtr   = lockedInfo;
        g_lockedHandle = bitmapHandle;
    }
    return lockedInfo;
}

//---------------------------------------------------------------------
// Hook: EwUnlockBitmap – Update the bitmap’s pixels from our external
// BMP file if the locked handle is the special external one.
// Technically we are replacing our loaded image with the same image, but here we set the correct alpha.
//---------------------------------------------------------------------
void EwUnlockBitmap(int* lockedPtr)
{
    if (!real_EwUnlockBitmap) {
        real_EwUnlockBitmap = (EwUnlockBitmap_t)dlsym(RTLD_NEXT, "EwUnlockBitmap");
        if (!real_EwUnlockBitmap) {
            return;
        }
    }
    
    if (lockedPtr && lockedPtr == g_lockedPtr &&
        g_lockedHandle == g_specialHandle && g_specialHandle != 0)
    {
        
        int basePtr = lockedPtr[0];
        int stride  = lockedPtr[2];
        int w = g_externalWidth;
        int h = g_externalHeight;
        
        if (!g_bmpData.data) {
            if (!LoadBmp32(externalBmpPath, &g_bmpData))
                goto UnlockAndCleanup;
        }
        if (g_bmpData.width < w || g_bmpData.height < h)
            goto UnlockAndCleanup;
        
        for (int row = 0; row < h; row++) {
            uint32_t* rowPtr = (uint32_t*)(basePtr + stride * row);
            int bmpRow = (h - 1 - row);
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
UnlockAndCleanup:
    real_EwUnlockBitmap(lockedPtr);
    if (lockedPtr == g_lockedPtr) {
        g_lockedPtr   = NULL;
        g_lockedHandle = 0;
    }
}

//---------------------------------------------------------------------
// Hook: EwSetRectH – Adjust the view’s height. If the height equals
// the original resource’s height then (when using an external image)
// substitute the external image’s height.
// This is what determines how many rows of tiles are found withing a font.
//---------------------------------------------------------------------
void EwSetRectH(void *rect, int p1, int p2, int p3, int p4, int height)
{
    if (!real_EwSetRectH) {
        real_EwSetRectH = (EwSetRectH_t)dlsym(RTLD_NEXT, "EwSetRectH");
        if (!real_EwSetRectH) {
            return;
        }
    }
    
    if (height == ORIGINAL_HEIGHT && externalResourceValid)
        height = g_externalHeight;
    real_EwSetRectH(rect, p1, p2, p3, p4, height);
}

void EwSetRectW(void *rect, int p1, int p2, int p3, int p4, int width)
{
    if (!real_EwSetRectW) {
        real_EwSetRectW = (EwSetRectW_t)dlsym(RTLD_NEXT, "EwSetRectW");
        if (!real_EwSetRectW) {
            return;
        }
    }

    if (width == ORIGINAL_WIDTH && externalResourceValid)
        width = g_externalWidth;
    real_EwSetRectW(rect, p1, p2, p3, p4, width);
}

#define _GNU_SOURCE
#include "o3-custom-fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

//---------------------------------------------------------------------
// Global Definitions & Constants (Internal)
//---------------------------------------------------------------------

// The external file path is defined once.
static const char *externalBmpPath = NULL;
static const char *standardBmpPath = "/storage/sdcard0/HD_Font.bmp";
static const char *variableBmpPath = "/storage/sdcard0/font.txt";

//---------------------------------------------------------------------
// Global State for External Resource
//---------------------------------------------------------------------
static bool externalResourceValid   = false;
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
static ResourcesExternBitmap_OnSetName_t real_ResourcesExternBitmap_OnSetName = NULL;

// A pointer to the virtual method table for external bitmaps.
static void *vmt_ResourcesExternBitmap = NULL;

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

//---------------------------------------------------------------------
// Helper: Check if there is a font.txt and take the filename from there.
// You can have several fonts on the sd, just put the name of the one you ant in font.txt.
// If empty use system font (as away to disable the mod); if nonexistent or wrong format use standard font (HD_Font.bmp).
// Also checks the BMP header for existence, dimensions, 32 bpp and no compression. Only non compressed 32bit (with alpha) v3 bmps will work.
// Can be made in magick or with photoshop.
// Returns true on success and sets *outWidth and *outHeight.
//---------------------------------------------------------------------
bool check_external_resource(int *outWidth, int *outHeight) {
    char varBuf[MAX_STRING_LENGTH];
    // First, check if the variable file exists.
    FILE *varfp = fopen(variableBmpPath, "r");
    if (varfp != NULL) {
        // Read one line from the variable file.
        if (fgets(varBuf, MAX_STRING_LENGTH, varfp) != NULL) {
            // Remove any trailing newline or carriage-return characters.
            size_t len = strlen(varBuf);
            while (len > 0 && (varBuf[len - 1] == '\n' || varBuf[len - 1] == '\r')) {
                varBuf[len - 1] = '\0';
                len--;
            }
            // If the file is empty, the mod is turned off.
            if (len == 0) {
                fclose(varfp);
                return false;
            }
            // Remove any extension (everything after the first period).
            char *dot = strchr(varBuf, '.');
            if (dot != NULL) {
                *dot = '\0';
            }
            // Build the full path: "/storage/sdcard0/<name>.bmp"
            char fullPath[MAX_STRING_LENGTH];
            snprintf(fullPath, MAX_STRING_LENGTH, "/storage/sdcard0/%s.bmp", varBuf);
            // Test if the constructed file exists.
            FILE *testfp = fopen(fullPath, "rb");
            if (testfp != NULL) {
                fclose(testfp);
                externalBmpPath = strdup(fullPath);
            } else {
                externalBmpPath = strdup(standardBmpPath);
            }
        } else {
            // If the file is empty, return false to turn off the mod.
            fclose(varfp);
            return false;
        }
        fclose(varfp);
    } else {
        // If the variable file does not exist, use the standard BMP path.
        externalBmpPath = strdup(standardBmpPath);
    }

    // Open the chosen BMP file and check its header.
    FILE *fp = fopen(externalBmpPath, "rb");
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
    int32_t w = *(int32_t *)&header[18];
    int32_t h = *(int32_t *)&header[22];
    int16_t bpp = *(int16_t *)&header[28];
    uint32_t compression = *(uint32_t *)&header[30];
    fclose(fp);
    if (compression != 0)
        return false;
    if (bpp != 32)
        return false;
    // The width should be the same as the original or it will mess up the font. the height can be any height (higher = more tiles) but should be at least the original 10 rows.
    if (!(w == ORIGINAL_WIDTH) || h < ORIGINAL_HEIGHT){
        return false;
    }
        
    *outWidth  = w;
    *outHeight = h;
    return true;
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

#ifndef O3_CUSTOM_FONTS_H
#define O3_CUSTOM_FONTS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// Global Definitions and Constants
//---------------------------------------------------------------------

// The original resource is identified by these dimensions.
#define ORIGINAL_WIDTH   416
#define ORIGINAL_HEIGHT  390
#define TILE_WIDTH   26
#define TILE_HEIGHT  39

// Maximum string length for our dynamic file name.
#define MAX_STRING_LENGTH 256
#define MAX_NAMES 10

extern bool externalResourceValid;
extern int fontPages;
extern int fontSelection;

//---------------------------------------------------------------------
// BMP Data Structure
//---------------------------------------------------------------------
typedef struct {
    int width;
    int height;
    uint8_t* data; // BGRA pixel data in memory
} BmpData;

//---------------------------------------------------------------------
// Function Pointer Typedefs (for runtime-resolved functions)
//---------------------------------------------------------------------
typedef int* (*EwLoadResource_t)(int param_1, unsigned int param_2);
typedef void* (*EwNewObjectIndirect_t)(int *templ, uint32_t param);
typedef void (*ResourcesExternBitmap__Init_t)(void *resource, uint32_t parent, uint32_t p3, uint32_t p4);
typedef uint32_t (*EwNewString_t)(unsigned short *param1);
typedef int (*CoreView__GetRoot_t)(int *view);
typedef int (*EwCreateBitmap_t)(int a0, int w, int h, int a3, int a4);
typedef int* (*EwLockBitmap_t)(int bitmapHandle, int x, int y, int w, int h, int something, int something2);
typedef void (*EwUnlockBitmap_t)(int* lockedPtr);
typedef void (*EwSetRectH_t)(void *rect, int p1, int p2, int p3, int p4, int height);
typedef void (*EwSetRectW_t)(void *rect, int p1, int p2, int p3, int p4, int width);
typedef void (*ResourcesExternBitmap_OnSetName_t)(void *param_1, const uint16_t *param_2);

typedef void (*EwFreeBitmap_t)(void* bitmapHandle);

typedef void (*ResourcesExternBitmap__Done_t)(void* resourcePtr);

//---------------------------------------------------------------------
// Hook Function Prototypes
//---------------------------------------------------------------------
int* EwLoadResource(int param_1, unsigned int param_2);
int  EwCreateBitmap(int a0, int w, int h, int a3, int a4);
int* EwLockBitmap(int bitmapHandle, int x, int y, int w, int h, int something, int something2);
void EwUnlockBitmap(int* lockedPtr);
void EwSetRectH(void *rect, int p1, int p2, int p3, int p4, int height);

//---------------------------------------------------------------------
// Helper Function Prototypes
//---------------------------------------------------------------------
uint16_t* convert_to_utf16(const char* str);
bool LoadBmp32(const char* path, BmpData* out);
bool check_external_resource(int *outWidth, int *outHeight);

#ifdef __cplusplus
}
#endif

#endif // O3_CUSTOM_FONTS_H

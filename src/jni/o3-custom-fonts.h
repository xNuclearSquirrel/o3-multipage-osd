#ifndef O3_CUSTOM_FONTS_H
#define O3_CUSTOM_FONTS_H

#include <stdint.h>
#include <stdbool.h>


//---------------------------------------------------------------------
// The original resource is identified by these dimensions.
#define ORIGINAL_WIDTH   416
#define ORIGINAL_HEIGHT  390


//---------------------------------------------------------------------
// Function Pointer Typedefs (for hooking)
typedef int*    (*EwLoadResource_t)(int param_1, unsigned int param_2);
typedef void*   (*EwNewObjectIndirect_t)(int *templ, uint32_t param);
typedef void    (*ResourcesExternBitmap__Init_t)(void *resource, uint32_t parent, uint32_t p3, uint32_t p4);
typedef uint32_t (*EwNewString_t)(unsigned short *param1);
typedef int     (*CoreView__GetRoot_t)(int *view);
typedef int     (*EwCreateBitmap_t)(int a0, int w, int h, int a3, int a4);
typedef int*    (*EwLockBitmap_t)(int bitmapHandle, int x, int y, int w, int h, int something, int something2);
typedef void    (*EwUnlockBitmap_t)(int* lockedPtr);
typedef void    (*EwSetRectH_t)(void *rect, int p1, int p2, int p3, int p4, int height);
typedef void    (*EwSetRectW_t)(void *rect, int p1, int p2, int p3, int p4, int width);
typedef void    (*ResourcesExternBitmap_OnSetName_t)(void *param_1, const uint16_t *param_2);
typedef void    (*EwFreeBitmap_t)(void* bitmapHandle);
typedef void    (*ResourcesExternBitmap__Done_t)(void* resourcePtr);
typedef void    (*MxDisplayPortCharItems_ClearAll_t)(int basePtr, int parentGroup);
typedef void    (*CoreGroup__Remove_t)(int parentPtr, int childPtr);

//---------------------------------------------------------------------
// Hook Function Prototypes
//---------------------------------------------------------------------
int* EwLoadResource(int param_1, unsigned int param_2);
int  EwCreateBitmap(int a0, int w, int h, int a3, int a4);
int* EwLockBitmap(int bitmapHandle, int x, int y, int w, int h, int something, int something2);
void EwUnlockBitmap(int* lockedPtr);
void EwSetRectH(void *rect, int p1, int p2, int p3, int p4, int height);
void EwSetRectW(void *rect, int p1, int p2, int p3, int p4, int width);

//---------------------------------------------------------------------
// Helper Function Prototypes
//---------------------------------------------------------------------
uint16_t* convert_to_utf16(const char* str);


#endif // O3_CUSTOM_FONTS_H

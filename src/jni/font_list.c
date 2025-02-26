#define _GNU_SOURCE
#include "font_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <ctype.h>
#include "spng.h"

// Global variables (single source of truth)
FontInfo g_fontList[MAX_NAMES];
int      g_fontCount = 0;
int      fontSelection = 0;
bool     externalResourceValid = false;
int      fontPages = 1;
char     names[MAX_NAMES][MAX_STRING_LENGTH];
static bool probeFontFile(const char* path, FontInfo* outInfo);



// Dynamic grid usage – these values will be set based on the selected font:
int g_curRows  = 22;
int g_curCols  = 60;
int g_curTileW = 24;
int g_curTileH = 36;

//---------------------------------------------------------------------
// readSettingsFile()/writeSettingsFile()
//---------------------------------------------------------------------
bool readSettingsFile(const char* path, int* outSel, int* outDef)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    int s, d;
    int n = fscanf(f, "%d %d", &s, &d);
    fclose(f);
    if (n != 2)
        return false;
    *outSel = s;
    *outDef = d;
    return true;
}

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
// scanFontDirectory(): Scan a directory for font files (BMP or PNG)
// and add them to list[] starting at index startIndex.
// Returns the number of fonts added.
static int scanFontDirectory(const char* dirPath, FontInfo list[], int maxCount, int startIndex)
{
    DIR *dirp = opendir(dirPath);
    if (!dirp) {
        mkdir(dirPath, 0755);
        return 0;
    }
    int found = startIndex;
    struct dirent* dp;
    while ((dp = readdir(dirp)) != NULL && found < maxCount) {
        if (dp->d_name[0] == '.')
            continue;
        char fullpath[MAX_STRING_LENGTH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirPath, dp->d_name);
        FontInfo info;
        // probeFontFile() defined below returns true if file is valid.
        if (/* probeFontFile */ (NULL != dp) && (strlen(dp->d_name) > 0)) { 
            // Call our probe function:
            if (probeFontFile(fullpath, &info)) {
                list[found++] = info;
            }
        }
    }
    closedir(dirp);
    return (found - startIndex);
}

//---------------------------------------------------------------------
// probeFontFile(): Check if a file is a valid BMP or PNG font file.
// It also deduces the resolution type and number of pages.
//---------------------------------------------------------------------
static bool probeFontFile(const char* path, FontInfo* outInfo)
{
    const char* dot = strrchr(path, '.');
    if (!dot)
        return false;
    char ext[8];
    snprintf(ext, sizeof(ext), "%s", dot);
    for (char* p = ext; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    FontFormat fmt;
    if (strcmp(ext, ".bmp") == 0)
        fmt = FONT_FMT_BMP;
    else if (strcmp(ext, ".png") == 0)
        fmt = FONT_FMT_PNG;
    else
        return false;
    
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return false;
    int w = 0, h = 0;
    int16_t bpp = 0;
    uint32_t compression = 0;
    if (fmt == FONT_FMT_BMP) {
        uint8_t hdr[54];
        if (fread(hdr, 1, 54, fp) < 54) {
            fclose(fp);
            return false;
        }
        if (hdr[0] != 'B' || hdr[1] != 'M') {
            fclose(fp);
            return false;
        }
        w = *(int32_t*)&hdr[18];
        h = *(int32_t*)&hdr[22];
        bpp = *(int16_t*)&hdr[28];
        compression = *(uint32_t*)&hdr[30];
        fclose(fp);
        if (bpp != 32 || compression != 0)
            return false;
    } else { // PNG
        uint8_t pngSig[8];
        if (fread(pngSig, 1, 8, fp) < 8) {
            fclose(fp);
            return false;
        }
        if (pngSig[0] != 0x89 || pngSig[1] != 0x50 || pngSig[2] != 0x4E ||
            pngSig[3] != 0x47) {
            fclose(fp);
            return false;
        }
        // Skip to IHDR width/height
        fseek(fp, 16, SEEK_SET);
        unsigned char whBuf[8];
        if (fread(whBuf, 1, 8, fp) < 8) {
            fclose(fp);
            return false;
        }
        fclose(fp);
        w = (whBuf[0] << 24) | (whBuf[1] << 16) | (whBuf[2] << 8) | whBuf[3];
        w = w * 16; //to pass the check later on
        h = (whBuf[4] << 24) | (whBuf[5] << 16) | (whBuf[6] << 8) | whBuf[7];
        h = h / 16;
        
    }
    if (w > 2047 || h > 2047){//internal bmp font loading limit
        return false;
    }
    // Determine resolution:
    // For a 60x22 font, assume tile size is 24x36 so final image: height must equal 16*36 = 576 and width a multiple of (16*24)=384.
    // For a 53x20 font, tile size 26x39 (final height=16*39=624, width multiple of (16*26)=416)
    // For a 30x16 font, tile size 48x32 (final height=16*32=512, width multiple of (16*48)=768)
    int pages = 1;
    FontResolution res = FONT_RES_UNKNOWN;
    if (h == 16 * 36 && w % (16 * 24) == 0) {
        res = FONT_RES_60x22;
        pages = w / (16 * 24);
    }
    else if (h == 16 * 39 && w % (16 * 26) == 0) {
        res = FONT_RES_53x20;
        pages = w / (16 * 26);
    }
    else if (h == 16 * 54 && w % (16 * 36) == 0) {
        res = FONT_RES_30x15;
        pages = w / (16 * 36);
    }
    else {
        return false;
    }
    outInfo->format = fmt;
    outInfo->resolution = res;
    outInfo->pages = pages;
    snprintf(outInfo->fullPath, MAX_STRING_LENGTH, "%s", path);
    return true;
}

//---------------------------------------------------------------------
// initFontList(): Scan both directories to build the font list.
// (This version re-scans every time it is called.)
//---------------------------------------------------------------------
static bool fontListInitialized = false;

void initFontList(void)
{

    g_fontCount = 0;
    // 1) Scan SD fonts.
    int n = scanFontDirectory("/storage/sdcard0/fonts", g_fontList, MAX_NAMES, 0);
    g_fontCount += n;
    // 2) If none found on SD, scan default fonts.
    if (g_fontCount == 0) {
        n = scanFontDirectory("/opt/default_fonts", g_fontList, MAX_NAMES, g_fontCount);
        g_fontCount += n;
    }
    // Update legacy names array
    for (int i = 0; i < g_fontCount; i++) {
        strncpy(names[i], g_fontList[i].fullPath, MAX_STRING_LENGTH - 1);
        names[i][MAX_STRING_LENGTH - 1] = '\0';
    }

    if (fontListInitialized)
    {   
        return;
    }
    // Read the settings file only once:
    const char* settingsPath = (g_fontCount > 0 &&
                                strstr(g_fontList[0].fullPath, "/storage/sdcard0/") != NULL)
                               ? "/storage/sdcard0/fonts/settings"
                               : "/opt/default_fonts/settings";
    int tmpSel = 0, tmpDef = -1;
    bool ok = readSettingsFile(settingsPath, &tmpSel, &tmpDef);

    if (ok){
        fontSelection = (tmpDef >= 0) ? tmpDef : tmpSel;
    }
    else{
        fontSelection = 0;
        writeSettingsFile(settingsPath, fontSelection, tmpDef);

    }

    if (g_fontCount > 0)
        fontSelection %= g_fontCount;

    fontListInitialized = true;
}


//---------------------------------------------------------------------
// pickFont(): Set the dynamic grid values based on the selected font.
//---------------------------------------------------------------------
void pickFont(void)
{
    if (g_fontCount == 0) {
        externalResourceValid = false;
        return;
    }
    fontSelection %= g_fontCount;
    FontResolution r = g_fontList[fontSelection].resolution;
    fontPages = g_fontList[fontSelection].pages;
    switch(r)
    {
        case FONT_RES_60x22:
            g_curRows = 22;
            g_curCols = 60;
            g_curTileW = 24;
            g_curTileH = 36;
            break;
        case FONT_RES_53x20:
            g_curRows = 20;
            g_curCols = 53;
            g_curTileW = 26;
            g_curTileH = 39;
            break;
        case FONT_RES_30x15:
            g_curRows = 15;
            g_curCols = 30;
            g_curTileW = 36;
            g_curTileH = 54;
            break;
        default:
            g_curRows = 22;
            g_curCols = 60;
            g_curTileW = 24;
            g_curTileH = 36;
            break;
    }
    externalResourceValid = true;
}

//---------------------------------------------------------------------
// check_external_resource(): If a valid external font is found, set the final dimensions.
//---------------------------------------------------------------------
bool check_external_resource(int *outWidth, int *outHeight)
{
    initFontList();
    if (g_fontCount == 0) {
        externalResourceValid = false;
        return false;
    }
    pickFont();
    if (!externalResourceValid)
        return false;
    // Final dimensions: width = 16 * g_curTileW * (pages) and height = 16 * g_curTileH.
    *outWidth  = 16 * g_curTileW * fontPages;
    *outHeight = 16 * g_curTileH;
    return true;
}

//---------------------------------------------------------------------
// Dummy implementation for loadFont()
// (In your real system, LoadBmp32 and LoadPng32 are implemented elsewhere.)
// Here we assume they are available in this file as static functions.
//---------------------------------------------------------------------
static bool LoadBmp32(const char* path, BmpData* out);
static bool LoadPng32(const char* path, BmpData* out);

bool loadFont(const FontInfo* fi, BmpData* out)
{
    freeCurrentFontData();
    bool ok = false;
    if (fi->format == FONT_FMT_BMP)
        ok = LoadBmp32(fi->fullPath, out);
    else
        ok = LoadPng32(fi->fullPath, out);
    return ok;
}

//---------------------------------------------------------------------
// Free previously loaded font data (if any)
//---------------------------------------------------------------------
void freeCurrentFontData(void)
{
    // In a complete system, you’d free g_bmpData.data.
    // For example:
    
    if (g_bmpData.data) {
        free(g_bmpData.data);
        g_bmpData.data = NULL;
    }

    g_bmpData.width = 0;
    g_bmpData.height = 0;
}


//---------------------------------------------------------------------
// Dummy implementation for LoadBmp32: (You can replace with your actual code.)
//---------------------------------------------------------------------
static bool LoadBmp32(const char* path, BmpData* out)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
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
    size_t imageSize = (size_t)w * h * 4;
    uint8_t* data = malloc(imageSize);
    if (!data) {
        fclose(fp);
        return false;
    }
    fseek(fp, pixelOffset, SEEK_SET);
    size_t rcount = fread(data, 1, imageSize, fp);
    fclose(fp);
    if (rcount < imageSize) {
        free(data);
        return false;
    }
    out->width = w;
    out->height = h;
    out->data = data;
    return true;
}

static bool LoadPng32(const char* path, BmpData* out)
{
    FILE* fd = fopen(path, "rb");
    if (!fd)
        return false;
    
    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx) {
        fclose(fd);
        return false;
    }
    
    // Set a reasonable limit on PNG chunk sizes.
    size_t limit = 1024UL * 1024UL * 64UL;
    spng_set_chunk_limits(ctx, limit, limit);
    spng_set_png_file(ctx, fd);
    
    struct spng_ihdr ihdr;
    int ret = spng_get_ihdr(ctx, &ihdr);
    if (ret) {
        spng_ctx_free(ctx);
        fclose(fd);
        return false;
    }
    
    size_t image_size = 0;
    ret = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &image_size);
    if (ret) {
        spng_ctx_free(ctx);
        fclose(fd);
        return false;
    }
    
    uint8_t* raw = malloc(image_size);
    if (!raw) {
        spng_ctx_free(ctx);
        fclose(fd);
        return false;
    }
    
    ret = spng_decode_image(ctx, raw, image_size, SPNG_FMT_RGBA8, 0);
    spng_ctx_free(ctx);
    fclose(fd);
    if (ret) {
        free(raw);
        return false;
    }
    
    // At this point, raw contains the decoded PNG pixels.
    // We expect the PNG to have:
    //    - Width = (pages * tileWidth) where tileWidth is g_curTileW.
    //    - Height = 256 * g_curTileH.
    // Our goal is to rearrange each page into a 16×16 grid.
    uint32_t srcW = ihdr.width;
    uint32_t srcH = ihdr.height;
    if (g_curTileW <= 0 || g_curTileH <= 0) {
        free(raw);
        return false;
    }
    int pages = srcW / g_curTileW;
    if (srcW % g_curTileW != 0 || srcH != (uint32_t)(256 * g_curTileH)) {
        free(raw);
        return false;
    }
    
    // Calculate final output dimensions:
    // For each page, we want a 16×16 grid of tiles.
    int finalW = pages * 16 * g_curTileW;
    int finalH = 16 * g_curTileH;
    size_t finalSize = (size_t)finalW * finalH * 4;
    uint8_t* finalData = malloc(finalSize);
    if (!finalData) {
        free(raw);
        return false;
    }
    memset(finalData, 0, finalSize);
    
    // Rearrangement:
    // For each page (p) and for each tile (i from 0 to 255):
    //   Source location in raw data:
    //       srcX = p * g_curTileW, srcY = i * g_curTileH.
    //   Destination: rearrange tile i into a 16×16 grid:
    //       row = i / 16, col = i % 16.
    //       dstX = p * (16 * g_curTileW) + (col * g_curTileW),
    //       dstY = row * g_curTileH.
    // To flip vertically, copy each tile row in reverse order.
    for (int p = 0; p < pages; p++) {
        for (int i = 0; i < 256; i++) {
            int srcX = p * g_curTileW;
            int srcY = i * g_curTileH;
            int row = i / 16;
            int col = i % 16;
            int dstX = p * (16 * g_curTileW) + col * g_curTileW;
            // Flip the row order in the 16x16 grid:
            int dstY = (15 - row) * g_curTileH;
            for (int ty = 0; ty < g_curTileH; ty++) {
                // Flip individual tile vertically (if needed)
                int srcIndex = ((srcY + (g_curTileH - 1 - ty)) * srcW + srcX) * 4;
                int dstIndex = ((dstY + ty) * finalW + dstX) * 4;
                memcpy(finalData + dstIndex, raw + srcIndex, g_curTileW * 4);
            }
        }
    }
    
    
    free(raw);
    
    // Now fix color channels: swap red and blue (0 and 2) for each pixel.
    for (int i = 0; i < finalW * finalH; i++) {
        uint8_t* pixel = finalData + i * 4;
        uint8_t temp = pixel[0];
        pixel[0] = pixel[2];
        pixel[2] = temp;
    }
    
    out->width = finalW;
    out->height = finalH;
    out->data = finalData;
    return true;
}

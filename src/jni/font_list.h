#ifndef FONT_LIST_H
#define FONT_LIST_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_STRING_LENGTH 256
#define MAX_NAMES 15


// Enumerations for font format and resolution type.
typedef enum {
    FONT_FMT_BMP,
    FONT_FMT_PNG
} FontFormat;

typedef enum {
    FONT_RES_UNKNOWN = 0,
    FONT_RES_60x22,  // Example: grid 60x22; tile size 24x36
    FONT_RES_53x20,  // Example: grid 53x20; tile size 26x39
    FONT_RES_30x15   // Example: grid 30x16; tile size 48x32
} FontResolution;

// Structure representing a discovered font file.
typedef struct {
    char fullPath[MAX_STRING_LENGTH]; // Full path to the font file.
    FontFormat format;                // BMP or PNG.
    FontResolution resolution;        // Which resolution layout it is.
    int pages;                        // Number of pages horizontally.
} FontInfo;

// Standard BMP data container (32‑bit RGBA)
typedef struct {
    int width;
    int height;
    uint8_t *data;
} BmpData;

// Global variables (defined in font_list.c)
extern FontInfo g_fontList[MAX_NAMES];
extern int      g_fontCount;    // Number of valid fonts found.
extern int      fontSelection;  // Currently selected font index.
extern bool     externalResourceValid;  // True if a valid external font is found.
extern int      fontPages;      // Number of pages horizontally.
extern BmpData g_bmpData;

// Legacy names array (for header naming, etc.)
extern char names[MAX_NAMES][MAX_STRING_LENGTH];

// Dynamic grid and tile sizes (set by pickFont)
extern int g_curRows;
extern int g_curCols;
extern int g_curTileW;
extern int g_curTileH;

extern int g_base;
extern int g_parent;

// Function prototypes:
// Re-scan the font directories and update globals.
// (This function will now re-scan every time it's called.)
void initFontList(void);

// Choose the active font (sets g_curRows, g_curCols, g_curTileW, g_curTileH, fontPages, and externalResourceValid)
// based on the current fontSelection and the discovered fonts.
void pickFont(void);

// Load the selected font’s image data (BMP or PNG) into *out.
// If the font is PNG, the function rearranges its source (256-tall pages)
// into the standard 16×16 grid layout.
bool loadFont(const FontInfo* fi, BmpData* out);

// Read and write settings file in “sel def” format.
bool readSettingsFile(const char* path, int* outSel, int* outDef);
bool writeSettingsFile(const char* path, int sel, int def);

// Check if a valid external font is available. If so, set *outWidth and *outHeight to the final expected dimensions
// (calculated from the dynamic tile sizes and page count) and return true.
bool check_external_resource(int *outWidth, int *outHeight);

// Free any previously loaded font data.
void freeCurrentFontData(void);

#endif // FONT_LIST_H

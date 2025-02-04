# o3-multipage-osd
Mod for enabling multipage OSD (up to 3 pages) for the O3 on the V2 goggles.
This mod combines and replaces two previous mods:
- o3-osd-record
- o3-custom-fonts


## NOTES
- The mod opens BMP images from the SD Card and uses them as the OSD font resource.
- The mod also directly interprets the msp displayport stream and enables the use of multiple font pages.
- There is an image size limit in the goggles, which currently restricts the fonts to 3 pages max. Using larger fonts will cause them not to be displayed art all.
- Using smaller fonts (e.g. a 1 page font when the FC expects 4 pages), will just remap the symbols of higher pages to page 1. So you can use 1 page betaflight fonts if you don't want the coloroptions.
- You can use your own fonts, just make sure it is a v3 BMP object with 32bit depth (alpha channel) and no compressionor use SNEAKY_FPV's fonts in the fonts folder.
- This mod also records the OSD to a .osd file. This is always enabled.
- Font selection is done by writing the font name into font.txt. If you createa font.txt and leave it empty you disable the mod.

##### Install
- coming soon 

##### Manual install
- `adb push o3-multipage-osd_1.0.0_pigeon-glasses-v2.ipk /tmp`
- open up shell with `adb shell`
- `opkg install /tmp/o3-multipage-osd_1.0.0_pigeon-glasses-v2.ipk`
- or `opkg upgrade /tmp/o3-multipage-osd_1.0.0_pigeon-glasses-v2.ipk` to upgrade an older verison.

##### Configuration
- copy fonts to the SD card root directory
- select font by writing the font name into font.txt (e. g. IANV_nexus, BTFL_sphere)
- If you are using INAV then in the OSD tab select "AVATAR" to get the 53*20 grid.

##### Known issues
- You need to manually select the font! Automatic font selection based on the FC firmware is not supported! If you switch between INav and Betaflight frequently consider having two SD cards with differnt fonts to swap in and out.
- Only max. 3 font pages supported.
- If the font is not as it should be, or the SD card is missing the mod will always try to default to the standard DJI OSD rendering. But test carefully.

##### Credits
- Thanks to Joonas for the help when developing this and SNEAKY_FPV for letting me use his fonts!

# o3-multipage-osd Version 2.0
Mod for enabling multipage OSD for the O3 on the V2 goggles.
This mod combines and replaces two previous mods:
- o3-osd-record
- o3-custom-fonts

## NOTES
- The mod opens font images from the SD Card and uses them as the OSD font resource.
- There are also 15 fonts installed on the goggles as a backup (one for each size and system), these can be changed (see further down).
- The mod directly interprets the msp DisplayPort stream and enables the use of multiple font pages.
- The font layout should be the classic 1-4-column .png format, which can be downloaded here https://sites.google.com/view/sneaky-fpv/home.
- This mod also records the OSD to a .osd file. This is always enabled if an SD card is present.
- I have a free OverlayTool, to create transparent OSD overlays, it's simple but it works. https://github.com/xNuclearSquirrel/O3_OverlayTool
- Font selection is done by placing the fonts you want to use in /fonts/ on the SD card.
- You put several fonts into the /fonts/ folder and then switch between them by long pressing the back button on the goggles (hold for ~6s).
- This mod supports multiple grid resolutions, determined by the font you selected.
- Supported are 3 grid sizes: (SD: 30x15, O3: 53x20, HD: 60x22).
- Grid size selection is done manually by selecting an _HD, _O3, or _SD font.


##### Install
- Make sure your goggles are on the latest firmware. I have not tested with older versions.
- Go to [https://fpv.wtf/](https://fpv.wtf/). If you have not done so, root the goggles and install WTFOS
- Go to the package manager, if you had previously installed - `o3-osd-record` or  `o3-custom-fonts` disable those first.
- Then search for [`o3-multipage-osd`](https://fpv.wtf/package/fpv-wtf/o3-multipage-osd) and install.


##### Configuration
- On the goggles set the canvas mode to HD (Settings -> Display -> Canvas Mode: HD).
- Fonts for this mod are available from [Sneaky_FPV](https://sites.google.com/view/sneaky-fpv/home)s website under **WTFOS** > "O3 + Goggles v2 Mod Format".

- Each font exists in three resolutions:
      **SD: 30x15, O3: 53x20, HD: 60x22**
   
- For Betaflight the default is O3 (53x20), if you want to use a different resolution, set it via CLI e.g. `set osd_canvas_height = 22` and `set osd_canvas_width = 60` to get the WTFOS resolution used with the vista.
- For INAV select "DJI NATIVE" (or "AVATAR" for older versions) in the OSD tab for the O3 resolution and DJIWTF for HD.

- To install fonts on your goggles, configure the mod settings in the package manager ([click here](https://fpv.wtf/package/fpv-wtf/o3-multipage-osd)). Simply add the fonts and resolutions you would like to use and they will be installed on the goggles. Alternatively you can manually download them and copy them into the /fonts/ directory on your SD card.
- To toggle between fonts in the /fonts/ folder press the back button (next to the record button) and hold for roughly 6 seconds. When restarting the goggles they will remember the font which was used last.



##### Known issues
- The mod is now always enabled. Even with no font in /fonts/ it will just use the default fonts. To switch it off, you will need to uninstall it.

- **Test carefully this is by no means well and thoroughly tested. There could be overallocations of memory and you could run into issues such as SD speed low warnings. If you encounter anything strange please report it!**

##### Manual install (alternative)
- `adb push o3-multipage-osd_2.1.0_pigeon-glasses-v2.ipk /tmp`
- open up shell with `adb shell`
- `opkg install /tmp/o3-multipage-osd_2.1.0_pigeon-glasses-v2.ipk`
- or `opkg upgrade /tmp/o3-multipage-osd_2.1.0_pigeon-glasses-v2.ipk` to upgrade an older version.

##### Credits
- Thanks to Joonas for the help when developing this and SNEAKY_FPV for letting me use his fonts!


## Support the Project 💖
If you find this project useful, consider donating to support development!

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://www.paypal.com/donate/?hosted_button_id=BSA49E6J5DLM4)


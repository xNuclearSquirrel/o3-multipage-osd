LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := o3-multipage-osd
LOCAL_SRC_FILES := o3-custom-fonts.c MxDisplayPortDisplayPort_DrawScreen.c DisplayPortProcessHook.c gs_lv_transcode_rec_omx_start.c 
LOCAL_CFLAGS    += -fPIC
LOCAL_LDFLAGS   += -fPIC

# Ensure the output goes to the correct location
TARGET_OUT := E:/Documents/gogglesV2/builds/msp

include $(BUILD_SHARED_LIBRARY)
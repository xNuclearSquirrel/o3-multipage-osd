// gs_lv_transcode_rec_omx_start.c

#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>     // For usleep()
#include <string.h>     // For snprintf, strncpy, strncat, strrchr
#include <time.h>       // For clock_gettime()

#include "ring_buffer.h"
#include "o3-custom-fonts.h" 
#include "font_list.h"  // for fontSelection, g_curRows/g_curCols, etc.

// Define the type signatures of the original functions
typedef int (*orig_gs_lv_transcode_rec_omx_start_t)(int);
typedef int (*orig_gs_lv_transcode_rec_omx_stop_t)(void *);

// Global variables
pthread_t worker_thread;               // Thread identifier
volatile int thread_running = 0;       // Flag to indicate if the thread is running

ring_buffer_t ring_buffer;
volatile int recording_active = 0;     // Recording active flag

// File pointer for OSD data
FILE *osd_file = NULL;

// Recording start time
uint32_t recording_start_time = 0;


static int actualFrameSize = 1320;


/**
 * We write a 40-byte header:
 *  - Bytes 0..3:  up to 4 bytes from the font name (firmware).
 *  - Bytes 4..31: next 28 bytes of the font name (or 0 if shorter).
 *  - Bytes 32..35: "DJO3"
 *  - Byte 36: colCount
 *  - Byte 37: 0
 *  - Byte 38: rowCount
 *  - Byte 39: 0
 */
void write_osd_file_header(void)
{
    char firmware[4] = {0};
    char remainder[28] = {0};
    char signature[4] = {'D','J','O','3'};  // "DJO3"

    // 1) Derive a short name from the chosen font. 
    //    We can get the current 'names[fontSelection]' or from 'g_chosenFontPath', etc.
    //    For simplicity, let's do the same approach as your old code:
    char *fontName = names[fontSelection];
    if (fontName[0] != '\0')
    {
        // Extract the base name (strip the directory path)
        char *base = strrchr(fontName, '/');
        if (base)
            base++;  // Move past the '/'
        else
            base = fontName;
    
        // Remove the extension (e.g., ".bmp")
        char *dot = strrchr(base, '.');
        if (dot)
            *dot = '\0';
    
        // Copy up to 4 bytes into firmware
        strncpy(firmware, base, 4);
    
        // If length > 4, copy the next 28 into remainder
        size_t length = strlen(base);
        if (length > 4) {
            strncpy(remainder, base + 4, 28);
        }
    }
    

    // 2) Write them out
    // firmware[4] + remainder[28] => total 32
    fwrite(firmware,   1, 4,  osd_file);
    fwrite(remainder,  1, 28, osd_file);

    // 3) Then "DJO3"
    fwrite(signature, 1, 4, osd_file);

    // 4) Then the row/col. 
    //    We store col in [36], row in [38], with 37/39=0.
    //    We get them from g_curCols, g_curRows.
    uint8_t colCount = (uint8_t)g_curCols;
    uint8_t rowCount = (uint8_t)g_curRows;

    actualFrameSize = g_curRows * g_curCols;
    uint8_t last4[4];
    last4[0] = colCount;
    last4[1] = 0;
    last4[2] = rowCount;
    last4[3] = 0;
    fwrite(last4, 1, 4, osd_file);
}

// Worker thread function implementation
void* worker_thread_func(void* arg)
{
    int header_written = 0;

    while (recording_active || ring_buffer.head != ring_buffer.tail) {
        RingBufferEntry entry;

        // Lock the ring buffer
        pthread_mutex_lock(&ring_buffer.mutex);

        // Wait if the ring buffer is empty
        while (ring_buffer.tail == ring_buffer.head && recording_active) {
            pthread_cond_wait(&ring_buffer.not_empty, &ring_buffer.mutex);
        }

        if (ring_buffer.tail != ring_buffer.head) {
            // Read data from the buffer
            entry = ring_buffer.entries[ring_buffer.tail];
            ring_buffer.tail = (ring_buffer.tail + 1) % RING_BUFFER_SIZE;

            // Signal that the ring buffer is not full
            pthread_cond_signal(&ring_buffer.not_full);

            pthread_mutex_unlock(&ring_buffer.mutex);

            // Write the header only once
            if (!header_written) {
                write_osd_file_header();
                header_written = 1;
            }

            // Write delta_time
            fwrite(&entry.delta_time, sizeof(uint32_t), 1, osd_file);

            // Instead of always writing 1060, we write (g_curRows * g_curCols).
            // But if you want to keep it exactly 1060, that's also possible.
            // Let's do the smaller portion for a “correct” file:

            fwrite(entry.osd_frame_data, sizeof(uint16_t), actualFrameSize, osd_file);
        } else {
            pthread_mutex_unlock(&ring_buffer.mutex);
            usleep(1000);  // Sleep 1 ms
        }
    }

    // Close the OSD file
    if (osd_file) {
        fclose(osd_file);
        osd_file = NULL;
    }

    return NULL;
}


// Hook for the `gs_lv_transcode_rec_omx_start` function
int gs_lv_transcode_rec_omx_start(int param_1)
{
    static orig_gs_lv_transcode_rec_omx_start_t orig_func = NULL;
    if (!orig_func) {
        orig_func = (orig_gs_lv_transcode_rec_omx_start_t)
            dlsym(RTLD_NEXT, "gs_lv_transcode_rec_omx_start");
        if (!orig_func) {
            return -1;
        }
    }

    // Call the original function
    int result = orig_func(param_1);

    // Access the 0xb8 flag at param_1 + 0xb8
    uint8_t flag_b8 = *(uint8_t *)((uint8_t *)param_1 + 0xb8);

    if (flag_b8 == 0) {
        // Recording started
        if (!recording_active) {
            // Initialize ring buffer
            ring_buffer.head = 0;
            ring_buffer.tail = 0;
            pthread_mutex_init(&ring_buffer.mutex, NULL);
            pthread_cond_init(&ring_buffer.not_empty, NULL);
            pthread_cond_init(&ring_buffer.not_full, NULL);

            // Build the .osd filename
            char *file_name_ptr = (char *)((uint8_t *)param_1 + 0x150);
            char osd_file_name[256];
            strncpy(osd_file_name, file_name_ptr, sizeof(osd_file_name) - 1);
            osd_file_name[sizeof(osd_file_name) - 1] = '\0';

            char *dot = strrchr(osd_file_name, '.');
            if (dot) {
                *dot = '\0';
            }
            strncat(osd_file_name, ".osd",
                    sizeof(osd_file_name) - strlen(osd_file_name) - 1);

            osd_file = fopen(osd_file_name, "wb");
            if (!osd_file) {
                return result;
            }

            recording_active = 1;

            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            recording_start_time = (uint32_t)
                ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));

            if (!thread_running) {
                thread_running = 1;
                int ret = pthread_create(&worker_thread, NULL, worker_thread_func, NULL);
                if (ret != 0) {
                    thread_running = 0;
                    fclose(osd_file);
                    osd_file = NULL;
                    recording_active = 0;
                    return result;
                }
            }
        }
    }

    return result;
}


// Hook for the `gs_lv_transcode_rec_omx_stop` function
int gs_lv_transcode_rec_omx_stop(void *param_1)
{
    static orig_gs_lv_transcode_rec_omx_stop_t orig_stop_func = NULL;
    if (!orig_stop_func) {
        orig_stop_func = (orig_gs_lv_transcode_rec_omx_stop_t)
            dlsym(RTLD_NEXT, "gs_lv_transcode_rec_omx_stop");
        if (!orig_stop_func) {
            return -1;
        }
    }

    // Access the 0xb8 flag
    uint8_t flag_b8 = *(uint8_t *)((uint8_t *)param_1 + 0xb8);

    if (flag_b8 == 0) {
        // Stop recording
        if (recording_active) {
            recording_active = 0;

            if (thread_running) {
                pthread_mutex_lock(&ring_buffer.mutex);
                pthread_cond_signal(&ring_buffer.not_empty);
                pthread_mutex_unlock(&ring_buffer.mutex);

                pthread_join(worker_thread, NULL);
                thread_running = 0;

                pthread_mutex_destroy(&ring_buffer.mutex);
                pthread_cond_destroy(&ring_buffer.not_empty);
                pthread_cond_destroy(&ring_buffer.not_full);
            }

            if (osd_file) {
                fclose(osd_file);
                osd_file = NULL;
            }
        }
    }

    // Call the original stop function
    int stop_result = orig_stop_func(param_1);
    return stop_result;
}

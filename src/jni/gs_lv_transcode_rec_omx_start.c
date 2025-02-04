// gs_lv_transcode_rec_omx_hooks.c

#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>     // For usleep()
#include <string.h>     // For snprintf, strncpy, strncat, strrchr
#include <time.h>       // For clock_gettime()

#include "ring_buffer.h"  // Include the header file

static const char *variableBmpPath = "/storage/sdcard0/font.txt";

// Define the type signatures of the original functions
typedef int (*orig_gs_lv_transcode_rec_omx_start_t)(int);
typedef int (*orig_gs_lv_transcode_rec_omx_stop_t)(void *);

// Global variables
pthread_t worker_thread;               // Thread identifier
volatile int thread_running = 0;       // Flag to indicate if the thread is running

// Define the ring buffer and recording flag
ring_buffer_t ring_buffer;
volatile int recording_active = 0;     // Recording active flag

// File pointer for OSD data
FILE *osd_file = NULL;

// Recording start time
uint32_t recording_start_time = 0;


// Function to write the header to the OSD file based on grid dimensions
void write_osd_file_header(void) {
    // Write the magic string "MSPOSD" followed by a null byte


    FILE *file = fopen(variableBmpPath, "rb");
    char firmware[4] = {0}; // first 4 byste of the font name go here
    char header[36] = {0};  // 32 bytes for name, 4 bytes for "DJO3"

    if (file) {
        size_t bytesRead = fread(firmware, 1, 4, file);
        if (bytesRead < 4) {
            memset(firmware, 0, 4);
        }

        bytesRead = fread(header, 1, 32, file);
        if (bytesRead < 32) {
            memset(header + bytesRead, 0, 32 - bytesRead);
        }

        memcpy(header + 32, "DJO3", 4);
        fclose(file);
    }

    fwrite(firmware, sizeof(firmware), 1, osd_file);
    fwrite(header, sizeof(header), 1, osd_file);

}

// Worker thread function implementation
void* worker_thread_func(void* arg) {
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



            // Write delta_time, frame_size, and OSD data to the file
            fwrite(&entry.delta_time, sizeof(uint32_t), 1, osd_file);
            fwrite(entry.osd_frame_data, sizeof(uint16_t), 1060, osd_file);
        } else {
            pthread_mutex_unlock(&ring_buffer.mutex);

            // Sleep briefly if no data
            usleep(1000);  // Sleep for 1 millisecond
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
int gs_lv_transcode_rec_omx_start(int param_1) {
    // Get a handle to the original function
    static orig_gs_lv_transcode_rec_omx_start_t orig_func = NULL;
    if (!orig_func) {
        orig_func = (orig_gs_lv_transcode_rec_omx_start_t)dlsym(RTLD_NEXT, "gs_lv_transcode_rec_omx_start");
        if (!orig_func) {
            return -1; // Return an error if the original function can't be found
        }
    }

    // Call the original function and capture its return value
    int result = orig_func(param_1);

    // Access the 0xb8 flag at param_1 + 0xb8
    uint8_t flag_b8 = *(uint8_t *)((uint8_t *)param_1 + 0xb8);

    if (flag_b8 == 0) {
        // Recording started
        if (!recording_active) {
            // Initialize the ring buffer
            ring_buffer.head = 0;
            ring_buffer.tail = 0;
            pthread_mutex_init(&ring_buffer.mutex, NULL);
            pthread_cond_init(&ring_buffer.not_empty, NULL);
            pthread_cond_init(&ring_buffer.not_full, NULL);

            // Open the OSD file
            char *file_name_ptr = (char *)((uint8_t *)param_1 + 0x150);
            char osd_file_name[256];

            strncpy(osd_file_name, file_name_ptr, sizeof(osd_file_name) - 1);
            osd_file_name[sizeof(osd_file_name) - 1] = '\0';

            char *dot = strrchr(osd_file_name, '.');
            if (dot != NULL) {
                *dot = '\0';
            }

            strncat(osd_file_name, ".osd", sizeof(osd_file_name) - strlen(osd_file_name) - 1);

            osd_file = fopen(osd_file_name, "wb");
            if (!osd_file) {
                return result;
            }

            recording_active = 1;

            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            recording_start_time = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);


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
int gs_lv_transcode_rec_omx_stop(void *param_1) {
    // Get a handle to the original function
    static orig_gs_lv_transcode_rec_omx_stop_t orig_stop_func = NULL;
    if (!orig_stop_func) {
        orig_stop_func = (orig_gs_lv_transcode_rec_omx_stop_t)dlsym(RTLD_NEXT, "gs_lv_transcode_rec_omx_stop");
        if (!orig_stop_func) {
            return -1; // Return an error code if the original function can't be found
        }
    }

    // Access the 0xb8 flag at param_1 + 0xb8
    uint8_t flag_b8 = *(uint8_t *)((uint8_t *)param_1 + 0xb8);

    if (flag_b8 == 0) {
        // Correct call to stop recording
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

    // Call the original stop function and capture its return value
    int stop_result = orig_stop_func(param_1);

    return stop_result;
}

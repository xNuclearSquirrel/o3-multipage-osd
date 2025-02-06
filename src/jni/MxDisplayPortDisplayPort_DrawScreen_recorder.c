// MxDisplayPortDisplayPort_DrawScreen.c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>     // For memcpy

#include "ring_buffer.h"  // Include the header file

// Declare the original function pointers
typedef void (*orig_MxDisplayPortDisplayPort_DrawScreen_t)(int, int);
typedef void* (*orig_MxContainerDynamicArray_At_t)(int, int, uint32_t, uint32_t);

static orig_MxDisplayPortDisplayPort_DrawScreen_t orig_func = NULL;
static orig_MxContainerDynamicArray_At_t orig_MxContainerDynamicArray_At = NULL;

void MxDisplayPortDisplayPort_DrawScreen(int param_1, int param_2) {
    // Load the original function if not already loaded
    if (orig_func == NULL) {
        orig_func = (orig_MxDisplayPortDisplayPort_DrawScreen_t)dlsym(RTLD_NEXT, "MxDisplayPortDisplayPort_DrawScreen");
        if (!orig_func) {
            return;
        }
    }

    if (orig_MxContainerDynamicArray_At == NULL) {
        orig_MxContainerDynamicArray_At = (orig_MxContainerDynamicArray_At_t)dlsym(RTLD_NEXT, "MxContainerDynamicArray_At");
        if (!orig_MxContainerDynamicArray_At) {
            return;
        }
    }

    // Call the original function
    orig_func(param_1, param_2);

    if (recording_active && grid_size == FRAME_SIZE) {
		// Prepare the ring buffer entry
		RingBufferEntry entry;

		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		entry.delta_time = (uint32_t)(((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000)) - recording_start_time);


		// Lock the ring buffer
		pthread_mutex_lock(&ring_buffer.mutex);

		// Check if the buffer is full
		if (((ring_buffer.head + 1) % RING_BUFFER_SIZE) == ring_buffer.tail) {
			// Buffer is full; advance tail to overwrite oldest data
			ring_buffer.tail = (ring_buffer.tail + 1) % RING_BUFFER_SIZE;
		}

		// Add data to the ring buffer
		ring_buffer.entries[ring_buffer.head] = entry;
		ring_buffer.head = (ring_buffer.head + 1) % RING_BUFFER_SIZE;

		// Signal that the ring buffer is not empty
		pthread_cond_signal(&ring_buffer.not_empty);

		// Unlock the ring buffer
		pthread_mutex_unlock(&ring_buffer.mutex);
	}
}
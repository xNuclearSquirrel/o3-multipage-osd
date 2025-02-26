#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <pthread.h>
#include <stdint.h>

// Define the maximum size of the OSD frame data
// 60*22 = 1320 is your largest known grid.
#define FRAME_SIZE 1320

// Define the size of the ring buffer
#define RING_BUFFER_SIZE 6

// Structure for OSD frame data
typedef struct {
    uint32_t delta_time;
    // we store the maximum possible glyphs (1320)
    uint16_t osd_frame_data[FRAME_SIZE];
} RingBufferEntry;

// Structure for the ring buffer
typedef struct {
    RingBufferEntry entries[RING_BUFFER_SIZE];
    int head;
    int tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} ring_buffer_t;

// Extern variables for hooking or usage in other files
extern ring_buffer_t ring_buffer;
extern volatile int recording_active;
extern uint32_t recording_start_time;

#endif // RING_BUFFER_H

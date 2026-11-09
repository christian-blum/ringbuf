#include "ringbuf.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

struct ringbuf_t {
    uint8_t *buffer;
    size_t element_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    ringbuf_policy_t policy;
    SemaphoreHandle_t mutex;
};

ringbuf_handle_t ringbuf_create(size_t element_size, size_t capacity, ringbuf_policy_t policy, bool use_psram) {
    if (element_size == 0 || capacity == 0) return NULL;

    ringbuf_handle_t rb = (ringbuf_handle_t)malloc(sizeof(struct ringbuf_t));
    if (!rb) return NULL;

    rb->element_size = element_size;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    rb->policy = policy;

    // Mutex for thread safety in FreeRTOS / ESP-IDF
    rb->mutex = xSemaphoreCreateMutex();
    if (!rb->mutex) {
        free(rb);
        return NULL;
    }

    // Allocate memory for the actual buffer (internal RAM or PSRAM)
    size_t total_size = element_size * capacity;
    uint32_t caps = use_psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    rb->buffer = (uint8_t *)heap_caps_malloc(total_size, caps);
    if (!rb->buffer) {
        // Fallback to standard heap if PSRAM is unavailable or exhausted
        rb->buffer = (uint8_t *)malloc(total_size);
    }

    if (!rb->buffer) {
        vSemaphoreDelete(rb->mutex);
        free(rb);
        return NULL;
    }

    return rb;
}

void ringbuf_destroy(ringbuf_handle_t rb) {
    if (!rb) return;
    if (rb->mutex) {
        vSemaphoreDelete(rb->mutex);
    }
    if (rb->buffer) {
        heap_caps_free(rb->buffer); // Works for standard malloc as well
    }
    free(rb);
}

bool ringbuf_push(ringbuf_handle_t rb, const void *element) {
    if (!rb || !element) return false;

    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) return false;

    if (rb->count == rb->capacity) {
        if (rb->policy == RINGBUF_POLICY_REJECT) {
            xSemaphoreGive(rb->mutex);
            return false; // Buffer full, reject insertion
        } else {
            // Overwrite: advance tail pointer by one element
            rb->tail = (rb->tail + 1) % rb->capacity;
            rb->count--;
        }
    }

    // Copy element to the head position
    memcpy(rb->buffer + (rb->head * rb->element_size), element, rb->element_size);
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;

    xSemaphoreGive(rb->mutex);
    return true;
}

bool ringbuf_pop(ringbuf_handle_t rb, void *out_element) {
    if (!rb || !out_element) return false;

    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) return false;

    if (rb->count == 0) {
        xSemaphoreGive(rb->mutex);
        return false; // Buffer empty
    }

    // Copy element from the tail position
    memcpy(out_element, rb->buffer + (rb->tail * rb->element_size), rb->element_size);
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;

    xSemaphoreGive(rb->mutex);
    return true;
}

void ringbuf_iterator_init(ringbuf_handle_t rb, ringbuf_iterator_t *it) {
    if (!rb || !it) return;

    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) == pdTRUE) {
        it->rb = rb;
        it->capacity = rb->capacity;
        it->tail = rb->tail;
        it->count = rb->count;
        it->current_pos = -1; // Positioned before the first element
        xSemaphoreGive(rb->mutex);
    } else {
        it->rb = NULL;
        it->count = 0;
        it->current_pos = -1;
    }
}

bool ringbuf_iterator_seek(ringbuf_iterator_t *it, size_t logical_index) {
    if (!it || !it->rb) return false;

    ringbuf_handle_t rb = it->rb;
    bool success = false;

    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) == pdTRUE) {
        it->tail = rb->tail;
        it->count = rb->count;

        if (logical_index < it->count) {
            it->current_pos = (int64_t)logical_index;
            success = true;
        }
        xSemaphoreGive(rb->mutex);
    }
    return success;
}

bool ringbuf_iterator_seek_oldest(ringbuf_iterator_t *it) {
    return ringbuf_iterator_seek(it, 0);
}

bool ringbuf_iterator_seek_newest(ringbuf_iterator_t *it) {
    if (!it || !it->rb) return false;
    
    ringbuf_handle_t rb = it->rb;
    size_t current_count = 0;
    
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) == pdTRUE) {
        current_count = rb->count;
        xSemaphoreGive(rb->mutex);
    }

    if (current_count == 0) return false;
    return ringbuf_iterator_seek(it, current_count - 1);
}

bool ringbuf_iterator_get(ringbuf_iterator_t *it, void *out_element) {
    if (!it || !it->rb || it->current_pos < 0 || it->current_pos >= (int64_t)it->count) return false;

    ringbuf_handle_t rb = it->rb;
    bool success = false;

    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) == pdTRUE) {
        size_t physical_index = (it->tail + it->current_pos) % it->capacity;
        memcpy(out_element, rb->buffer + (physical_index * rb->element_size), rb->element_size);
        xSemaphoreGive(rb->mutex);
        success = true;
    }
    return success;
}

bool ringbuf_iterator_next(ringbuf_iterator_t *it, void *out_element) {
    if (!it || !it->rb || it->count == 0) return false;

    if (it->current_pos < (int64_t)it->count - 1) {
        it->current_pos++;
        return ringbuf_iterator_get(it, out_element);
    }
    return false;
}

bool ringbuf_iterator_prev(ringbuf_iterator_t *it, void *out_element) {
    if (!it || !it->rb || it->count == 0) return false;

    if (it->current_pos > 0) {
        it->current_pos--;
        return ringbuf_iterator_get(it, out_element);
    }
    return false;
}

size_t ringbuf_count(ringbuf_handle_t rb) {
    if (!rb) return 0;
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) return 0;
    size_t cnt = rb->count;
    xSemaphoreGive(rb->mutex);
    return cnt;
}

bool ringbuf_is_full(ringbuf_handle_t rb) {
    if (!rb) return false;
    return ringbuf_count(rb) == rb->capacity;
}

bool ringbuf_is_empty(ringbuf_handle_t rb) {
    if (!rb) return false;
    return ringbuf_count(rb) == 0;
}

void ringbuf_clear(ringbuf_handle_t rb) {
    if (!rb) return;
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) == pdTRUE) {
        rb->head = 0;
        rb->tail = 0;
        rb->count = 0;
        xSemaphoreGive(rb->mutex);
    }
}
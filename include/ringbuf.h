#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Policy when the buffer is full
typedef enum {
    RINGBUF_POLICY_REJECT = 0,    // Returns false / reports error when full
    RINGBUF_POLICY_OVERWRITE      // Overwrites the oldest element when full
} ringbuf_policy_t;

// Opaque handle for the ring buffer
typedef struct ringbuf_t *ringbuf_handle_t;

// Bidirectional iterator for non-destructive reading
typedef struct {
    ringbuf_handle_t rb;
    size_t capacity;
    size_t tail;
    size_t count;
    int32_t current_pos;          // Logical position (-1 to count - 1)
} ringbuf_iterator_t;

/**
 * @brief Creates a new ring buffer on the heap.
 * 
 * @param element_size Size of a single element in bytes.
 * @param capacity Maximum number of elements in the buffer.
 * @param policy Strategy when buffer is full (REJECT or OVERWRITE).
 * @param use_psram true to allocate the buffer in PSRAM (if available), otherwise internal RAM.
 * @return ringbuf_handle_t Handle to the buffer or NULL on failure.
 */
ringbuf_handle_t ringbuf_create(size_t element_size, size_t capacity, ringbuf_policy_t policy, bool use_psram);

/**
 * @brief Destroys the ring buffer and frees allocated memory.
 */
void ringbuf_destroy(ringbuf_handle_t rb);

/**
 * @brief Pushes an element into the buffer.
 * 
 * @param rb Buffer handle.
 * @param element Pointer to the element to insert.
 * @return true on success, false if buffer is full (only with POLICY_REJECT).
 */
bool ringbuf_push(ringbuf_handle_t rb, const void *element);

/**
 * @brief Reads and removes the next element (destructive).
 * 
 * @param rb Buffer handle.
 * @param out_element Destination buffer for the element.
 * @return true on success, false if buffer is empty.
 */
bool ringbuf_pop(ringbuf_handle_t rb, void *out_element);

/**
 * @brief Initializes an iterator for non-destructive bidirectional reading.
 * 
 * @param rb Buffer handle.
 * @param it Pointer to the iterator.
 */
void ringbuf_iterator_init(ringbuf_handle_t rb, ringbuf_iterator_t *it);

/**
 * @brief Positions the iterator at a specific logical index (0 = oldest, count - 1 = newest).
 * 
 * @param it Iterator pointer.
 * @param logical_index Index of the element (0 to count - 1).
 * @return true on success, false if index is out of bounds.
 */
bool ringbuf_iterator_seek(ringbuf_iterator_t *it, size_t logical_index);

/**
 * @brief Positions the iterator at the oldest element (index 0).
 * 
 * @param it Iterator pointer.
 * @return true on success, false if buffer is empty.
 */
bool ringbuf_iterator_seek_oldest(ringbuf_iterator_t *it);

/**
 * @brief Positions the iterator at the newest element (index count - 1).
 * 
 * @param it Iterator pointer.
 * @return true on success, false if buffer is empty.
 */
bool ringbuf_iterator_seek_newest(ringbuf_iterator_t *it);

/**
 * @brief Reads the element at the current iterator position without moving.
 * 
 * @param it Iterator pointer.
 * @param out_element Destination buffer for the element.
 * @return true on success, false if position is invalid.
 */
bool ringbuf_iterator_get(ringbuf_iterator_t *it, void *out_element);

/**
 * @brief Moves the iterator to the next element (oldest -> newest) and reads it.
 * 
 * @param it Iterator pointer.
 * @param out_element Destination buffer for the element.
 * @return true if an element was successfully read, false if at the end.
 */
bool ringbuf_iterator_next(ringbuf_iterator_t *it, void *out_element);

/**
 * @brief Moves the iterator to the previous element (newest -> oldest) and reads it.
 * 
 * @param it Iterator pointer.
 * @param out_element Destination buffer for the element.
 * @return true if an element was successfully read, false if at the beginning.
 */
bool ringbuf_iterator_prev(ringbuf_iterator_t *it, void *out_element);

// Helper functions
size_t ringbuf_count(ringbuf_handle_t rb);
bool ringbuf_is_full(ringbuf_handle_t rb);
bool ringbuf_is_empty(ringbuf_handle_t rb);
void ringbuf_clear(ringbuf_handle_t rb);
/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer implementation
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * Finds the entry corresponding to a file position.
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
        struct aesd_circular_buffer *buffer,
        size_t char_offset,
        size_t *entry_offset_byte_rtn)
{
    size_t cumulative_size = 0;
    uint8_t index;
    uint8_t entry_count;

    if (!buffer || !entry_offset_byte_rtn) {
        return NULL;
    }

    entry_count = buffer->full ?
        AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : buffer->in_offs;

    for (index = 0; index < entry_count; index++) {
        uint8_t actual_index =
            (buffer->out_offs + index) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        size_t entry_size = buffer->entry[actual_index].size;

        if (char_offset < cumulative_size + entry_size) {
            *entry_offset_byte_rtn = char_offset - cumulative_size;
            return &buffer->entry[actual_index];
        }

        cumulative_size += entry_size;
    }

    return NULL;
}

/**
 * Adds an entry to the circular buffer.
 *
 *If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 *new start location.
 *
 */
const char *aesd_circular_buffer_add_entry(
        struct aesd_circular_buffer *buffer,
        const struct aesd_buffer_entry *add_entry)
{
    const char *ret = NULL;

    if (!buffer || !add_entry) {
        return NULL;
    }

    /*
     * When full, in_offs points to the oldest entry to be overwritten.
     */
    if (buffer->full) {
        ret = buffer->entry[buffer->in_offs].buffptr;
    }

    buffer->entry[buffer->in_offs] = *add_entry;

    buffer->in_offs =
        (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    if (buffer->full) {
        buffer->out_offs =
            (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }

    return ret;
}

/**
 * Initializes the circular buffer.
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    uint8_t index;

    if (!buffer) {
        return;
    }

    buffer->in_offs = 0;
    buffer->out_offs = 0;
    buffer->full = false;

    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++) {
        buffer->entry[index].buffptr = NULL;
        buffer->entry[index].size = 0;
    }
}

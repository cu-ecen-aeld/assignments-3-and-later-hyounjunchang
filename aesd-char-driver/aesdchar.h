/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

//#define AESD_DEBUG 1  //Remove comment on this line to enable debug
#define AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED 10

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_buffer_entry
{
    char *buffptr;
    size_t size;
};

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     */
    struct cdev cdev;     /* Char device structure      */

    struct aesd_buffer_entry entry[AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED];
    struct aesd_buffer_entry curr_input;

    uint8_t out_offs;
    uint8_t in_offs;

    size_t total_bytes_evicted;

    bool full;

    struct semaphore sem; // locking structure
};


/**
 * Returns the total number of bytes currently readable in the device,
 * including total_bytes_evicted (absolute file offset of the oldest entry).
 * Caller must hold dev->sem.
 */
static inline loff_t aesd_dev_size(const struct aesd_dev *dev)
{
    loff_t size = dev->total_bytes_evicted;
    uint8_t num_entries, i;

    if (dev->full) {
        num_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else {
        num_entries = (dev->in_offs - dev->out_offs +
                       AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
                      % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    for (i = 0; i < num_entries; i++) {
        uint8_t idx = (dev->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        size += dev->entry[idx].size;
    }

    return size;
}

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */

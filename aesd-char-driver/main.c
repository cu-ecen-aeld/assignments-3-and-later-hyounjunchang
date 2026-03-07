/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

#include <linux/slab.h> // kfree kmalloc

MODULE_AUTHOR("Hyounjun Chang");
MODULE_LICENSE("Dual BSD/GPL");

// helper functions declaration
struct aesd_buffer_entry *aesd_dev_find_entry_offset_for_fpos(struct aesd_dev *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn );

void aesd_dev_add_entry(struct aesd_dev *buffer, const struct aesd_buffer_entry *add_entry);
void free_all_aesd_dev_buffer(struct aesd_dev *buffer);



struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{   
    struct aesd_dev *dev;
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    // similar to page 58, Linux Device Drivers, from scull_open()
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

   
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_device.curr_input.size = 0;
    aesd_device.curr_input.buffptr = NULL;

    aesd_device.out_offs = 0;
    aesd_device.read_start_index = 0;
    aesd_device.in_offs = 0;
    aesd_device.full = false;
    
    // intialize mutex
    sema_init(aesd_device.sem, 1);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    
    free_all_aesd_dev_buffer(&aesd_device);

    unregister_chrdev_region(devno, 1);
}

// aesd_dev helper functions

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_dev_find_entry_offset_for_fpos(struct aesd_dev *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    size_t count = 0;
    uint8_t num_entries;

    uint8_t read_index = buffer->out_offs;
    uint8_t write_index = buffer->in_offs;

    int i;
 
    if (buffer->full){
        num_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    else{
        num_entries = (write_index-read_index)% AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    
    for (i = 0; i < num_entries; i++){
        uint8_t curr_index = (read_index + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        size_t curr_size = buffer->entry[curr_index].size;
        
        if (count + curr_size > char_offset){
            *entry_offset_byte_rtn = char_offset - count;
            return &buffer->entry[curr_index];
        }
        count += curr_size;
    }

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_dev_add_entry(struct aesd_dev *buffer, const struct aesd_buffer_entry *add_entry)
{
    // insert entry and increment insert index
    uint8_t write_index = buffer->in_offs;
    uint8_t new_write_index = (write_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    
    buffer->entry[write_index] = *add_entry;
    buffer->in_offs = new_write_index;
    
    // buffer full, increment read index
    if(buffer->full){   
        buffer->out_offs = new_write_index;
    }
    // buffer not full, but becomes full with new entry
    else if (new_write_index == buffer->out_offs){
        buffer->full = true;
    }
}

void free_all_aesd_dev_buffer(struct aesd_dev *buffer){
    int i;
    if (buffer->full){
        for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
            kfree(buffer->entry[i].buffptr);
        }
    }
    else{
        // since device not full, out_off != in_off unless empty
        for (i = buffer->out_offs; i != buffer->in_offs; i++){
            if (i > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED){
                i = i % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            }
            kfree(buffer->entry[i].buffptr);
        }
    }
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

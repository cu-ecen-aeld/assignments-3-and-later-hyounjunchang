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
uint8_t aesd_dev_find_entry_offset_for_fpos(struct aesd_dev *buffer,
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

// fpos = file_offset to read from
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    unsigned long bytes_read = 0;
    uint8_t buf_index;
    size_t start_char_index;
    size_t end_char_index = 0;
    size_t current_buffer_size;
    size_t new_fpos = 0;
    int i;

    char* kernel_buf;
    struct aesd_dev *dev;
    size_t bytes_left_curr_entry;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    
    
    dev = filp->private_data;
    //acquire mutex
    down(dev->sem);

    kernel_buf = kmalloc(count, GFP_KERNEL);
    if (kernel_buf == NULL){
        PDEBUG("kmalloc error");
        retval = -ENOMEM;
        goto out;
    }

    // get offset_to-read_from
    buf_index = aesd_dev_find_entry_offset_for_fpos(dev, (size_t)*f_pos, &start_char_index);

    if (buf_index == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED){
        PDEBUG("Invalid fpos");
        retval = -EFAULT;
        goto out;
    }
    
   
    // first start_char_index is given from ased_dev_find_entry_offset_for_fpos, rest are always 0
    while (bytes_read < count){
        // reached end of pointer, device is not full
        if (buf_index == dev->in_offs && dev->full == false){
            goto copy_to_userspace;
        }

        current_buffer_size = dev->entry[buf_index].size;
        bytes_left_curr_entry = current_buffer_size - start_char_index;


        // current entry not fully read
        if (bytes_read + bytes_left_curr_entry > count){
            memcpy(kernel_buf+bytes_read, dev->entry[buf_index].buffptr+start_char_index, count-bytes_read);
            
            bytes_read = count;
            end_char_index = start_char_index + (count - bytes_read);
            goto copy_to_userspace;
        }
        // currenty entry fully read, end of read
        else if (bytes_read + bytes_left_curr_entry == 0){
            memcpy(kernel_buf+bytes_read, dev->entry[buf_index].buffptr+start_char_index, bytes_left_curr_entry);

            bytes_read = count;
            buf_index = (buf_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            end_char_index = 0;

            dev->full = false;
            goto copy_to_userspace;
        }
        else{
            memcpy(kernel_buf+bytes_read, dev->entry[buf_index].buffptr+start_char_index, bytes_left_curr_entry);

            start_char_index = 0;
            buf_index = (buf_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            end_char_index = 0;

            bytes_read += bytes_left_curr_entry;
            dev->full = false;
        }
    }

    copy_to_userspace:
    retval = copy_to_user(buf, kernel_buf, bytes_read);
    PDEBUG("copy_to_user for read: %zu bytes", retval);

    // update dev structure
    dev->out_offs = buf_index;
    dev->read_start_index = end_char_index;

    // update fpos
    for (i = 0; i < buf_index; i++){
        new_fpos += dev->entry[i].size;
    }
    new_fpos += end_char_index;
    *f_pos = new_fpos;
    
    out:
    kfree(kernel_buf);
    //release mutex
    up(dev->sem);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev;
    char* new_buf;
    size_t curr_size, new_bufsize;
    int i;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    dev = filp->private_data;
    //acquire mutex
    down(dev->sem);

    // we ignore f_pos for this assignment, and write to buffer
    curr_size = dev->curr_input.size;
    new_bufsize = curr_size + count;

    new_buf = kmalloc(new_bufsize, GFP_KERNEL);

    if (new_buf == NULL){
        PDEBUG("kmalloc error");
        retval = -ENOMEM;
        goto out;
    }

    // copy existing buffer to new one
    if (curr_size){
        memcpy(new_buf, dev->curr_input.buffptr, curr_size);
        kfree(dev->curr_input.buffptr);
        dev->curr_input.buffptr = new_buf;
    }
    
    
    // iterate through each byte received
    for (i = 0; i < count; i++){
        dev->curr_input.buffptr[curr_size] = buf[i];
        dev->curr_input.size = dev->curr_input.size + 1;

        if (buf[i] == '\n'){
            // calculate new in_off
            uint8_t new_dev_in_offs = (dev->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

            // entry is full
            if (dev->full){
                dev->out_offs = (dev->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                kfree(dev->entry[dev->in_offs].buffptr);
            }
            // buffer full with new entry
            else if (new_dev_in_offs == dev->out_offs){
                dev->full = true;
            }

            // update entry
            dev->entry[dev->in_offs].buffptr = dev->curr_input.buffptr;
            dev->entry[dev->in_offs].size =  dev->curr_input.size;
            dev->in_offs = new_dev_in_offs;

            // update current input
            dev->curr_input.buffptr = NULL;
            dev->curr_input.size = 0;

            // create new memory
            new_bufsize = count - i;
            new_buf = kmalloc(new_bufsize, GFP_KERNEL);
            if (new_buf == NULL){
                PDEBUG("kmalloc error, partial write done");
                retval = (i+1);
                goto out;
            }
            dev->curr_input.buffptr = new_buf;
        }

    }
    
    out:
    //release mutex
    up(dev->sem);
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
    int i;
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

    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
        aesd_device.entry[i].buffptr = NULL;
        aesd_device.entry[i].size = 0;
    }
    
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

// returns index of buffer pointer if position found, returns AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED if position not found
// entry_offset_byte_rtn is updated
uint8_t aesd_dev_find_entry_offset_for_fpos(struct aesd_dev *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    int i;
    size_t count = 0;
    
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
        size_t curr_size = buffer->entry[i].size;
        if (count + curr_size > char_offset){
            *entry_offset_byte_rtn = char_offset - count;
            return i;
        }
        count += curr_size;
    }

    return AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
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

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

// Claude AI chat history: https://claude.ai/chat/63e236bb-6020-40a9-ae6b-258c7edeb090
// Initial code written by me was buggy, used Claude to fix bugs

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

    char* kernel_buf;
    struct aesd_dev *dev;
    size_t bytes_left_curr_entry;
    uint8_t entries_read, total_entries;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    
    
    dev = filp->private_data;
    //acquire mutex
    down(&dev->sem);

    // Update file pointer... on open, in case it is overrun
    if (*f_pos < dev->total_bytes_evicted){
        *f_pos = dev->total_bytes_evicted;  // snap f_pos forward on stale open
    }

    kernel_buf = kmalloc(count, GFP_KERNEL);
    if (kernel_buf == NULL){
        PDEBUG("kmalloc error");
        retval = -ENOMEM;
        goto out;
    }

    // get offset_to-read_from
    buf_index = aesd_dev_find_entry_offset_for_fpos(dev, (size_t)*f_pos, &start_char_index);

    if (buf_index == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED){
        PDEBUG("EOF");
        retval = 0;  // EOF
        goto out;
    }

    PDEBUG("buf_index=%d entry size=%zu buffptr=%p", buf_index, dev->entry[buf_index].size, dev->entry[buf_index].buffptr);
    
    entries_read = 0;
    if (dev->full && buf_index == dev->in_offs) {
        total_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else {
        total_entries = (dev->in_offs - buf_index + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
                        % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    while (bytes_read < count){
        // with checking if buf_index has lapped back to in_offs accounting for full:
        if (buf_index == dev->in_offs && !dev->full){
            goto copy_to_userspace;
        }
        // AND add a separate check for having read all entries when full:
        if (entries_read >= total_entries){
            goto copy_to_userspace;
        }
        entries_read++;

        current_buffer_size = dev->entry[buf_index].size;
        bytes_left_curr_entry = current_buffer_size - start_char_index;


        // current entry not fully read
        if (bytes_read + bytes_left_curr_entry > count){
            memcpy(kernel_buf+bytes_read, dev->entry[buf_index].buffptr+start_char_index, count-bytes_read);
            
            end_char_index = start_char_index + (count - bytes_read);
            bytes_read = count;
            goto copy_to_userspace;
        }
        // currenty entry fully read, end of read
        else if (bytes_read + bytes_left_curr_entry == count){
            memcpy(kernel_buf+bytes_read, dev->entry[buf_index].buffptr+start_char_index, bytes_left_curr_entry);

            bytes_read = count;
            buf_index = (buf_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            end_char_index = 0;

            goto copy_to_userspace;
        }
        else{
            memcpy(kernel_buf+bytes_read, dev->entry[buf_index].buffptr+start_char_index, bytes_left_curr_entry);

            start_char_index = 0;
            buf_index = (buf_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            end_char_index = 0;

            bytes_read += bytes_left_curr_entry;
        }
    }

    copy_to_userspace:
    // FIXED: copy_to_user returns bytes NOT copied (0 on success)
    retval = bytes_read;
    if (copy_to_user(buf, kernel_buf, bytes_read)) {
        retval = -EFAULT;
    }

    PDEBUG("copy_to_user for read: %zu bytes", retval);

    // update fpos
    *f_pos += bytes_read;
    
    out:
    kfree(kernel_buf);
    //release mutex
    up(&dev->sem);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev;
    char* new_buf = NULL;
    char* old_buf = NULL;
    size_t curr_size, new_bufsize, remaining;
    int i;

    dev = filp->private_data;
    //acquire mutex
    down(&dev->sem);


    PDEBUG("curr_size=%zu in_offs=%d out_offs=%d full=%d total_evicted=%zu",
       dev->curr_input.size, dev->in_offs, dev->out_offs, 
       dev->full, dev->total_bytes_evicted);

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

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
    }
    dev->curr_input.buffptr = new_buf;

    if (copy_from_user(new_buf+curr_size, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
    
    
    // iterate through each byte received
    for (i = 0; i < count; i++){
        if (new_buf[curr_size] == '\n'){
            uint8_t new_dev_in_offs = (dev->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            
            if (dev->full){
                dev->total_bytes_evicted += dev->entry[dev->out_offs].size;
                kfree(dev->entry[dev->out_offs].buffptr);
                dev->out_offs = (dev->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            }
            else if (new_dev_in_offs == dev->out_offs){
                dev->full = true;
                PDEBUG("buffer now full, in_offs=%d out_offs=%d", dev->in_offs, dev->out_offs);
            }

            // commit entry including '\n'
            dev->entry[dev->in_offs].buffptr = new_buf;
            dev->entry[dev->in_offs].size = curr_size + 1;
            dev->in_offs = new_dev_in_offs;

            // Debug message
            PDEBUG("committed entry[%d] size=%zu in_offs now=%d",
            dev->in_offs-1, dev->entry[(dev->in_offs-1+AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size,
            dev->in_offs);

            // allocate new buffer for remaining bytes
            remaining = count - i - 1;
            // copy remaining bytes into new buffer
            old_buf = new_buf;
            new_buf = kmalloc(remaining + 1, GFP_KERNEL);
                        if (new_buf == NULL){
                dev->curr_input.buffptr = NULL;
                dev->curr_input.size = 0;
                retval = (i+1);
                goto out;
            }

            if (remaining > 0){
                memcpy(new_buf, old_buf + curr_size + 1, remaining);
            }


            dev->curr_input.buffptr = new_buf;
            dev->curr_input.size = remaining;
            curr_size = 0;  // reset — next byte is at index 0 of new buffer
            continue;       // don't increment curr_size below
        }
        curr_size++;
    }
        dev->curr_input.buffptr = new_buf;
    dev->curr_input.size = curr_size;
    retval = count;
    
    out:
    //release mutex
    up(&dev->sem);
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
    aesd_device.in_offs = 0;
    aesd_device.full = false;
    aesd_device.total_bytes_evicted = 0;

    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
        aesd_device.entry[i].buffptr = NULL;
        aesd_device.entry[i].size = 0;
    }
    
    // intialize mutex
    sema_init(&aesd_device.sem, 1);

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

// returns index of buffeelse ifr pointer if position found, returns AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED if position not found
// entry_offset_byte_rtn is updated
uint8_t aesd_dev_find_entry_offset_for_fpos(struct aesd_dev *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn ){
    size_t count = 0;
    uint8_t num_entries;

    uint8_t read_index = buffer->out_offs;
    uint8_t write_index = buffer->in_offs;

    int i;
    PDEBUG("find_entry: char_offset=%zu out_offs=%d in_offs=%d full=%d total_evicted=%zu",
       char_offset, buffer->out_offs, buffer->in_offs, buffer->full, buffer->total_bytes_evicted);

    if (char_offset < buffer->total_bytes_evicted){
        return AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;  // EOF - data is gone
    }else {
        char_offset -= buffer->total_bytes_evicted;
    }

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
            return curr_index;
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

        for (i = buffer->out_offs; i != buffer->in_offs; i = (i+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED){
            kfree(buffer->entry[i].buffptr);
        }
    }
    kfree(buffer->curr_input.buffptr);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

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
#include <linux/slab.h>     
#include <linux/uaccess.h>   
#include <linux/string.h>   
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("vatsashiva"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
  
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev          *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t                    entry_offset;     /* byte index inside entry */
    size_t                    copied_total = 0;
    size_t                    bytes_to_copy;
    ssize_t                   retval = 0;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

     while (copied_total < count) {
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                    &dev->buffer,
                    (size_t)*f_pos,
                    &entry_offset);

    if (!entry)
        break;
    
    bytes_to_copy = min(count - copied_total,
                            entry->size - entry_offset);

        if (copy_to_user(buf + copied_total,
                         entry->buffptr + entry_offset,
                         bytes_to_copy)) {
            retval = -EFAULT;
            goto out;
        }

        copied_total += bytes_to_copy;
        *f_pos += bytes_to_copy;
    }
    
     retval = copied_total;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
   struct aesd_dev          *dev = filp->private_data;
    const char               *evicted_ptr;
    char                     *new_working;
    char                     *nl_pos;        /* first '\n' in the  buffer */
    size_t                    commit_len;
    struct aesd_buffer_entry  new_entry;
    ssize_t                   retval = -ENOMEM;
 
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
 
    if (!count)
        return 0;
 
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
 
    //  add incoming bytes to the buffer 
 
    new_working = krealloc(dev->working_buf,
                           dev->working_buf_size + count,
                           GFP_KERNEL);
    if (!new_working)
        goto out;
 
    dev->working_buf = new_working;
 
    if (copy_from_user(dev->working_buf + dev->working_buf_size, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
 
    dev->working_buf_size += count;
    retval = count;  
 
    //2 ---terminated by '\n'
 
    while ((nl_pos = memchr(dev->working_buf, '\n',
                            dev->working_buf_size)) != NULL) {
 
        commit_len = nl_pos - dev->working_buf + 1; /* include '\n' */
 
        new_entry.buffptr = kmalloc(commit_len, GFP_KERNEL);
        if (!new_entry.buffptr) {
            retval = -ENOMEM;
            goto out;
        }
        memcpy((char *)new_entry.buffptr, dev->working_buf, commit_len);
        new_entry.size = commit_len;
 
        /*
         * Add to circular buffer.  If the buffer was full,
         * add_entry returns the oldest buffptr so we can free it.
         */
	evicted_ptr = aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);

	if (evicted_ptr)
    		kfree(evicted_ptr);


        dev->working_buf_size -= commit_len;
        if (dev->working_buf_size) {
            memmove(dev->working_buf,
                    dev->working_buf + commit_len,
                    dev->working_buf_size);
        } else {
            
            kfree(dev->working_buf);
            dev->working_buf      = NULL;
            dev->working_buf_size = 0;
            break;
        }
    }
 
out:
    mutex_unlock(&dev->lock);
    return retval; 
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t new_pos = 0;
    loff_t total_size = 0;
    size_t index;
    size_t entry_count;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry_count = dev->buffer.full ?
        AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : dev->buffer.in_offs;

    for (index = 0; index < entry_count; index++) {
        size_t actual_index =
            (dev->buffer.out_offs + index) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        total_size += dev->buffer.entry[actual_index].size;
    }

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;

        case SEEK_CUR:
            new_pos = filp->f_pos + offset;
            break;

        case SEEK_END:
            new_pos = total_size + offset;
            break;

        default:
            mutex_unlock(&dev->lock);
            return -EINVAL;
    }

    if (new_pos < 0 || new_pos > total_size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = new_pos;

    mutex_unlock(&dev->lock);
    return new_pos;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    size_t entry_count;
    size_t index;
    size_t actual_index;
    size_t cumulative_size = 0;

    if (cmd != AESDCHAR_IOCSEEKTO)
        return -EINVAL;

    if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)))
        return -EFAULT;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry_count = dev->buffer.full ?
        AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : dev->buffer.in_offs;

    if (seekto.write_cmd >= entry_count) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    for (index = 0; index < seekto.write_cmd; index++) {
        actual_index =
            (dev->buffer.out_offs + index) %
            AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        cumulative_size += dev->buffer.entry[actual_index].size;
    }

    actual_index =
        (dev->buffer.out_offs + seekto.write_cmd) %
        AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    if (seekto.write_cmd_offset >= dev->buffer.entry[actual_index].size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = cumulative_size + seekto.write_cmd_offset;

    mutex_unlock(&dev->lock);
    return 0;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek  =  aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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

    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    
    uint8_t  index;
    struct aesd_buffer_entry *entry;
    
    cdev_del(&aesd_device.cdev);

    
     
     AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr) {
            kfree(entry->buffptr);
            entry->buffptr = NULL;
        }
    }

    if (aesd_device.working_buf) {
        kfree(aesd_device.working_buf);
        aesd_device.working_buf      = NULL;
        aesd_device.working_buf_size = 0;
    }

    mutex_destroy(&aesd_device.lock);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

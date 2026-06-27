/*
 * chardev.c - Simple Character Device Driver
 *
 * Demonstrates:
 *   - Character device lifecycle (open/read/write/close)
 *   - file_operations structure
 *   - copy_to_user / copy_from_user
 *   - Major/minor number allocation
 *   - /dev/ node auto-creation via class/device
 *
 * Relevance to ONU firmware (EN7528HU TCLinux):
 *   This is exactly how /dev/omci works.
 *   pon_mac.ko creates a char device and
 *   omci_app reads OMCI frames from it via read().
 *
 * Test:
 *   sudo insmod chardev.ko
 *   echo "hello kernel" | sudo tee /dev/mydev
 *   cat /dev/mydev
 *   sudo rmmod chardev
 *   dmesg | tail -20
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define DEVICE_NAME  "mydev"
#define CLASS_NAME   "mydev_class"
#define BUF_SIZE     256

static int          major;
static char         kbuf[BUF_SIZE];
static size_t       kbuf_len = 0;
static struct cdev  my_cdev;
static struct class *my_class;
static dev_t        dev_num;
static DEFINE_MUTEX(mydev_mutex);

/*
 * mydev_open - called when userspace opens /dev/mydev
 * e.g: open("/dev/mydev", O_RDWR)
 */
static int mydev_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&mydev_mutex)) {
        pr_warn("mydev: device busy\n");
        return -EBUSY;
    }
    pr_info("mydev: opened by PID %d\n", current->pid);
    return 0;
}

/*
 * mydev_read - called when userspace reads /dev/mydev
 * e.g: read(fd, buf, len) or cat /dev/mydev
 *
 * copy_to_user: safely copies from kernel buffer to
 *               userspace buffer. Cannot directly deref
 *               user pointers in kernel space — different
 *               address space, may be swapped out.
 */
static ssize_t mydev_read(struct file *file,
                           char __user *ubuf,
                           size_t len,
                           loff_t *off)
{
    size_t to_copy;

    if (*off >= kbuf_len)
        return 0;   /* EOF */

    to_copy = min(len, kbuf_len - (size_t)*off);

    if (copy_to_user(ubuf, kbuf + *off, to_copy)) {
        pr_err("mydev: copy_to_user failed\n");
        return -EFAULT;
    }

    *off += to_copy;
    pr_info("mydev: read %zu bytes -> [%.*s]\n",
            to_copy, (int)to_copy, kbuf);
    return to_copy;
}

/*
 * mydev_write - called when userspace writes to /dev/mydev
 * e.g: write(fd, buf, len) or echo "data" > /dev/mydev
 *
 * copy_from_user: safely copies from userspace buffer
 *                 to kernel buffer.
 */
static ssize_t mydev_write(struct file *file,
                            const char __user *ubuf,
                            size_t len,
                            loff_t *off)
{
    size_t to_copy = min(len, (size_t)(BUF_SIZE - 1));

    if (copy_from_user(kbuf, ubuf, to_copy)) {
        pr_err("mydev: copy_from_user failed\n");
        return -EFAULT;
    }

    kbuf[to_copy] = '\0';
    kbuf_len      = to_copy;

    pr_info("mydev: wrote %zu bytes <- [%s]\n",
            to_copy, kbuf);
    return to_copy;
}

/*
 * mydev_release - called when userspace closes /dev/mydev
 */
static int mydev_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&mydev_mutex);
    pr_info("mydev: closed\n");
    return 0;
}

/*
 * file_operations - maps system calls to driver functions
 * This is the core structure of every Linux char driver.
 * Same structure used by pon_mac.ko for /dev/omci.
 */
static const struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = mydev_open,
    .read    = mydev_read,
    .write   = mydev_write,
    .release = mydev_release,
};

static int __init chardev_init(void)
{
    int ret;

    /* Step 1: allocate major number dynamically */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("mydev: failed to alloc major number\n");
        return ret;
    }
    major = MAJOR(dev_num);

    /* Step 2: init and register character device */
    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;

    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_num, 1);
        pr_err("mydev: cdev_add failed\n");
        return ret;
    }

    /* Step 3: create /sys/class/mydev_class */
    my_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(my_class)) {
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(my_class);
    }

    /* Step 4: create /dev/mydev node automatically */
    device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);

    pr_info("mydev: loaded — major=%d /dev/%s created\n",
            major, DEVICE_NAME);
    return 0;
}

static void __exit chardev_exit(void)
{
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);
    pr_info("mydev: unloaded\n");
}

module_init(chardev_init);
module_exit(chardev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ONU Firmware Engineer");
MODULE_DESCRIPTION("Char Device Driver — models /dev/omci pattern");
MODULE_VERSION("1.0");

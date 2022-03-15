#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "bignum.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

#define ULL_TO_USER_BUF(val, buf, size)                                 \
    ({                                                                  \
        char *tmp = vmalloc(21);                                        \
        snprintf(tmp, 21, "%llu", val);                                 \
        size_t failed = copy_to_user(buf, tmp, min(size, (size_t) 21)); \
        vfree(tmp);                                                     \
        failed;                                                         \
    })

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

static enum FIB_MODES {
    FIB_MODE_BASIC_64 = 0,
    FIB_MODE_BASIC_BIG = 1,
    FIB_MODE_FAST_DOUBLING_64 = 2,
    FIB_MODE_FAST_DOUBLING_BIG = -1,
} mode = FIB_MODE_BASIC_64;

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

static long long fib_basic_64(uint64_t target, char *buf, size_t size)
{
    unsigned long long fib[2] = {0, 1};

    for (int i = 1; i <= target; i++) {
        swap(fib[0], fib[1]);
        fib[0] += fib[1];
    }
    return ULL_TO_USER_BUF(fib[0], buf, size);
}

static long long fib_fast_64(uint64_t target, char *buf, size_t size)
{
    unsigned long long result = !!target;
    if (target > 2) {
        // find first 1
        uint8_t count = 63 - __builtin_clzll(target);
        uint64_t fib_n0 = 1, fib_n1 = 1;

        for (uint64_t i = count, fib_2n0, fib_2n1, mask; i-- > 0;) {
            fib_2n0 = fib_n0 * ((fib_n1 << 1) - fib_n0);
            fib_2n1 = fib_n0 * fib_n0 + fib_n1 * fib_n1;

            mask = -!!(target & (1UL << i));
            fib_n0 = (fib_2n0 & ~mask) + (fib_2n1 & mask);
            fib_n1 = (fib_2n0 & mask) + fib_2n1;
        }
        result = fib_n0;
    }
    return ULL_TO_USER_BUF(result, buf, size);
}

static long long fib_basic_big(uint64_t target, char *buf, size_t size)
{
    struct list_head *lgr = bignum_new(1), *slr = bignum_new(0);

    for (uint64_t i = 0; i < target; ++i) {
        bignum_add_to_smaller(lgr, slr);
        swap(lgr, slr);
    }

    char *result = bignum_to_string(slr);
    size_t failed = copy_to_user(buf, result, size);
    vfree(result);
    bignum_free(lgr);
    bignum_free(slr);
    return failed;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    switch (mode) {
    case FIB_MODE_BASIC_64:
        printk(KERN_INFO "MODE = BASIC_64.\n");
        return (ssize_t) fib_basic_64(*offset, buf, size);

    case FIB_MODE_FAST_DOUBLING_64:
        printk(KERN_INFO "MODE = FAST_DOUBLING_64.\n");
        return (ssize_t) fib_fast_64(*offset, buf, size);

    case FIB_MODE_BASIC_BIG:
        printk(KERN_INFO "MODE = BASIC_BIG.\n");
        return (ssize_t) fib_basic_big(*offset, buf, size);

    case FIB_MODE_FAST_DOUBLING_BIG:
    default:
        return copy_to_user(buf, "TO BE IMPLEMENTED.", size);
    }
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    int val, err = kstrtoint_from_user(buf, size, 10U, &val);

    if (err == ERANGE || err == EINVAL)
        pr_err("OVERFLOW OR NOT A NUMBER STRING.\n");
    else
        switch (val) {
        case FIB_MODE_BASIC_64:
            pr_info("SET MODE : BASIC_64.\n");
            mode = FIB_MODE_BASIC_64;
            return FIB_MODE_BASIC_64;

        case FIB_MODE_FAST_DOUBLING_64:
            pr_info("SET MODE : FAST_64.\n");
            mode = FIB_MODE_FAST_DOUBLING_64;
            return FIB_MODE_FAST_DOUBLING_64;

        case FIB_MODE_BASIC_BIG:
            pr_info("SET MODE : BASIC_BIG.\n");
            mode = FIB_MODE_BASIC_BIG;
            return FIB_MODE_BASIC_BIG;

        case FIB_MODE_FAST_DOUBLING_BIG:
        default:
            pr_warn("TO BE IMPLEMENTED.\n");
            break;
        }

    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);

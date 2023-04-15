#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/version.h>

#include "uwurandom_core.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("urandom but better");
MODULE_AUTHOR("valadaptive");
MODULE_VERSION("0.1");

static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .open = dev_open,
    .release = dev_release
};

static int major;
static struct class* dev_class;
static dev_t dev_num;

static ssize_t
dev_read(struct file *fp, char *buf, size_t n, loff_t *of) {
    uwu_state* state = fp->private_data;

    int result = uwu_write_chars(state, buf, n);
    if (result < 0) return result;

    return n;
}

static int
dev_open(struct inode *ino, struct file *fp) {
    size_t len = sizeof(uwu_state);

    uwu_state *data = kmalloc(len, GFP_KERNEL);

    if (data == NULL) {
        return -ENOMEM;
    }

    unsigned int* rng_buf = kmalloc(RAND_SIZE * sizeof(unsigned int), GFP_KERNEL);

    if (rng_buf == NULL) {
        kfree(data);
        return -ENOMEM;
    }

    data->prev_op = -1;
    data->current_op = -1;
    data->rng_buf = rng_buf;
    // mark the offset into rng_buf as just past the end of the buffer,
    // meaning we'll regenerate the buffer the first time we ask for random bytes
    data->rng_idx = RAND_SIZE;
    generate_new_ops(data);

    fp->private_data = data;

    return 0;
}

static int
dev_release(struct inode *ino, struct file *fp) {
    kfree(((uwu_state*) fp->private_data)->rng_buf);
    kfree(fp->private_data);
    fp->private_data = NULL;
    return 0;
}

// Kernel versions 6.2 and up take a const* to devices in dev_uevent
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(6,2,0) )
typedef const struct device* uevent_dev_ptr;
#else
typedef struct device* uevent_dev_ptr;
#endif

// Make sure all users can thoroughly enjoy /dev/uwurandom
static int
dev_uevent(uevent_dev_ptr dev, struct kobj_uevent_env* env) {
    int result = add_uevent_var(env, "DEVMODE=%#o", 0666);
    if (!result) {
        return result;
    }
    return 0;
}

static void
kexit(void) {
    device_destroy(dev_class, dev_num);
    class_destroy(dev_class);
    unregister_chrdev(major, "uwurandom");
    return;
}

static int
kinit(void) {
    struct device* pDev;

    major = register_chrdev(0, "uwurandom", &fops);

    if (major < 0)
        return major;

    dev_num = MKDEV(major, 0);

    // Create device class and device automatically. So nice!
    dev_class = class_create(THIS_MODULE, "uwurandom");
    if (IS_ERR(dev_class)) {
        unregister_chrdev(major, "uwurandom");
        return PTR_ERR(dev_class);
    }

    dev_class->dev_uevent = dev_uevent;

    pDev = device_create(dev_class, NULL, dev_num, NULL, "uwurandom");
    if (IS_ERR(pDev)) {
        class_destroy(dev_class);
        unregister_chrdev(major, "uwurandom");
        return PTR_ERR(pDev);
    }


    return 0;
}

module_init(kinit);
module_exit(kexit);

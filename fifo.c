#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include "fifo.h"

MODULE_LICENSE("GPL");

/*
 * Due to unknown reason I have to use static data here
 * If they are allocated dynamically the cleanup func does not work
 */
static size_t fifo_nr_devs = NR_DEVS;
module_param(fifo_nr_devs, ulong, S_IRUGO);
static struct fifo_dev fifo_dev[MAX_NR_DEVS];
static dev_t fifo_devno[MAX_NR_DEVS];
static dev_t fifo_major;
static dev_t fifo_minor;	/* Minor number of the first device */

/* FIXME: I'm not pretty sure about the error code return
 * if we write on the read port or vice versa.
 */
int fifo_open(struct inode *inode, struct file *filp)
{
	struct fifo_dev *dev;

	if (IS_EVEN(iminor(inode)) &&
	    ((filp->f_flags & O_ACCMODE) != O_WRONLY))
		return -EPERM;
	else if (IS_ODD(iminor(inode)) &&
		 ((filp->f_flags & O_ACCMODE) != O_RDONLY))
		return -EPERM;

	dev = container_of(inode->i_cdev, struct fifo_dev, f_cdev);
	filp->private_data = dev;

	return 0;
}

/* This function is not reentrant
 * NEVER NEVER call it outside fifo_read()
 */
static inline ssize_t __do_read(struct fifo_dev *dev, char __user *buf,
				size_t count)
{
	count = min(dev->f_size, count);
	if (is_empty(dev)) {
		return 0;
	} else if (dev->f_wp > dev->f_rp) {
		count = min(count, (size_t)(dev->f_wp - dev->f_rp));
	} else {
		count = min(count, (size_t)(dev->f_end - dev->f_rp));
	}

	if (copy_to_user(buf, dev->f_rp, count))
		return -EFAULT;
	dev->f_size -= count;
	dev->f_rp += count;
	if (dev->f_rp == dev->f_end)
		dev->f_rp = dev->f_data;

#ifdef DEBUG
	if (unlikely(dev->f_rp > dev->f_end)) {
		pr_emerg("Error occured on reading!\n"
			 "dev->f_rp = 0x%lx, dev->f_end = 0x%lx\n",
			 (unsigned long)dev->f_rp,
			 (unsigned long)dev->f_end);
		return -EFAULT;
	}
	if (unlikely(dev->f_rp < dev->f_data)) {
		pr_emerg("Error occured on reading!\n"
			 "dev->f_rp = 0x%lx, dev->f_data = 0x%lx\n",
			 (unsigned long)dev->f_rp,
			 (unsigned long)dev->f_data);
		return -EFAULT;
	}
#endif

	return count;
}

ssize_t fifo_read(struct file *filp, char __user *buf,
		  size_t count, loff_t *offp)
{
	ssize_t retval = 0;
	struct fifo_dev *dev;

	dev = (struct fifo_dev *)(filp->private_data);

	if (down_interruptible(&dev->f_sem))
		return -ERESTARTSYS;
#ifdef DEBUG
	if (is_empty(dev)) {
		pr_debug("The fifo is empty\n"
			 "dev->f_rp = 0x%lx, dev->f_wp = 0x%lx\n"
			 "dev->f_data = 0x%lx, dev->f_end = 0x%lx\n"
			 "dev->f_size = %lu\n",
			 (unsigned long)dev->f_rp,
			 (unsigned long)dev->f_wp,
			 (unsigned long)dev->f_data,
			 (unsigned long)dev->f_end,
			 (unsigned long)dev->f_size);
	}
#endif

	while (is_empty(dev)) {
		up(&dev->f_sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(dev->f_outq, !is_empty(dev)) < 0)
			return -ERESTARTSYS;
		if (down_interruptible(&dev->f_sem))
			return -ERESTARTSYS;
	}

	retval = __do_read(dev, buf, count);

	up(&dev->f_sem);
	wake_up_interruptible(&dev->f_inq);
	return retval;
}

static inline ssize_t __do_write(struct fifo_dev *dev, const char __user *buf,
				 size_t count)
{
	if (is_full(dev))
		return 0;
	else if (dev->f_wp >= dev->f_rp)
		count = min(count, (size_t)(dev->f_end - dev->f_wp));
	else
		count = min(count, (size_t)(dev->f_rp - dev->f_wp));

	if (copy_from_user(dev->f_wp, buf, count)) {
		return -EFAULT;
	}
	dev->f_size += count;
	dev->f_wp += count;
	if ((dev->f_wp == dev->f_end))
		dev->f_wp = dev->f_data;

#ifdef DEBUG
	if (unlikely(dev->f_wp > dev->f_end )) {
		pr_emerg("Error occured on writing!\n"
			 "dev->f_wp = 0x%lx, dev->f_end = 0x%lx\n",
			 (unsigned long)dev->f_wp,
			 (unsigned long)dev->f_end);
		return -EFAULT;
	}
	if (unlikely(dev->f_wp < dev->f_data)) {
		pr_emerg("Error occured on writing!\n"
			 "dev->f_wp = 0x%lx, dev->f_data = 0x%lx\n",
			 (unsigned long)dev->f_wp,
			 (unsigned long)dev->f_data);
		return -EFAULT;
	}
#endif

	return count;
}

static ssize_t fifo_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *offp)
{
	ssize_t retval = 0;
	struct fifo_dev *dev;

	dev = filp->private_data;

	if (down_interruptible(&dev->f_sem)) {
		pr_emerg("write cannot get the first lock!\n");
		return -ERESTARTSYS;
	}
#ifdef DEBUG
	if (is_full(dev)) {
		pr_debug("The fifo is full\n"
			 "dev->f_rp = 0x%lx, dev->f_wp = 0x%lx\n"
			 "dev->f_data = 0x%lx, dev->f_end = 0x%lx\n"
			 "dev->f_size = %lu\n",
			 (unsigned long)dev->f_rp,
			 (unsigned long)dev->f_wp,
			 (unsigned long)dev->f_data,
			 (unsigned long)dev->f_end,
			 (unsigned long)dev->f_size);
	}
#endif

	while (is_full(dev)) {
		up(&dev->f_sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(dev->f_inq, !is_full(dev)) < 0)
			return -ERESTARTSYS;
		if (down_interruptible(&dev->f_sem))
			return -ERESTARTSYS;
	}

	retval = __do_write(dev, buf, count);

	up(&dev->f_sem);
	wake_up_interruptible(&dev->f_outq);
	return retval;
}

static const struct file_operations fifo_ops = {
	.owner = THIS_MODULE,
	.open = fifo_open,
	.read = fifo_read,
	.write = fifo_write
};

static int fifo_setup_dev(struct fifo_dev *dev, const int index)
{
	int err = 0;
	dev_t devno_in;
	dev_t devno_out;

	if (down_interruptible(&dev->f_sem))
		return -ERESTARTSYS;
	devno_in = MKDEV(fifo_major, fifo_minor + (index << 1));
	devno_out = MKDEV(fifo_major, fifo_minor + (index << 1) + 1);
	cdev_init(&dev->f_cdev, &fifo_ops);
	err = cdev_add(&dev->f_cdev, devno_in, 1);
	err = cdev_add(&dev->f_cdev, devno_out, 1);
	if (err < 0)
		goto out;
	dev->f_data = (char *)kmalloc(PAGE_SIZE << ORDER, GFP_KERNEL);
	dev->f_end = dev->f_data + (PAGE_SIZE << ORDER);
	dev->f_size = 0;
	dev->f_rp = dev->f_data;
	dev->f_wp = dev->f_data;
	init_waitqueue_head(&dev->f_inq);
	init_waitqueue_head(&dev->f_outq);

out:
	up(&dev->f_sem);
	return err;
}

static int __init fifo_init(void)
{
	int err = 0;
	int i = 0;

	if (fifo_nr_devs > MAX_NR_DEVS) {
		pr_err("Too many devices\n");
		/* TODO: Add an error number */
		return -1;
	}

	err = alloc_chrdev_region(fifo_devno, 0, fifo_nr_devs, "fifo");
	if (err < 0)
		return err;
	fifo_major = MAJOR(fifo_devno[0]);
	for (i = 0; i < fifo_nr_devs; i++) {
		sema_init(&fifo_dev[i].f_sem, 1);
		mutex_init(&fifo_dev[i].f_mtx);
		err = fifo_setup_dev(&fifo_dev[i], i);
		if (err < 0)
			return err;
	}
	return err;
}

static void __exit fifo_exit(void)
{
	int i = 0;

	for (i = 0; i < fifo_nr_devs; i++) {
		unregister_chrdev_region(fifo_devno[i], fifo_nr_devs);
		cdev_del(&fifo_dev[i].f_cdev);
		kfree(fifo_dev[i].f_data);
	}
}

module_init(fifo_init);
module_exit(fifo_exit);

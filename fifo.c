#include <linux/module.h>
#include <linux/init.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "fifo.h"

MODULE_LICENSE("GPL");

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
static ssize_t __do_read(struct fifo_dev *dev, char __user *buf,
		size_t count)
{
	count = min(dev->f_size, count);
	if (dev->f_wp == dev->f_rp)
		return 0;
	else if (dev->f_wp > dev->f_rp)
		count = min(count, (size_t)(dev->f_wp - dev->f_rp));
	else
		count = min(count, (size_t)(dev->f_end - dev->f_rp));

	if (copy_to_user(buf, dev->f_rp, count))
		return -EFAULT;
	dev->f_size -= count;
	dev->f_rp += count;
	if (dev->f_rp == dev->f_end)
		dev->f_rp = dev->f_data;
	if (dev->f_wp == dev->f_end)
		dev->f_wp = dev->f_data;

	if (dev->f_wp > dev->f_end) {
		printk(KERN_EMERG"Error occured on reading!\n"
				"dev->f_rp = %lx, dev->f_end = %lx\n",
				(unsigned long)dev->f_rp,
				(unsigned long)dev->f_end);
		return -EFAULT;
	}

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

	while (is_empty(dev)) {
		up(&dev->f_sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(dev->f_inq, !is_empty(dev)) < 0)
			return -ERESTARTSYS;
		if (down_interruptible(&dev->f_sem))
			return -ERESTARTSYS;
	}

	retval = __do_read(dev, buf, count);
	if (retval < 0)
		goto out;
	wake_up_interruptible(&dev->f_outq);

out:
	up(&dev->f_sem);
	return retval;
}

static ssize_t __do_write(struct fifo_dev *dev, const char __user *buf,
		size_t count)
{
	if (is_full(dev))
		return 0;
	else if (dev->f_wp >= dev->f_rp)
		count = min(count, (size_t)(dev->f_end - dev->f_wp));
	else
		count = min(count, (size_t)(dev->f_rp - dev->f_wp));

	if (copy_from_user(dev->f_wp, buf, count))
		return -EFAULT;
	dev->f_size += count;
	dev->f_wp += count;
	if ((dev->f_wp == dev->f_end) && (dev->f_rp != dev->f_data))
		dev->f_wp = dev->f_data;

	if (dev->f_wp > dev->f_end) {
		printk(KERN_EMERG"Error occured on writing!\n"
				"dev->f_wp = %lx, dev->f_end = %lx\n",
				(unsigned long)dev->f_wp,
				(unsigned long)dev->f_end);
	}

	return count;
}
ssize_t fifo_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	ssize_t retval = 0;
	struct fifo_dev *dev;

	dev = filp->private_data;

	if (down_interruptible(&dev->f_sem)) {
		printk(KERN_EMERG"write cannot get the first lock!\n");
		return -ERESTARTSYS;
	}

	while (is_full(dev)) {
		up(&dev->f_sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(dev->f_outq, !is_full(dev)) < 0)
			return -ERESTARTSYS;
		if (down_interruptible(&dev->f_sem))
			return -ERESTARTSYS;
	}

	retval = __do_write(dev, buf, count);
	if (retval < 0)
		return -EFAULT;

	wake_up_interruptible(&dev->f_inq);
	up(&dev->f_sem);
	return retval;
}

static const struct file_operations fifo_ops = {
	.owner = THIS_MODULE,
	.open = fifo_open,
	.read = fifo_read,
	.write = fifo_write
};

static int fifo_setup_dev(struct fifo_dev *dev, int index)
{
	int err = 0;
	dev_t devno;

	if (down_interruptible(&dev->f_sem))
		return -ERESTARTSYS;
	devno = MKDEV(fifo_major, fifo_minor + (index << 1));
	cdev_init(&dev->f_cdev, &fifo_ops);
	err = cdev_add(&dev->f_cdev, devno, 2);
	if (err < 0)
		goto out;
	dev->f_data = (char *)__get_free_pages(GFP_KERNEL, ORDER);
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

	err = alloc_chrdev_region(&fifo_devno, 0, fifo_nr_devs, "fifo");
	if (err < 0)
		return err;
	fifo_major = MAJOR(fifo_devno);
	for (i = 0; i < fifo_nr_devs; i++) {
		sema_init(&fifo_dev[i].f_sem, 1);
		err = fifo_setup_dev(&fifo_dev[i], i);
		if (err < 0)
			return err;
	}
	return err;
}

static void __exit fifo_exit(void)
{
	int i = 0;

	unregister_chrdev_region(fifo_devno, fifo_nr_devs);
	for (i = 0; i < fifo_nr_devs; i++) {
		free_pages((unsigned long)(fifo_dev[i].f_data), ORDER);
		cdev_del(&fifo_dev[i].f_cdev);
	}
}

module_init(fifo_init);
module_exit(fifo_exit);

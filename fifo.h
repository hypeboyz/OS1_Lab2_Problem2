#ifndef FIFO_H
#define FIFO_H

#define IS_ODD(x) (x & 1)
#define IS_EVEN(x) !(x & 1)

#ifndef NR_DEVS
#define NR_DEVS 4
#endif

#ifndef ORDER
#define ORDER 1
#endif

#define DEBUG

struct fifo_dev {
	wait_queue_head_t f_inq, f_outq;
	struct semaphore f_sem;
	struct cdev f_cdev;
	size_t f_size;
	char *f_data, *f_end;
	char *f_rp, *f_wp;
};

static inline int is_empty(struct fifo_dev *dev)
{
	return (dev->f_rp == dev->f_wp);
}

static inline int is_full(struct fifo_dev *dev)
{
	if ((dev->f_wp + 1) == dev->f_end)
		return (dev->f_rp == dev->f_data);
	if ((dev->f_wp == dev->f_end) && (dev->f_rp == dev->f_data))
		return true;
	else if ((dev->f_wp == dev->f_end) && (dev->f_rp != dev->f_data))
		dev->f_wp = dev->f_data;
	return ((dev->f_wp + 1) == dev->f_rp);
}

static inline size_t free_space(struct fifo_dev *dev)
{
	return ((PAGE_SIZE << ORDER) - dev->f_size);
}

static struct fifo_dev fifo_dev[NR_DEVS >> 1];
static int fifo_nr_devs = NR_DEVS >> 1;
static dev_t fifo_devno;
static dev_t fifo_major;
static dev_t fifo_minor;	/* Minor number of the first device */

#endif

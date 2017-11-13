#ifndef FIFO_H
#define FIFO_H
#include <linux/cdev.h>
#include <linux/mutex.h>

#define IS_ODD(x) (x & 1)
#define IS_EVEN(x) !(x & 1)

#define NR_DEVS 2
#define MAX_NR_DEVS 256 >> 1

#ifndef ORDER
#define ORDER 1
#endif

struct fifo_dev {
	wait_queue_head_t f_inq, f_outq;
	struct semaphore f_sem;
	struct mutex f_mtx;
	struct cdev f_cdev;
	size_t f_size;
	char *f_data, *f_end;
	char *f_rp, *f_wp;
};

static inline int is_empty(struct fifo_dev *dev)
{
	return !dev->f_size;
}

static inline int is_full(struct fifo_dev *dev)
{
	return (PAGE_SIZE << ORDER) == dev->f_size;
}

static inline size_t free_space(struct fifo_dev *dev)
{
	return ((PAGE_SIZE << ORDER) - dev->f_size);
}

#endif

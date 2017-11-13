#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>

#define NR_DEVS 4

const char *test_str = "This is test\n";

static pthread_mutex_t count_lock;
static size_t thread_count = 0;

static pthread_cond_t latch;

static size_t thread_num;
static pthread_t tid_list[BUFSIZ];
static size_t workload;

#define DEFAULT_THREAD_NUM 2
#define DEFAULT_WORKLOAD 4096

void *reader_worker_func(void *args)
{
	int fd_r = (int )args;
	sigset_t set;
	size_t sum = 0;
	long retval;
	char *buf;
	size_t i = 0;

	sigemptyset(&set);

	pthread_mutex_lock(&count_lock);
	thread_count--;
	if (thread_count == 0) {
		pthread_cond_broadcast(&latch);
	}

	while (thread_count != 0) {
		pthread_cond_wait(&latch, &count_lock);
	}
	pthread_mutex_unlock(&count_lock);
	/* End of the barrier */

	/* printf("thread %ld starts reading\n", syscall(SYS_gettid)); */
	buf = (char *)malloc(BUFSIZ);
	for (i = 0; i < workload; i++) {
		retval = read(fd_r, buf, strlen(test_str));
		if (retval < 0) {
			perror("Error reading\n");
			free(buf);
			pthread_exit((long *)retval);
		}
		retval = fwrite(buf, BUFSIZ, 1, stdout);
		if (retval < 0) {
			perror("Cannot print\n");
			free(buf);
			pthread_exit((long *)retval);
		}
		retval = retval * strlen(test_str);
		bzero(buf, BUFSIZ);
		sum += retval;
	}

	free(buf);
	return (size_t *)retval;
}

void *wrtier_worker_func(void *args)
{
	sigset_t set;
	size_t i;
	size_t sum = 0;
	long retval;
	int fd_w = (int )args;

	sigemptyset(&set);

	pthread_mutex_lock(&count_lock);
	thread_count--;
	if (thread_count == 0) {
		pthread_cond_broadcast(&latch);
	}

	while (thread_count != 0) {
		pthread_cond_wait(&latch, &count_lock);
	}
	pthread_mutex_unlock(&count_lock);
	/* End of the barrier */

	/* printf("thread %ld starts writing\n", syscall(SYS_gettid)); */
	for (i = 0; i < workload; i++) {
		do {
			retval = write(fd_w, test_str, strlen(test_str));
			if (retval < 0) {
				perror("Error writing\n");
				pthread_exit((long *)retval);
			}
		} while (retval == 0);
		sum += strlen(test_str);
	}

	return (size_t *)strlen(test_str);
}

int main(int argc, char **argv)
{
	int fds[NR_DEVS];
	size_t i;
	char c;
	void *(*worker_ptr)(void *);
	char *path;

	thread_num = DEFAULT_THREAD_NUM;
	workload = DEFAULT_WORKLOAD;

	while ((c = (char)getopt(argc, argv, "t::w::")) != -1) {
		switch(c) {
		case 't':
			thread_num = atoll(argv[optind]);
			if (thread_num <= 0) {
				thread_num = DEFAULT_THREAD_NUM;
			}
			break;
		case 'w':
			workload = atoll(argv[optind]);
			if (workload <= 0) {
				workload = DEFAULT_WORKLOAD;
			}
			break;
		default:
			break;
		}
	}

	path = (char *)malloc(BUFSIZ);
	for (i = 0; i < NR_DEVS; i++) {
		sprintf(path, "/dev/fifo%zu", i);
		if (i & 1) {
			fds[i] = open(path, O_RDONLY);
		} else {
			fds[i] = open(path, O_WRONLY);
		}
		if (fds[i] < 0) {
			fprintf(stderr,
				"Error opening %s with error num: %d\n",
				path,
				errno);
			return errno;
		}
	}
	free(path);


	thread_count = thread_num;
	if (pthread_mutex_init(&count_lock, NULL) < 0) {
		perror("Cannot initialize count lock");
		free(tid_list);
		return errno;
	}
	if (pthread_cond_init(&latch, NULL) < 0) {
		perror("Cannot initialize latch variable");
		free(tid_list);
		return errno;
	}

	for (i = 0; i < thread_num; i++) {
		if (i & 1) {
			worker_ptr = &reader_worker_func;
		} else {
			worker_ptr = &wrtier_worker_func;
		}
		if (pthread_create(&tid_list[i],
				   NULL,
				   worker_ptr,
				   (long *)(long)fds[i % NR_DEVS]) < 0) {
			perror("Failed to create thread");
			free(tid_list);
			return errno;
		}
		if (pthread_detach(tid_list[i]) < 0) {
			perror("Failed to detach thread");
			free(tid_list);
			return errno;
		}
	}

	pthread_mutex_destroy(&count_lock);
	pthread_cond_destroy(&latch);

	pthread_exit(NULL);
	return 0;
}

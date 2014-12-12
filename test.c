#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#define TEST_STR "This is test\n"

char *buf;
int fd_r, fd_w;

void sig_chd(int signo)
{
	int retval;

	if (strcmp(buf, "") || strlen(buf)) {
		retval = read(fd_r, buf, BUFSIZ);
		if (retval < 0) {
			perror("Error reading!");
			_exit(1);
		}
	}
	if (strlen(buf))
		retval = write(STDOUT_FILENO, buf, retval);
	if (retval < 0) {
		perror("Error printing!");
		_exit(1);
	}

	_exit(0);
}

int main(void)
{
	pid_t pid;
	struct sigaction sigact;
	int retval;

	fd_w = open("/dev/fifo0", O_WRONLY);
	fd_r = open("/dev/fifo1", O_RDONLY);
	if (fd_w < 0)
		fprintf(stderr, "Error opening fifo0");
	if (fd_r < 0)
		fprintf(stderr, "Error opening fifo1");

	sigact.sa_handler = sig_chd;
	/* We don't care if the old sigmask will be
	 * reset
	 */
	sigaction(SIGCHLD, &sigact, NULL);

	pid = fork();
	if (pid < 0)
		return 1;
	else if (!pid) {
		/* child */
		int i = 0;
		size_t sum = 0;

		for (i = 0; i < 4096; i++) {
			retval = write(fd_w, TEST_STR, strlen(TEST_STR));
			if (retval < 0) {
				perror("Error writing");
				return retval;
			}
			sum += retval;
		}
		exit(0);
	} else {
		/* parent */
		int i = 0;
		size_t sum = 0;

		buf = (char *)malloc(BUFSIZ);
		while (sum < (strlen(TEST_STR) << 12)) {
			retval = read(fd_r, buf, BUFSIZ);
			if (retval < 0) {
				perror("Error reading");
				return retval;
			}
			retval = write(STDOUT_FILENO, buf, BUFSIZ);
			if (retval < 0) {
				perror("Cannot print");
				return retval;
			}
			sum += retval;
			bzero(buf, BUFSIZ);
		}

		waitpid(pid, NULL, 0);
		exit(0);
	}

	close(fd_r);
	close(fd_w);
	return 0;
}

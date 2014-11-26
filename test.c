#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define TEST_STR "This is test\n"

static void sig_chld(int signo)
{
	_exit(0);
}

int main()
{
	int fd_r, fd_w;
	pid_t pid;
	char *buf;
	int retval;

	buf = (char *)malloc(BUFSIZ);
	fd_w = open("/dev/fifo0", O_WRONLY);
	fd_r = open("/dev/fifo1", O_RDONLY);
	if (fd_w < 0)
		fprintf(stderr, "Error opening fifo0");
	if (fd_r < 0)
		fprintf(stderr, "Error opening fifo1");

	pid = fork();
	if (pid < 0)
		return 1;
	else if (!pid) { 
		/* child */
		int i = 0;
		for (i = 0; i < 4096; i++) {
			retval = write(fd_w, TEST_STR, strlen(TEST_STR) + 1);
			if (retval < 0) {
				perror("Error writing\n");
				return retval;
			}
		}
		exit(0);
	} else {
		/* parent */
		int i = 0;
		const struct sigaction sig_child = {
			.sa_handler = sig_chld,
		};

		sigaction(SIGCHLD, &sig_child, NULL);
		for (i = 0; i < 4096; i++) {
			retval = read(fd_r, buf, BUFSIZ);
			if (retval < 0) {
				perror("Error reading\n");
				return retval;
			}
			retval = write(STDOUT_FILENO, buf, BUFSIZ);
			if (retval < 0) {
				perror("Cannot print!\n");
				return retval;
			}
			bzero(buf, BUFSIZ);
		}

		waitpid(pid, NULL, 0);
		exit(0);

	}

	close(fd_r);
	close(fd_w);
	return 0;
}

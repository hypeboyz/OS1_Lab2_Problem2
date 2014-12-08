#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define TEST_STR "This is test\n"

int term = 0;

void sig_chd(int signo)
{
	term = 1;
}

int main()
{
	int fd_r, fd_w;
	char *buf;
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
		buf = (char *)malloc(BUFSIZ);

		for (i = 0; i < 4096; i++) {
			retval = write(fd_w, TEST_STR, strlen(TEST_STR) + 1);
			if (retval < 0) {
				perror("Error writing\n");
				return retval;
			}
		}
		memset(buf, (int)'y', BUFSIZ);
		retval = write(fd_w, buf, BUFSIZ);
		if (retval < 0) {
			perror("Error writing\n");
			return retval;
		} else if (retval < BUFSIZ) {
			for (i = 0; i < 3; i++)
				retval = write(fd_w, buf, BUFSIZ);
		}
		exit(0);
	} else {
		/* parent */
		int i = 0;
		buf = (char *)malloc(BUFSIZ);

		for (i = 0; i < 4096; i++) {
			if (!term) {
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
				sleep(rand() % 10);
			} else {
				if (!strlen(buf)) {
					retval = read(fd_r, buf, BUFSIZ);
					if (retval < 0) {
						perror("Cannot read from fifo!\n");
						return retval;
					}
				}
				retval = write(STDOUT_FILENO, buf, BUFSIZ);
				if (retval < 0) {
					perror("Cannot print!\n");
					return retval;
				}
				return 0;
			}
		}

		waitpid(pid, NULL, 0);
		exit(0);

	}

	close(fd_r);
	close(fd_w);
	return 0;
}

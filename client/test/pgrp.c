//	pgrp.c
//
//	Displays process group membership data after fork().
//

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>


static void sig_main (int sig)
{
	syslog(LOG_INFO, "Main: Caught signal %d", sig);
	printf("Main: Caught signal %d\n", sig);
	fflush(stdout);
	exit(1);
}

static void sig_child (int sig)
{
	syslog(LOG_INFO, "Child: Caught signal %d", sig);
	printf("Child: Caught signal %d\n", sig);
	fflush(stdout);
	exit(1);
}

int main (int argc, char **argv)
{
	int pid;
	int status;

	signal(SIGHUP, sig_main);
	signal(SIGINT, sig_main);
	signal(SIGQUIT, sig_main);
	signal(SIGTERM, sig_main);

	// Identify ourself and parentage
	printf("Main: Pid=%d, PGrp=%d, PPid=%d\n", getpid(), getpgrp(), getppid());
	fflush(stdout);

	// Fork
	if ((pid = fork()) < 0)
	{
		fprintf(stderr, "Main: fork failed: %s\n", strerror(errno));
		exit(1);
	}
	else if (pid > 0)
	{
		// Parent
		wait(&status);
		exit(0);
	}

	// Child
	signal(SIGHUP, sig_child);
	signal(SIGINT, sig_child);
	signal(SIGQUIT, sig_child);
	signal(SIGTERM, sig_child);
	printf("Child: Pid=%d, PGrp=%d, PPid=%d\n", getpid(), getpgrp(), getppid());
	fflush(stdout);
	sleep(10);

	_exit(0);
}

/*
 * sys_sigsuspend in C
 *
 * (C) 2020.03.11 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <getopt.h>
/* __NR_sigaction/__NR_sigsuspend */
#include <asm/unistd.h>
/* syscall() */
#include <unistd.h>
/* signal */
#include <signal.h>

/* Architecture defined */
#ifndef __NR_sigaction
#define __NR_sigaction	67
#endif
#ifndef __NR_sigsuspend
#define __NR_sigsuspend	72
#endif
#ifndef SIGLOST
#define SIGLOST		29
#endif
#ifndef SIGUNUSED
#define SIGUNUSED	31
#endif

static void usage(const char *program_name)
{
	printf("BiscuitOS: sys_sigsuspend helper\n");
	printf("Usage:\n");
	printf("      %s <-s signal>\n", program_name);
	printf("\n");
	printf("\t-s\t--signal\tThe signal for suspend.\n");
	printf("\t\t\tSIGHUP       1\n");
	printf("\t\t\tSIGINT       2\n");
	printf("\t\t\tSIGQUIT      3\n");
	printf("\t\t\tSIGILL       4\n");
	printf("\t\t\tSIGTRAP      5\n");
	printf("\t\t\tSIGABRT      6\n");
	printf("\t\t\tSIGIOT       6\n");
	printf("\t\t\tSIGBUS       7\n");
	printf("\t\t\tSIGFPE       8\n");
	printf("\t\t\tSIGKILL      9\n");
	printf("\t\t\tSIGUSR1     10\n");
	printf("\t\t\tSIGSEGV     11\n");
	printf("\t\t\tSIGUSR2     12\n");
	printf("\t\t\tSIGPIPE     13\n");
	printf("\t\t\tSIGALRM     14\n");
	printf("\t\t\tSIGTERM     15\n");
	printf("\t\t\tSIGSTKFLT   16\n");
	printf("\t\t\tSIGCHLD     17\n");
	printf("\t\t\tSIGCONT     18\n");
	printf("\t\t\tSIGSTOP     19\n");
	printf("\t\t\tSIGTSTP     20\n");
	printf("\t\t\tSIGTTIN     21\n");
	printf("\t\t\tSIGTTOU     22\n");
	printf("\t\t\tSIGURG      23\n");
	printf("\t\t\tSIGXCPU     24\n");
	printf("\t\t\tSIGXFSZ     25\n");
	printf("\t\t\tSIGVTALRM   26\n");
	printf("\t\t\tSIGPROF     27\n");
	printf("\t\t\tSIGWINCH    28\n");
	printf("\t\t\tSIGIO       29\n");
	printf("\t\t\tSIGPOLL     29\n");
	printf("\t\t\tSIGLOST     29\n");
	printf("\t\t\tSIGPWR      30\n");
	printf("\t\t\tSIGSYS      31\n");
	printf("\t\t\tSIGUNUSED   31\n");
	printf("\t\t\tSIGRTMIN    32\n");
	printf("\ne.g:\n");
	printf("%s -s SIGKILL\n\n", program_name);
}

static void sig_usr(int signum)
{
	printf("Sig-handler: %d\n", signum);
}

int main(int argc, char *argv[])
{
	char *sig = NULL;
	int c, hflags = 0;
	int signo;
	struct sigaction sa_usr, sa_save;
	sigset_t fill_mask;
	opterr = 0;

	/* options */
	const char *short_opts = "hs:";
	const struct option long_opts[] = {
		{ "help", no_argument, NULL, 'h'},
		{ "signal", required_argument, NULL, 's'},
		{ 0, 0, 0, 0 }
	};

	while ((c = getopt_long(argc, argv, short_opts, 
						long_opts, NULL)) != -1) {
		switch (c) {
		case 'h':
			hflags = 1;
			break;
		case 's': /* signal */
			sig = optarg;
			break;
		default:
			abort();
		}
	}

	if (hflags || !sig) {
		usage(argv[0]);
		return 0;
	}

	/* parse signal argument */
	if (strstr(sig, "SIGHUP"))
		signo = SIGHUP;
	else if (strstr(sig, "SIGINT"))
		signo = SIGINT;
	else if (strstr(sig, "SIGQUIT"))
		signo = SIGQUIT;
	else if (strstr(sig, "SIGILL"))
		signo = SIGILL;
	else if (strstr(sig, "SIGTRAP"))
		signo = SIGTRAP;
	else if (strstr(sig, "SIGABRT"))
		signo = SIGABRT;
	else if (strstr(sig, "SIGIOT"))
		signo = SIGIOT;
	else if (strstr(sig, "SIGBUS"))
		signo = SIGBUS;
	else if (strstr(sig, "SIGFPE"))
		signo = SIGFPE;
	else if (strstr(sig, "SIGKILL"))
		signo = SIGKILL;
	else if (strstr(sig, "SIGUSR1"))
		signo = SIGUSR1;
	else if (strstr(sig, "SIGSEGV"))
		signo = SIGSEGV;
	else if (strstr(sig, "SIGUSR2"))
		signo = SIGUSR2;
	else if (strstr(sig, "SIGPIPE"))
		signo = SIGPIPE;
	else if (strstr(sig, "SIGALRM"))
		signo = SIGALRM;
	else if (strstr(sig, "SIGTERM"))
		signo = SIGTERM;
	else if (strstr(sig, "SIGSTKFLT"))
		signo = SIGSTKFLT;
	else if (strstr(sig, "SIGCHLD"))
		signo = SIGCHLD;
	else if (strstr(sig, "SIGCONT"))
		signo = SIGCONT;
	else if (strstr(sig, "SIGSTOP"))
		signo = SIGSTOP;
	else if (strstr(sig, "SIGTSTP"))
		signo = SIGTSTP;
	else if (strstr(sig, "SIGTTIN"))
		signo = SIGTTIN;
	else if (strstr(sig, "SIGTTOU"))
		signo = SIGTTOU;
	else if (strstr(sig, "SIGURG"))
		signo = SIGURG;
	else if (strstr(sig, "SIGXCPU"))
		signo = SIGXCPU;
	else if (strstr(sig, "SIGXFSZ"))
		signo = SIGXFSZ;
	else if (strstr(sig, "SIGVTALRM"))
		signo = SIGVTALRM;
	else if (strstr(sig, "SIGPROF"))
		signo = SIGPROF;
	else if (strstr(sig, "SIGWINCH"))
		signo = SIGWINCH;
	else if (strstr(sig, "SIGIO"))
		signo = SIGIO;
	else if (strstr(sig, "SIGPOLL"))
		signo = SIGPOLL;
	else if (strstr(sig, "SIGLOST"))
		signo = SIGLOST;
	else if (strstr(sig, "SIGPWR"))
		signo = SIGPWR;
	else if (strstr(sig, "SIGSYS"))
		signo = SIGSYS;
	else if (strstr(sig, "SIGUNUSED"))
		signo = SIGUNUSED;
	else if (strstr(sig, "SIGRTMIN"))
		signo = SIGRTMIN;

	/* setup sig handler */
	sa_usr.sa_handler = sig_usr;
	sigemptyset(&sa_usr.sa_mask);
	sa_usr.sa_flags = 0;

	/*
	 * sys_sigaction
	 *
	 *    SYSCALL_DEFINE3(sigaction,
	 *                    int, sig,
	 *                    const struct old_sigaction __user *, act,
	 *                    struct old_sigaction __user *, oact)
	 */
	syscall(__NR_sigaction, signo, &sa_usr, &sa_save);

	/* initialize signal set */
	sigfillset(&fill_mask);

	/* remove special signal from signal set */
	sigdelset(&fill_mask, signo);

	/* wait suspend signal expect signal from kill_mask */
	/*
	 * sys_sigsuspend
	 *
	 *    #ifdef CONFIG_OLD_SIGSUSPEND 
	 *    SYSCALL_DEFINE1(sigsuspend,
	 *                    old_sigset_t, mask)
	 *    #endif
	 *
	 *    #ifdef CONFIG_OLD_SIGSUSPEND3
	 *    SYSCALL_DEFINE3(sigsuspend,
	 *                    int, unused1,
	 *                    int, unused2,
	 *                    old_sigset_t, mask)
	 */
	syscall(__NR_sigsuspend, &fill_mask);

	/* recove signal */
	/*
	 * sys_sigaction
	 *
	 *    SYSCALL_DEFINE3(sigaction,
	 *                    int, sig,
	 *                    const struct old_sigaction __user *, act,
	 *                    struct old_sigaction __user *, oact)
	 */
	syscall(__NR_sigaction, signo, &sa_save, (struct sigaction *)0);

	return 0;
}

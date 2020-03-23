/*
 * BiscuitOS Common system call: Four Paramenter
 *
 * (C) 2020.03.20 BuddyZhang1 <buddy.zhang@aliyun.com>
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
/* __NR_* */
#include <asm/unistd.h>
/* syscall() */
#include <unistd.h>

/* Architecture defined */
#ifndef __NR_hello_BiscuitOS
#define __NR_hello_BiscuitOS	400
#endif

int main(void)
{
	char buffer[128] = "Userspace_BiscuitOS";
	int nr_write = strlen(buffer) + 1;
	int nr_read = 6;
	int reader;
	int ret;

	/*
	 * sys_hello_BiscuitOS: Four paramenter
	 * kernel:
	 *       SYSCALL_DEFINE4(hello_BiscuitOS,
	 *                       char __user *, strings,
	 *                       int, nr_write,
	 *                       int, nr_read,
	 *                       int __user *, reader)
	 */
	ret = syscall(__NR_hello_BiscuitOS, buffer, 
						nr_write, nr_read, &reader);
	if (ret < 0) {
		printf("SYSCALL failed %d\n", ret);
		return ret;
	} else {
		buffer[nr_read] = '\0';
		printf("BiscuitOS[%d]: %s\n", reader, buffer);
	}

	return 0;
}

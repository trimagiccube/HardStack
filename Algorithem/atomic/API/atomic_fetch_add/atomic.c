/*
 * atomic
 *
 * (C) 2019.05.05 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Memory access
 *
 *
 *      +----------+
 *      |          |
 *      | Register |                                         +--------+
 *      |          |                                         |        |
 *      +----------+                                         |        |
 *            A                                              |        |
 *            |                                              |        |
 * +-----+    |      +----------+        +----------+        |        |
 * |     |<---o      |          |        |          |        |        |
 * | CPU |<--------->| L1 Cache |<------>| L2 Cache |<------>| Memory |
 * |     |<---o      |          |        |          |        |        |
 * +-----+    |      +----------+        +----------+        |        |
 *            |                                              |        |
 *            o--------------------------------------------->|        |
 *                         volatile/atomic                   |        |
 *                                                           |        |
 *                                                           +--------+
 */

/*
 * atomic_add_* (ARMv7 Cotex-A9MP)
 *
 * static inline int atomic_fetch_add(int i, atomic_t *v)
 * {
 *         unsigned long tmp;
 *         int result, val;
 *
 *         prefetchw(&v->counter);
 *         __asm__ volatile ("\n\t"
 *         "@ atomic_add\n\t"
 * "1:      ldrex   %0, [%4]\n\t"        @ result, tmp115
 * "        add     %1, %0, %5\n\t"      @ result,
 * "        strex   %2, %1, [%4]\n\t"    @ tmp, result, tmp115
 * "        teq     %2, #0\n\t"          @ tmp
 * "        bne     1b"
 *          : "=&r" (result), "=&r" (val), "=&r" (tmp), "+Qo" (v->counter)
 *          : "r" (&v->counter), "Ir" (i)
 *          : "cc");
 *
 *         return result;
 * }
 */

#include <linux/kernel.h>
#include <linux/init.h>

static atomic_t BiscuitOS_counter = ATOMIC_INIT(8);

/* atomic_* */
static __init int atomic_demo_init(void)
{
	int val;

	/* Atomic add */
	val = atomic_fetch_add(1, &BiscuitOS_counter);

	printk("Atomic: %d\n", val);

	return 0;
}
device_initcall(atomic_demo_init);

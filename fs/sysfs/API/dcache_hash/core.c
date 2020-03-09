/*
 * Dentry hash mechanism
 *
 * (C) 2020.03.07 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/fs_struct.h>
#include <linux/vmalloc.h>

#include <asm/pgtable.h>

/*
 * Little-endian word-at-a-time zero byte handling.
 */

struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

static inline unsigned long has_zero(unsigned long a, unsigned long *bits,
			const struct word_at_a_time *c)
{
	unsigned long mask = ((a - c->one_bits) & ~a) & c->high_bits;

	*bits = mask;
	return mask;
}

#define prep_zero_mask(a, bits, c)	(bits)
#define zero_bytemask(mask)		(mask)

#define HASH_ZERO	0x00000004	/* Zero allocated hash table */

static inline unsigned long create_zero_mask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

/*
 * Mixing scores (int bits) for (7,20):
 * Input delta: 1-bit      2-bit
 * 1 round:     330.3     9201.6
 * 2 rounds:   1246.4    25475.4
 * 3 rounds:   1807.1    31295.1
 * 4 rounds:   2042.3    31718.6
 * Perfect:    2048      31744
 *            (32*64)   (32*31/2 * 64)
 */
#define HASH_MIX(x, y, a)				\
(	x ^= (a),					\
	y ^= x, x = rol32(x, 7),			\
	x += y, y = rol32(y, 20),			\
	y *= 9						\
)

/* dentry hashtable */
static struct hlist_bl_head *dentry_hashtable __read_mostly;
static unsigned long dhash_entries = 32;
static unsigned int d_hash_shift __read_mostly;

static inline unsigned int fold_hash(unsigned long x, unsigned long y)
{
	/* Use arch-optimized multiply if one exists */
	return __hash_32(y ^ __hash_32(x));
}

static inline unsigned long find_zero(unsigned long mask)
{
	unsigned long ret;

	/* We have clz available. */
	ret = fls(mask) >> 3;

	return ret;
}

static const char *name_array[] = { "BiscuitOS_fs", "BiscuitOS_mm",
				    "BiscuitOS_proc", "BiscuitOS_ramfs"
				    "BiscuitOS_tmpfs", "BiscuitOS_etc"
};

/*
 * Load an unaligned word from kernel space.
 *
 * In the (very unlikely) case of the word being a page-crosser
 * and the next page not being mapped, task the exception and
 * return zeros in the non-existing part.
 */
static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long ret, offset;

	/* Load word from unaligned pointer addr */
	asm(
	"1:	ldr	%0, [%2]\n"
	"2:\n"
	"	.pushsection .text.fixup,\"ax\"\n"
	"	.align 2\n"
	"3:	and	%1, %2, #0x3\n"
	"	bic	%2, %2, #0x3\n"
	"	ldr	%0, [%2]\n"
	"	lsl	%1, %1, #0x3\n"
	"	lsr	%0, %0, %1\n"
	"	b	2b\n"
	"	.popsection\n"
	"	.pushsection __ex_table,\"a\"\n"
	"	.align	3\n"
	"	.long	1b, 3b\n"
	"	.popsection"
	: "=&r" (ret), "=&r" (offset)
	: "r" (addr), "Qo" (*(unsigned long *)addr));

	return ret;
}

/*
 * Calculate the length and hash of the path component, and
 * return the "hash_len" as the result.
 */
static inline u64 hash_name(const void *salt, const char *name)
{
	unsigned long a = 0, b, x = 0, y = (unsigned long)salt;
	unsigned long adata, bdata, mask, len;
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;

	len = 0;
	goto inside;

	do {
		HASH_MIX(x, y, a);
		len += sizeof(unsigned long);
inside:
		a = load_unaligned_zeropad(name+len);
		b = a ^ REPEAT_BYTE('/');
	} while (!(has_zero(a, &adata, &constants) | 
					has_zero(b, &bdata, &constants)));

	adata = prep_zero_mask(a, adata, &constants);
	bdata = prep_zero_mask(b, bdata, &constants);
	mask = create_zero_mask(adata | bdata);
	x ^= a & zero_bytemask(mask);

	return hashlen_create(fold_hash(x, y), len + find_zero(mask));
}

static inline struct hlist_bl_head *d_hash(unsigned int hash)
{
	return dentry_hashtable + (hash >> d_hash_shift);
}

/*
 * allocate a large system hash table from vmalloc
 * - it is assumed that the hash table must contain an exact power-of-2
 *   quantity of entries
 * - limit is the number of hash buckets, not the total allocation size.
 */
static void *alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries,
				     int scale,
				     int flags,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit)
{
	unsigned long long max = high_limit;
	unsigned long log2qty, size;
	void *table = NULL;
	gfp_t gfp_flags;

	numentries = roundup_pow_of_two(numentries);
	max = min(max, 0x80000000ULL);
	if (numentries < low_limit)
		numentries = low_limit;
	if (numentries > max)
		numentries = max;

	log2qty = ilog2(numentries);

	gfp_flags = (flags & HASH_ZERO) ? GFP_ATOMIC | __GFP_ZERO : GFP_ATOMIC;
	do {
		size = bucketsize << log2qty;
		table = __vmalloc(size, gfp_flags, PAGE_KERNEL);
	} while (!table && size > PAGE_SIZE && --log2qty);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);
	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}

static void dcache_init(void)
{
	dentry_hashtable = 
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					HASH_ZERO,
					&d_hash_shift,
					NULL,
					0,
					PAGE_SIZE * 16);
}

/* Module initialize entry */
static int __init Demo_init(void)
{
	/* Define 6 dentry */
	struct dentry *dentry_array[6];
	struct fs_struct *fs = current->fs;
	struct path *path = &fs->pwd;
	int index;

	/* init dcache init */
	dcache_init();

	for (index = 0; index < 6; index++) {
		struct qstr name;
		struct dentry *dentry = dentry_array[index];
		struct hlist_bl_head *b;

		/* Calculate hast len */
		name.hash_len = hash_name(path->dentry, name_array[index]);
		name.name = name_array[index];

		/* create a dentry */
		dentry = kzalloc(sizeof(struct dentry), GFP_KERNEL);
		if (!dentry) {
			printk("dentry[%d]kzalloc() not free memory\n", index);
			goto out_mem;
		}
		/* setup dentry name */
		dentry->d_name.len = name.len;
		dentry->d_name.hash = name.hash;
		memcpy(dentry->d_iname, name_array[index], name.len);
		dentry->d_iname[name.len] = 0;

		/* Init hash table */
		INIT_HLIST_BL_NODE(&dentry->d_hash);
		INIT_HLIST_NODE(&dentry->d_u.d_alias);

		/* d_add */
		b = d_hash(dentry->d_name.hash);
		hlist_bl_lock(b);
		hlist_bl_add_head_rcu(&dentry->d_hash, b);
		hlist_bl_unlock(b);
	}

out_mem:
	;
	return 0;
}

/* Module exit entry */
static void __exit Demo_exit(void)
{
}

module_init(Demo_init);
module_exit(Demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BiscuitOS <buddy.zhang@aliyun.com>");
MODULE_DESCRIPTION("Dentry hash mechanism");

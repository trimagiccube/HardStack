/*
 * Buddy Allocator
 *
 * (C) 2020.02.02 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>

#include "linux/buddy.h"

/* nr_pages for memory */
unsigned long nr_pages;
/* Simulate memory */
unsigned char *memory;
/* mem_map array */
struct page *mem_map;
/* Huge page sizes are variable */
unsigned int pageblock_order = 10;
/* Emulate Zone */
struct zone BiscuitOS_zone;

/*
 * Locate the struct page for both the matching buddy in our
 * pair (buddy1) and the combined O(n+1) page they form (page).
 *
 * 1) Any buddy B1 will have an order O twin B2 which satisfies
 *    the following equation:
 *
 *    For example, if the starting buddy (buddy2) is #8 its order
 *    1 buddy is #10:
 *
 *      B2 = 0 ^ (1 << 1) = 0 ^ 2 = 0000B ^ 0010B = 0010B = 2
 *      B2 = 2 ^ (1 << 1) = 2 ^ 2 = 0010B ^ 0010B = 0000B = 0
 *      B2 = 4 ^ (1 << 1) = 4 ^ 2 = 0100B ^ 0010B = 0110B = 6
 *      B2 = 6 ^ (1 << 1) = 6 ^ 2 = 0110B ^ 0010B = 0100B = 4
 *      B2 = 8 ^ (1 << 1) = 8 ^ 2 = 1000B ^ 0010B = 1010B = A
 *      B2 = A ^ (1 << 1) = A ^ 2 = 1010B ^ 0010B = 1000B = 8
 *      B2 = C ^ (1 << 1) = C ^ 2 = 1100B ^ 0010B = 1110B = E
 *      B2 = E ^ (1 << 1) = E ^ 2 = 1110B ^ 0010B = 1100B = C
 *
 *    For Figure, order 1
 *
 *    0      2      4      6      8      A      C      E      10
 *    +------+------+------+------+------+------+------+------+
 *    |      |      |      |      |      |      |      |      |
 *    |  B0  |  B1  |  B2  |  B3  |  B4  |  B5  |  B6  |  B7  |  
 *    |      |      |      |      |      |      |      |      |
 *    +------+------+------+------+------+------+------+------+
 *    | <- Pairs -> | <- Paris -> | <- Paris -> | <- Paris -> |
 *
 * 2) And buddy B will have and O+1 parent P which satisfies
 *    the following equation:
 *
 *      P = B & ~(1 << O)
 *
 *    For example, if the starting buddy is #8 it order 1:
 *
 *      P = 0 & ~(1 << 1) = 0000B & 1101B = 0000B
 *      P = 2 & ~(1 << 1) = 0010B & 1101B = 0000B
 *      P = 4 & ~(1 << 1) = 0100B & 1101B = 0100B
 *      P = 6 & ~(1 << 1) = 0110B & 1101B = 0100B
 *      P = 8 & ~(1 << 1) = 1000B & 1101B = 1000B
 *      P = A & ~(1 << 1) = 1010B & 1101B = 1000B
 *      P = C & ~(1 << 1) = 1100B & 1101B = 1100B
 *      P = E & ~(1 << 1) = 1110B & 1101B = 1100B
 *
 *    For Figure, order 1
 *
 *    0      2      4      6      8      A      C      E      10
 *    +------+------+------+------+------+------+------+------+
 *    |      |      |      |      |      |      |      |      |
 *    |  B0  |  B1  |  B2  |  B3  |  B4  |  B5  |  B6  |  B7  |  
 *    |      |      |      |      |      |      |      |      |
 *    +------+------+------+------+------+------+------+------+
 *    | <- Pairs -> | <- Paris -> | <- Paris -> | <- Paris -> |
 *    P0            P1            P2            P3
 *
 * Assumption: *_mem_map is contiguous at least up to MAX_ORDER.
 */
static inline unsigned long
__find_buddy_pfn(unsigned long page_pfn, unsigned int order)
{
	return page_pfn ^ (1 << order);
}

/*
 * This function checks whether a page is free && is the buddy
 * we can coalesce a page and its buddy if
 * (a) the buddy is not in a hole (check before calling!) &&
 * (b) the buddy is in the buddy system &&
 * (c) a page and its buddy have the same order &&
 * (d) a page and its buddy are in the same zone.
 *
 * For recording whether a page is in the system, we set PageBuddy.
 * Setting, clearing, and testing PageBuddy is serialized by 
 * zone->lock.
 *
 * For recording page's order, we use page_private(page);
 */
static inline int page_is_buddy(struct page *page, struct page *buddy,
					unsigned int order)
{
	if (PageBuddy(buddy) && page_order(buddy) == order)
		return 1;
}

/*
 * Freeing functing for a buddy system allocator.
 *
 * The concept of a buddy system is to maintain direct-mapped table
 * (containing bit values) for memory blocks of various "orders".
 * The bottom level table contains the map for the smallest allocatable
 * units of memory (here, pages), and each level above it describes
 * pairs of units from the levels below, hence, "buddies".
 * At a high level, all that happens here is marking the table entry
 * at the bottom level available, and propagating the changes upward
 * as necessary, plus some accounting needed to play nicely with other
 * parts of the VM system.
 * At each level, we keep a list of pages, which are heads of continuous
 * free pages of length of (1 << order) and marked with PageByddy.
 * Page's order is recorded or freeing one, we can derive the state of
 * the other. That is, if we allocate a small block, and both were
 * free, the remainder of the region must be split into blocks. If
 * a block is freed, and its buddy is also free, then this triggers
 * coalescing into a block of larger size.
 *
 * -- nyc
 */

static inline void __free_one_page(struct zone *zone, struct page *page, 
			unsigned long pfn, unsigned int order)
{
	unsigned int max_order;
	unsigned long buddy_pfn;
	unsigned long combined_pfn;
	struct page *buddy;

	max_order = min_t(unsigned int, MAX_ORDER, pageblock_order + 1);

continue_merging:
	while (order < max_order - 1) {
		buddy_pfn = __find_buddy_pfn(pfn, order);	
		buddy = page + (buddy_pfn - pfn);

		if (!pfn_valid_within(buddy_pfn))
			goto done_merging;
		if (!page_is_buddy(page, buddy, order))
			goto done_merging;

		/*
		 * Our buddy is free and meger with it and move up
		 * one order.
		 */
		list_del(&buddy->lru);
		zone->free_area[order].nr_free--;
		rmv_page_order(buddy);
		combined_pfn = buddy_pfn & pfn;
		page = page + (combined_pfn - pfn);
		pfn = combined_pfn;
		order++;
	}

done_merging:
	set_page_order(page, order);

	/* We have no PCP, so only goto pcp_emulate */
	if (order == 0)
		goto pcp_emulate;

	/*
	 * If this is not the largest possible page, check if the buddy
	 * of the next-highest order is free. If it is, it's possible
	 * that pages are being free that will coalesce soon. In case,
	 * that is happening, add the free page to the tail of the list
	 * so it's less likely to be used soon and more likely to be meged
	 * as a higher order page.
	 */
	if ((order < MAX_ORDER-2) && pfn_valid_within(buddy_pfn)) {
		struct page *higher_page, *higher_buddy;

		combined_pfn = buddy_pfn & pfn;
		higher_page = page + (combined_pfn - pfn);
		buddy_pfn = __find_buddy_pfn(combined_pfn, order + 1);
		higher_buddy = higher_page + (buddy_pfn - combined_pfn);
		if (pfn_valid_within(buddy_pfn) &&
		    page_is_buddy(higher_page, higher_buddy, order + 1)) {
			list_add_tail(&page->lru,
				&zone->free_area[order].free_list[0]);
			goto out;
		}
		goto pcp_emulate;
	}

pcp_emulate:
	list_add(&page->lru, &zone->free_area[order].free_list[0]);
out:
	zone->free_area[order].nr_free++;
}

static void __free_pages_ok(struct page *page, unsigned int order)
{
	unsigned long pfn = page_to_pfn(page);

	__free_one_page(page_zone(page), page, pfn, order);
}

static void free_pcppages_bulk(struct zone *zone, int count, 
					struct per_cpu_pages *pcp)
{
	int batch_free = 0;
	int prefetch_nr = 0;
	struct page *page, *tmp;
	LIST_HEAD(head);
	struct list_head *list;

	/*
	 * Remove pages from lists in a round-robin fashion. A
	 * batch_free count is maintained that is incremented when an
	 * empty list is encountered. This is so more pages area freed
	 * off fuller lists instead of spinning execessively around
	 * empty lists.
	 */
	list = &pcp->lists[0];

	do {
		page = list_last_entry(list, struct page, lru);
		list_del(&page->lru);
		pcp->count--;

		list_add_tail(&page->lru, &head);
	} while (--count && !list_empty(list));

	/*
	 * Use safe version since after __free_one_page(),
	 * page->lru.next will not point to original list.
	 */
	list_for_each_entry_safe(page, tmp, &head, lru)
		__free_one_page(zone, page, page_to_pfn(page), 0);

}

static void free_unref_page_commit(struct page *page, unsigned long pfn)
{
	struct zone *zone = page_zone(page);
	struct per_cpu_pages *pcp;

	pcp = zone->pcp;
	list_add(&page->lru, &pcp->lists[0]);
	pcp->count++;
	if (pcp->count >= pcp->high) {
		unsigned long batch = pcp->batch;
		free_pcppages_bulk(zone, batch, pcp);
	}
}

/*
 * Free a 0-order page
 */
void free_unref_page(struct page *page)
{
	unsigned long flags;
	unsigned long pfn = page_to_pfn(page);

	free_unref_page_commit(page, pfn);
}

static inline void free_the_page(struct page *page, unsigned int order)
{
	if (order == 0)		/* Via pcp? */
		free_unref_page(page);
	else
		__free_pages_ok(page, order);
}

void __free_pages(struct page *page, unsigned int order)
{
	free_the_page(page, order);
}

/*
 * The order of subdivision here is critical for the IO subsystem.
 * Please do not alter this order without good reasons and regression
 * testing. Specifically, as large blocks of memory are subdivided,
 * the order in which smaller blocks are delivered depends on the
 * order they're subdivided in this function. This is the primary
 * factor influencing the order in which pages are delivered to the
 * IO subsystem according to empirical testing, and this is also
 * justified by considering the behavior of a buddy system containing
 * a single large block of memory acted on by a series of small
 * allocations. This behavior is a critical factor in sglist merging's
 * success.
 */
static inline void expand(struct zone *zone, struct page *page,
			int low, int high, struct free_area *area)
{
	unsigned long size = 1 << high;

	while (high > low) {
		area--;
		high--;
		size >>= 1;

		list_add(&page[size].lru, &area->free_list[0]);
		area->nr_free++;
		set_page_order(&page[size], high);
	}
}

/*
 * Go through the free lists for the given migratetype and remove
 * the smallest available page from the freelist.
 */
static inline
struct page *__rmqueue_smallest(struct zone *zone, unsigned int order)
{
	unsigned int current_order;
	struct free_area *area;
	struct page *page;

	/* Find a page for appropriate size in the preferred list */
	for (current_order = order; current_order < MAX_ORDER; 
							++current_order) {
		area = &(zone->free_area[current_order]);
		page = list_first_entry_or_null(&area->free_list[0],
							struct page, lru);
		if (!page)
			continue;
		list_del(&page->lru);
		rmv_page_order(page);
		area->nr_free--;
		expand(zone, page, order, current_order, area);
		return page;
	}
	return NULL;
}

/*
 * Obtain a specified number of elements from the buddy allocator, all under
 * a single hold of the lock, for efficiency. Add them to the supplied list.
 * Returns the number of new pages which were placed at *list.
 */
static int rmqueue_bulk(struct zone *zone, unsigned int order,
			unsigned long count, struct list_head *list,
			unsigned int alloc_flags)
{
	int i, alloced = 0;

	for (i = 0; i < count; ++i) {
		struct page *page = __rmqueue_smallest(zone, 0);

		if (unlikely(page == NULL))
			break;

		/*
		 * Split buddy pages returned by expand() are received here in
		 * physical page order. The page is added to the tail of
		 * caller's list. From the callers perspective, the linked list
		 * is ordered by page number under some conditions. This is
		 * useful for IO devices that can forward direction from the
		 * head, thus also in the physical page order. This is useful
		 * for IO devices that can merge IO requests if the physical
		 * pages are ordered properly.
		 */
		list_add_tail(&page->lru, list);
		alloced++;
	}
	return alloced;
}

/* Remove page from the per-cpu list, caller must protect the list */
static struct page *__rmqueue_pcplist(struct zone *zone, 
			unsigned int alloc_flags, struct per_cpu_pages *pcp,
			struct list_head *list)
{
	struct page *page;

	do {
		if (list_empty(list)) {
			pcp->count += rmqueue_bulk(zone, 0,
					pcp->batch, list, alloc_flags);
			if (unlikely(list_empty(list)))
				return NULL;
		}
		page = list_first_entry(list, struct page, lru);
		list_del(&page->lru);
		pcp->count--;
	} while (0);

	return page;
}

static struct page *rmqueue_pcplist(struct zone *zone, unsigned int order,
						gfp_t gfp_flags)
{
	struct per_cpu_pages *pcp;
	struct list_head *list;
	struct page *page;
	unsigned long flags;

	pcp = zone->pcp;
	list = &pcp->lists[0];
	page = __rmqueue_pcplist(zone, 0, pcp, list);
	return page;
}

/*
 * Allocate a page from the given zone. Use pcplists for order-0
 * allocations.
 */
static inline
struct page *rmqueue(struct zone *zone, unsigned int order,
						gfp_t gfp_flags)
{
	struct page *page;

	if (likely(order == 0)) {
		page = rmqueue_pcplist(zone, order, gfp_flags);
		return page;
	}

	/* We most definitely don't want callers attempting to
	 * allocate greater than order-1 page units with __GFP_NOFAIL.
	 */
	page = __rmqueue_smallest(zone, order);
	return page;
}

/*
 * get_page_from_freelist goes through the zonelist trying to
 * allocate a page.
 */
static struct page *
get_page_from_freelist(gfp_t gfp_mask, unsigned int order)
{
	struct zone *zone = &BiscuitOS_zone;
	struct page *page;

	page = rmqueue(zone, order, gfp_mask);
	return page;
}

/*
 * This is the 'heart' of the zoned buddy allocator
 */
struct page *__alloc_pages(gfp_t gfp_mask, unsigned int order)
{
	struct page *page;

	/* First allocation attempt */
	page = get_page_from_freelist(gfp_mask, order);
	return page;	
}

/*
 * page_address - get the mapped virtual address of a page
 */
void *page_address(const struct page *page)
{
	return lowmem_page_address(page);
}

static void pageset_update(struct per_cpu_pages *pcp, unsigned long high,
				unsigned long batch)
{
	/* Update high, then batch, in order */
	pcp->high = high;
	pcp->batch = batch;
}

static void pageset_init(void)
{
	struct zone *zone = &BiscuitOS_zone;
	struct per_cpu_pages *pcp;
	unsigned long batch = BATCH_SIZE;

	zone->pcp = (struct per_cpu_pages *)malloc(
				sizeof(struct per_cpu_pages));
	memset(zone->pcp, 0, sizeof(struct per_cpu_pages));
	pcp = zone->pcp;

	INIT_LIST_HEAD(&pcp->lists[0]);

	pageset_update(pcp, 6 * batch, max(1UL, batch));
}

/*
 * PHYS_OFFSET                                         
 * | <--------------------- MEMORY_SIZE ----------------------> |
 * +---------------+--------------------------------------------+
 * |               |                                            |
 * |               |                                            |
 * |               |                                            |
 * +---------------+--------------------------------------------+
 * | <- mem_map -> |
 *
 */
int memory_init(void)
{
	unsigned long start_pfn, end_pfn;
	struct zone *zone = &BiscuitOS_zone;
	int order, index;

	/* Emulate Memory Region */
	memory = (unsigned char *)malloc(MEMORY_SIZE);

	/* Establish mem_map[] */
	mem_map = (struct page *)(unsigned long)memory;

	/* Initialize all pages */
	nr_pages = MEMORY_SIZE / PAGE_SIZE;
	zone->managed_pages = nr_pages;
	for (index = 0; index < nr_pages; index++) {
		struct page *page = &mem_map[index];

		INIT_LIST_HEAD(&page->lru);
		page->page_type |= PAGE_TYPE_BASE;
	}

	/* Initialize Zone */
	for (order = 0; order < MAX_ORDER; order++) {
		INIT_LIST_HEAD(&zone->free_area[order].free_list[0]);
		zone->free_area[order].nr_free = 0;
	}

	/* free all page into Buddy Allocator */
	start_pfn = PFN_UP(PHYS_OFFSET);
	end_pfn = PFN_DOWN(PHYS_OFFSET + MEMORY_SIZE);

	while (start_pfn < end_pfn) {
		int order = min_t(unsigned int,
					MAX_ORDER - 1UL, __ffs(start_pfn));

		while (start_pfn + (1UL << order) > end_pfn)
			order--;

		/* Free page into Buddy Allocator */
		__free_pages(pfn_to_page(start_pfn), order);

		start_pfn += (1UL << order);
	}
	/* PCP init */
	pageset_init();

	printk("BiscuitOS PCP Memory Allocator.\n");
	printk("Physical Memory: %#lx - %#lx\n", (unsigned long)PHYS_OFFSET, 
					(unsigned long)(PHYS_OFFSET + MEMORY_SIZE));
	printk("mem_map[] contains %#lx pages, page size %#lx\n", nr_pages,
						(unsigned long)PAGE_SIZE);

	return 0;
}

void memory_exit(void)
{
	free(memory);
}
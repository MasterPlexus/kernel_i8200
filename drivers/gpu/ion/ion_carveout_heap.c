/*
 * drivers/gpu/ion/ion_carveout_heap.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/spinlock.h>

#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/ion.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/dma-contiguous.h>
#include "ion_priv.h"

#include <asm/mach/map.h>

#define ION_FLAG_CMA	(1 << 16)

struct ion_carveout_heap {
	struct ion_heap heap;
	struct gen_pool *pool;
	ion_phys_addr_t base;
};

ion_phys_addr_t ion_carveout_allocate(struct ion_heap *heap,
				      unsigned long size,
				      unsigned long align)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);
	unsigned long offset = gen_pool_alloc(carveout_heap->pool, size);

	if (!offset)
		return ION_CARVEOUT_ALLOCATE_FAIL;

	return offset;
}

void ion_carveout_free(struct ion_heap *heap, ion_phys_addr_t addr,
		       unsigned long size)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);
	if (addr == ION_CARVEOUT_ALLOCATE_FAIL)
		return;
	gen_pool_free(carveout_heap->pool, addr, size);
}

static int ion_carveout_heap_phys(struct ion_heap *heap,
				  struct ion_buffer *buffer,
				  ion_phys_addr_t *addr, size_t *len)
{
	*addr = buffer->priv_phys;
	*len = buffer->size;
	return 0;
}

static int ion_carveout_heap_allocate(struct ion_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long size, unsigned long align,
				      unsigned long flags)
{
	struct page *page;
	unsigned int alignfix = get_order(size);

	buffer->priv_phys = ion_carveout_allocate(heap, size, align);
	if (buffer->priv_phys != ION_CARVEOUT_ALLOCATE_FAIL) {
		buffer->flags &= ~ION_FLAG_CMA;
		return 0;
	}

	if (alignfix < get_order(align))
		alignfix = get_order(align);

	page = dma_alloc_from_contiguous(NULL, size >> PAGE_SHIFT, alignfix);

	if (!page) {
		buffer->priv_phys = ION_CARVEOUT_ALLOCATE_FAIL;
		return -ENOMEM;
	}

	buffer->flags |= ION_FLAG_CMA;
	buffer->priv_phys = page_to_phys(page);
	return 0;
}

static void ion_carveout_heap_free(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;

	if (buffer->flags & ION_FLAG_CMA)
		dma_release_from_contiguous(NULL,
			phys_to_page(buffer->priv_phys),
			buffer->size >> PAGE_SHIFT);
	else
		ion_carveout_free(heap, buffer->priv_phys, buffer->size);

	buffer->priv_phys = ION_CARVEOUT_ALLOCATE_FAIL;
}

struct sg_table *ion_carveout_heap_map_dma(struct ion_heap *heap,
					      struct ion_buffer *buffer)
{
	struct sg_table *table;
	struct scatterlist *sg;
	int ret, i;
	int nents = PAGE_ALIGN(buffer->size) / PAGE_SIZE;
	struct page *page = phys_to_page(buffer->priv_phys);

	table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(table, nents, GFP_KERNEL);
	if (ret) {
		kfree(table);
		return ERR_PTR(ret);
	}

	sg = table->sgl;
	for (i = 0; i < nents; i++) {
		sg_set_page(sg, page + i, PAGE_SIZE, 0);
		sg = sg_next(sg);
	}

	return table;
}

void ion_carveout_heap_unmap_dma(struct ion_heap *heap,
				 struct ion_buffer *buffer)
{
	if (buffer->sg_table)
		sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
}

void *ion_carveout_heap_map_kernel(struct ion_heap *heap,
				   struct ion_buffer *buffer)
{
	int mtype = MT_MEMORY_NONCACHED;

	if (buffer->flags & ION_FLAG_CACHED)
		mtype = MT_MEMORY;

	return __arm_ioremap(buffer->priv_phys, buffer->size,
			      mtype);
}

void ion_carveout_heap_unmap_kernel(struct ion_heap *heap,
				    struct ion_buffer *buffer)
{
	__arm_iounmap(buffer->vaddr);
	buffer->vaddr = NULL;
	return;
}

int ion_carveout_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
			       struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
			       __phys_to_pfn(buffer->priv_phys) + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static struct ion_heap_ops carveout_heap_ops = {
	.allocate = ion_carveout_heap_allocate,
	.free = ion_carveout_heap_free,
	.phys = ion_carveout_heap_phys,
	.map_user = ion_carveout_heap_map_user,
	.map_kernel = ion_carveout_heap_map_kernel,
	.unmap_kernel = ion_carveout_heap_unmap_kernel,
	.map_dma = ion_carveout_heap_map_dma,
	.unmap_dma = ion_carveout_heap_unmap_dma,
};

void ion_carveout_heap_debug_show_chunk(struct gen_pool *pool,
	struct gen_pool_chunk *chunk, void *data)
{
	struct seq_file *s = (struct seq_file *)data;
	int order = pool->min_alloc_order;
	unsigned long *map = chunk->bits;
	unsigned long index, end, size, start;

	size = (chunk->end_addr - chunk->start_addr) >> order;
	start = 0;
next:
	index = find_next_zero_bit(map, size, start);
	if (index >= size)
		return;

	end = find_next_bit(map, size, index + 1);
	if (end >= size)
		end = size;

	seq_printf(s, "%08lx %12.lu\n",
		chunk->start_addr + (index << order), (end - index) << order);
	start = end;
	if (start < size)
		goto next;
}

static int ion_carveout_heap_debug_show(struct ion_heap *heap,
	struct seq_file *s, void *unused)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);

	seq_printf(s, "\ncarveout heap free list, avail: %u\n",
		gen_pool_avail(carveout_heap->pool));

	seq_printf(s, "%8.s %12.s\n", "phys", "size");
	gen_pool_for_each_chunk(carveout_heap->pool,
		ion_carveout_heap_debug_show_chunk, s);

	return 0;
}

struct ion_heap *ion_carveout_heap_create(struct ion_platform_heap *heap_data)
{
	struct ion_carveout_heap *carveout_heap;

	carveout_heap = kzalloc(sizeof(struct ion_carveout_heap), GFP_KERNEL);
	if (!carveout_heap)
		return ERR_PTR(-ENOMEM);

	carveout_heap->pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!carveout_heap->pool) {
		kfree(carveout_heap);
		return ERR_PTR(-ENOMEM);
	}
	carveout_heap->base = heap_data->base;
	gen_pool_add(carveout_heap->pool, carveout_heap->base, heap_data->size,
		     -1);
	carveout_heap->heap.ops = &carveout_heap_ops;
	carveout_heap->heap.type = ION_HEAP_TYPE_CARVEOUT;
	carveout_heap->heap.debug_show = ion_carveout_heap_debug_show;

	return &carveout_heap->heap;
}

void ion_carveout_heap_destroy(struct ion_heap *heap)
{
	struct ion_carveout_heap *carveout_heap =
	     container_of(heap, struct  ion_carveout_heap, heap);

	gen_pool_destroy(carveout_heap->pool);
	kfree(carveout_heap);
	carveout_heap = NULL;
}

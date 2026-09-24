#pragma once

#include <stdint.h>

struct limine_memmap_response;

namespace physical_memory {

/*
 * Initialize the physical page allocator from Limine's memory map.
 * Only entries explicitly marked as usable are made available, and physical
 * address zero is kept reserved. Must be called before allocation functions.
 */
void initialize(const limine_memmap_response* memory_map);

/*
 * Allocate one 4 KiB physical page and store its physical address in the
 * output argument. Returns false when no usable pages remain.
 */
bool allocate_page(uintptr_t* physical_address);

/*
 * Return one previously allocated 4 KiB physical page to the free pool.
 * Returns false if the address is invalid or was not currently allocated.
 */
bool free_page(uintptr_t physical_address);

/*
 * Return the number of usable physical pages discovered during initialization.
 */
uint64_t total_pages();

/*
 * Return the number of currently unallocated physical pages.
 */
uint64_t free_pages();

} // namespace physical_memory

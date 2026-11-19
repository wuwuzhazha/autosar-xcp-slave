#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/* Host tests run in a mapped 32-bit address space: objects are registered in
   test_stubs.c and the core reaches them through XCP_MEM_PTR/XCP_MEM_ADDR.
   This keeps the suite independent of PIE, ASLR, and the host pointer width. */
unsigned char *test_mem_ptr(unsigned int addr);
unsigned int test_mem_addr(const void *ptr);
unsigned int test_mem_range_ok(unsigned int addr, unsigned int size);

#define XCP_MEM_PTR(addr) test_mem_ptr(addr)
#define XCP_MEM_ADDR(ptr) test_mem_addr(ptr)
#define XCP_CHECK_READ_ACCESS(addr, size)  (test_mem_range_ok((unsigned int)(addr), (unsigned int)(size)) ? 0U : 1U)
#define XCP_CHECK_WRITE_ACCESS(addr, size) (test_mem_range_ok((unsigned int)(addr), (unsigned int)(size)) ? 0U : 1U)

#include "NvM.h"

#endif

#ifndef TEST_STUBS_H
#define TEST_STUBS_H

/* Register a host object to obtain a stable 32-bit XCP address for it. Repeat
   calls for the same object return the same address. */
unsigned int test_mem_register(void *ptr, unsigned int size);

#define TEST_ADDR(obj) test_mem_register((void *)&(obj), (unsigned int)sizeof(obj))

#endif

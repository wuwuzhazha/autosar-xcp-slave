#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

#define XCP_CHECK_READ_ACCESS(addr, size)  ((uint32)(uintptr_t)(addr) >= 0x80000000UL)
#define XCP_CHECK_WRITE_ACCESS(addr, size) ((uint32)(uintptr_t)(addr) >= 0x80000000UL)

#include "NvM.h"

#endif

/**
 * @file    test_config.h
 * @brief   测试专用配置（-include 强制包含，覆盖 Xcp_Config.h 目标相关宏）
 */
#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/* 地址保护：高位地址(>=0x80000000)视为禁读/禁写，返回非0触发 ERR_ACCESS_DENIED */
#define XCP_CHECK_READ_ACCESS(addr, size)  ((uint32)(uintptr_t)(addr) >= 0x80000000UL)
#define XCP_CHECK_WRITE_ACCESS(addr, size) ((uint32)(uintptr_t)(addr) >= 0x80000000UL)

/* 使 Xcp_Resume.c 认为 NvM 可用 */
#include "NvM.h"

#endif /* TEST_CONFIG_H */

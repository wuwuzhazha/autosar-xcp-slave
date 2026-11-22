#include "Cdd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "StandardTypes.h"
#include "ComStack_Types.h"
#include "CanIf.h"
#include "Det.h"
#include "NvM.h"
#include "Xcp.h"
#include "Xcp_Resume.h"
#include "Xcp_Internal.h"
#include "Os.h"
#include "test_stubs.h"

#define TEST_TX_RING 64

uint8  test_tx_buf[TEST_TX_RING][64];
uint32 test_tx_len[TEST_TX_RING];
uint8  test_tx_head = 0;
uint8  test_tx_tail = 0;
uint32 test_tx_count = 0;
uint32 test_tx_drop  = 0;
boolean test_tx_fail_next = FALSE;
uint32  test_tx_fail_count = 0U;

/* SchM probe state; defined next to the SchM stubs below. */
extern uint32   test_schm_depth;
extern boolean  test_schm_tx_under_lock;

Std_ReturnType CanIf_Transmit(PduIdType tx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    (void)tx_pdu_id;
    if (test_schm_depth > 0U)
    {
        test_schm_tx_under_lock = TRUE;
    }
    if ((test_tx_fail_next) || (test_tx_fail_count > 0U))
    {
        if (test_tx_fail_next)
        {
            test_tx_fail_next = FALSE;
        }
        else
        {
            test_tx_fail_count--;
        }
        test_tx_drop++;
        return E_NOT_OK;
    }

    if (pdu_info_ptr->sdu_length > 64U)
    {
        return E_NOT_OK;
    }
    memcpy(test_tx_buf[test_tx_tail], pdu_info_ptr->sdu_data_ptr, pdu_info_ptr->sdu_length);
    test_tx_len[test_tx_tail] = pdu_info_ptr->sdu_length;
    test_tx_tail = (uint8)((test_tx_tail + 1U) % TEST_TX_RING);
    test_tx_count++;
    return E_OK;
}

Std_ReturnType FrIf_Transmit(PduIdType tx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    return CanIf_Transmit(tx_pdu_id, pdu_info_ptr);
}
Std_ReturnType SoAd_IfTransmit(PduIdType tx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    return CanIf_Transmit(tx_pdu_id, pdu_info_ptr);
}
Std_ReturnType FrIf_DisableLPdu(uint16 lpdu_handle) { (void)lpdu_handle; return E_OK; }
Std_ReturnType FrIf_ReconfigLPdu(uint16 lpdu_handle, uint16 frame_id, uint8 chn_idx,
                                 uint8 cycle_rep, uint16 offset, uint16 len, uint16 cfg_idx)
{
    (void)lpdu_handle;(void)frame_id;(void)chn_idx;(void)cycle_rep;(void)offset;(void)len;(void)cfg_idx;
    return E_OK;
}

uint32 test_tx_pop(uint8 *out, uint32 *len)
{
    uint32 n;
    if (test_tx_head == test_tx_tail)
    {
        return 0U;
    }
    n = test_tx_len[test_tx_head];
    if ((out != NULL) && (len != NULL))
    {
        *len = n;
        memcpy(out, test_tx_buf[test_tx_head], n);
#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)

        if ((n > 3U) && (((uint32)out[0] | ((uint32)out[1] << 8U)) + 3U) <= n)
        {
            uint16 ml = (uint16)out[0] | ((uint16)out[1] << 8U);
            memmove(out, out + 3U, ml);
            *len = ml;
            n = ml;
        }
#endif
#if (XCP_ON_ETHERNET_ENABLED == STD_ON)

        if ((n > 4U) && (((uint32)out[0] | ((uint32)out[1] << 8U)) + 4U) <= n)
        {
            uint16 ml = (uint16)out[0] | ((uint16)out[1] << 8U);
            memmove(out, out + 4U, ml);
            *len = ml;
            n = ml;
        }
#endif
    }
    test_tx_head = (uint8)((test_tx_head + 1U) % TEST_TX_RING);
    return n;
}

uint32 test_tx_pending(void)
{
    return (uint32)((test_tx_tail - test_tx_head) & 0xFFU);
}

uint8  test_det_api;
uint8  test_det_err;
uint32 test_det_count = 0;

Std_ReturnType Det_ReportError(uint16 module_id, uint8 instance_id, uint8 api_id, uint8 error_id)
{
    (void)module_id;
    (void)instance_id;
    test_det_api = api_id;
    test_det_err = error_id;
    test_det_count++;
    return E_OK;
}

static uint8   nvm_store[256 * 1024];
static uint32  nvm_store_size = 0;
static boolean nvm_valid = FALSE;

void test_nvm_corrupt(const uint8 *data, uint32 len)
{
    if (len > sizeof(nvm_store)) { len = sizeof(nvm_store); }
    memcpy(nvm_store, data, len);
    nvm_store_size = len;
    nvm_valid = TRUE;
}

Std_ReturnType NvM_ReadBlock(uint16 block_id, void *data_ptr)
{
    (void)block_id;
    if (nvm_valid && (nvm_store_size > 0U))
    {
        memcpy(data_ptr, nvm_store, nvm_store_size);
        return E_OK;
    }
    return E_NOT_OK;
}

Std_ReturnType NvM_InvalidateNvBlock(uint16 block_id)
{
    (void)block_id;
    nvm_valid = FALSE;
    return E_OK;
}
Std_ReturnType NvM_WriteBlock(uint16 block_id, const void *data_ptr)
{
    (void)block_id;
#if (XCP_RESUME_MODE_ENABLED == STD_ON)

    nvm_store_size = sizeof(Xcp_ResumeDataType);
    if (nvm_store_size > sizeof(nvm_store))
    {
        return E_NOT_OK;
    }
    memcpy(nvm_store, data_ptr, nvm_store_size);
    nvm_valid = TRUE;
#else
    (void)data_ptr;
#endif
    return E_OK;
}

static uint8 test_seed_len_override = 0U;
void test_seed_override_len(uint8 v) { test_seed_len_override = v; }

void Xcp_GetSeed_Callout(uint8 resource, uint8 *seed, uint8 *seed_length)
{
    if ((seed == NULL) || (seed_length == NULL))
    {
        return;
    }
    seed[0] = resource;
    seed[1] = (uint8)(resource ^ 0x5AU);
    seed[2] = (uint8)(resource ^ 0xA5U);
    seed[3] = 0x42U;

    if (test_seed_len_override != 0U)
    {
        seed[4] = 0x11U; seed[5] = 0x22U; seed[6] = 0x33U; seed[7] = 0x44U;
        *seed_length = test_seed_len_override;
        return;
    }
    *seed_length = 4U;
}

boolean Xcp_VerifyKey_Callout(uint8 resource, const uint8 *key, uint8 key_length)
{
    uint8 i;
    uint8 expected[4];
    if ((key == NULL) || (key_length != 4U))
    {
        return FALSE;
    }
    expected[0] = resource;
    expected[1] = (uint8)(resource ^ 0x5AU);
    expected[2] = (uint8)(resource ^ 0xA5U);
    expected[3] = 0x42U;
    for (i = 0U; i < 4U; i++)
    {
        if ((uint8)(key[i] ^ expected[i]) != (uint8)0xFFU)
        {
            return FALSE;
        }
    }
    return TRUE;
}

uint8 test_flash_mem[0x20000];
boolean test_flash_erase_fail = FALSE;

Std_ReturnType Xcp_FlashErase_Callout(uint32 address, uint32 length)
{
    if (test_flash_erase_fail
        || (address < 0x08000000UL) || ((address - 0x08000000UL + length) > 0x10000UL))
    {
        return E_NOT_OK;
    }
    memset(&test_flash_mem[address - 0x08000000UL], 0xFF, length);
    return E_OK;
}
uint32 test_flash_program_calls = 0U;
boolean test_flash_program_fail = FALSE;

Std_ReturnType Xcp_FlashProgram_Callout(uint32 address, const uint8 *data, uint32 length)
{
    test_flash_program_calls++;
    if (test_flash_program_fail
        || (address < 0x08000000UL) || ((address - 0x08000000UL + length) > 0x10000UL) || (data == NULL))
    {
        return E_NOT_OK;
    }
    memcpy(&test_flash_mem[address - 0x08000000UL], data, length);
    return E_OK;
}

uint32 test_flash_verify_expected = 0;
boolean test_flash_verify_fail    = FALSE;

Std_ReturnType Xcp_FlashVerify_Callout(uint32 address, uint32 length, uint32 expected_value)
{
    (void)address;
    (void)length;
    if (test_flash_verify_fail || (expected_value != test_flash_verify_expected))
    {
        return E_NOT_OK;
    }
    return E_OK;
}

static uint8 test_checksum_len_override = 0U;
void test_checksum_override_len(uint8 v) { test_checksum_len_override = v; }

Std_ReturnType Xcp_BuildChecksum_Callout(uint32 addr, uint32 len, uint32 *checksum, uint8 *checksumLen)
{
    uint32 sum = 0U;
    const uint8 *p = NULL;
    uint32 i;

    if (len > 0U)
    {
        p = test_mem_ptr(addr);
        if (p == NULL)
        {
            return E_NOT_OK;
        }
    }
    for (i = 0U; i < len; i++)
    {
        sum += p[i];
    }
    *checksum   = sum;
    *checksumLen = (test_checksum_len_override != 0U) ? test_checksum_len_override : 4U;
    return E_OK;
}

uint8 test_cal_ram[2][0x1000];
uint8 test_cal_rom[2][0x1000];

Std_ReturnType Xcp_CopyCalPage_Callout(uint8 src_segment, uint8 src_page, uint8 dst_segment, uint8 dst_page)
{
    (void)src_segment;
    (void)dst_segment;
    memcpy(test_cal_ram[dst_page], test_cal_rom[src_page], 0x1000U);
    return E_OK;
}

Std_ReturnType Xcp_UserCommand_Callout(const uint8 *cmd_data, uint8 cmd_length)
{
    uint8 i;
    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    for (i = 1U; (i < cmd_length) && (i < XCP_MAX_CTO); i++)
    {
        g_xcp_channel.storage_ptr->cto_buffer[i] = cmd_data[i];
    }
#if (XCP_MAX_CTO < 255U)
    g_xcp_channel.storage_ptr->cto_length =
        (uint8)((cmd_length > XCP_MAX_CTO) ? XCP_MAX_CTO : cmd_length);
#else
    g_xcp_channel.storage_ptr->cto_length = cmd_length;
#endif
    Xcp_SendPositiveResponse(g_xcp_channel.storage_ptr->cto_length);
    return E_OK;
}

void Xcp_EcuReset_Callout(void)
{

}

TickType test_os_ticks = 0U;

Std_ReturnType GetCounterValue(uint32 counter_id, TickRefType value)
{
    (void)counter_id;
    if (value == NULL_PTR) { return E_NOT_OK; }
    *value = test_os_ticks;
    return E_OK;
}

Std_ReturnType GetElapsedValue(uint32 counter_id, TickRefType value, TickRefType elapsed_value)
{
    TickType now;
    (void)counter_id;
    if ((value == NULL_PTR) || (elapsed_value == NULL_PTR)) { return E_NOT_OK; }
    now = test_os_ticks;

    if (*value <= now) { *elapsed_value = now - *value; }
    else               { *elapsed_value = (0xFFFFFFFFU - *value) + now + 1U; }
    *value = now;
    return E_OK;
}

Std_ReturnType XCP_CDD_TRANSMIT_NAME(PduIdType pdu_id, const PduInfoType *pdu_info_ptr)
{
    return CanIf_Transmit(pdu_id, pdu_info_ptr);
}

#include "SchM_Xcp.h"
uint32 test_schm_enter_count = 0U;
uint32 test_schm_exit_count  = 0U;
uint32 test_schm_depth       = 0U;
uint32 test_schm_nested_count = 0U;
boolean test_schm_tx_under_lock = FALSE;

void SchM_Enter_Xcp(uint8 exclusive_area)
{
    (void)exclusive_area;
    test_schm_enter_count++;
    if (test_schm_depth > 0U)
    {
        test_schm_nested_count++;
    }
    test_schm_depth++;
}

void SchM_Exit_Xcp(uint8 exclusive_area)
{
    (void)exclusive_area;
    test_schm_exit_count++;
    if (test_schm_depth > 0U)
    {
        test_schm_depth--;
    }
}

/* The test address map: XCP addresses are 32-bit, so host objects are exposed
   at stable fake addresses instead of truncating 64-bit pointers. The table is
   populated on demand through TEST_ADDR() (test_stubs.h). */
#define TEST_MEM_BLOCKS 32

typedef struct
{
    uint32 base;
    uint32 size;
    uint8 *host;
} TestMemBlock;

static TestMemBlock test_mem_blocks[TEST_MEM_BLOCKS];
static uint32 test_mem_count = 0U;
static uint32 test_mem_next  = 0x10000000UL;

unsigned int test_mem_register(void *ptr, unsigned int size)
{
    uintptr_t host = (uintptr_t)ptr;
    uint32 i;

    for (i = 0U; i < test_mem_count; i++)
    {
        uintptr_t block_host = (uintptr_t)test_mem_blocks[i].host;
        if ((host >= block_host) && ((host - block_host) < test_mem_blocks[i].size))
        {
            uint32 offset = (uint32)(host - block_host);

            /* Scopes may reuse a stack slot; grow the block so a larger object
               registered at the same address keeps its full extent accessible. */
            if (size > (test_mem_blocks[i].size - offset))
            {
                test_mem_blocks[i].size = offset + size;
            }
            return (unsigned int)(test_mem_blocks[i].base + offset);
        }
    }

    if ((size == 0U) || (test_mem_count >= TEST_MEM_BLOCKS))
    {
        return 0U;
    }

    test_mem_blocks[test_mem_count].base = test_mem_next;
    test_mem_blocks[test_mem_count].size = size;
    test_mem_blocks[test_mem_count].host = (uint8 *)ptr;
    test_mem_count++;
    test_mem_next += (size + 0xFFFU) & ~0xFFFUL;

    return (unsigned int)test_mem_blocks[test_mem_count - 1U].base;
}

unsigned char *test_mem_ptr(unsigned int addr)
{
    uint32 i;

    for (i = 0U; i < test_mem_count; i++)
    {
        if ((addr >= test_mem_blocks[i].base) &&
            ((addr - test_mem_blocks[i].base) < test_mem_blocks[i].size))
        {
            return (unsigned char *)(test_mem_blocks[i].host + (addr - test_mem_blocks[i].base));
        }
    }
    return NULL;
}

unsigned int test_mem_addr(const void *ptr)
{
    uintptr_t host = (uintptr_t)ptr;
    uint32 i;

    for (i = 0U; i < test_mem_count; i++)
    {
        uintptr_t block_host = (uintptr_t)test_mem_blocks[i].host;
        if ((host >= block_host) && ((host - block_host) < test_mem_blocks[i].size))
        {
            return (unsigned int)(test_mem_blocks[i].base + (uint32)(host - block_host));
        }
    }

    /* The only core caller is GET_ID with XCP_ID_TEXT. String literals in
       different translation units need not be merged by the linker, so register
       this copy on first use; tests register their own objects with TEST_ADDR. */
    if (ptr != NULL)
    {
        const char *s = (const char *)ptr;
        uint32 len = 0U;

        while ((len < 256U) && (s[len] != '\0'))
        {
            len++;
        }
        if (len < 256U)
        {
            return test_mem_register((void *)ptr, len + 1U);
        }
    }
    return 0U;
}

unsigned int test_mem_range_ok(unsigned int addr, unsigned int size)
{
    uint32 i;

    if (size == 0U)
    {
        return 1U;
    }

    for (i = 0U; i < test_mem_count; i++)
    {
        uint32 offset;

        if (addr < test_mem_blocks[i].base)
        {
            continue;
        }
        offset = addr - test_mem_blocks[i].base;
        if ((offset <= test_mem_blocks[i].size) &&
            (size <= (test_mem_blocks[i].size - offset)))
        {
            return 1U;
        }
    }
    return 0U;
}

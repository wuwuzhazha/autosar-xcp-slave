/* C ABI used by the pyxcp integration test. The slave runtime lives in this
   shared library; the Python side moves frames between it and a python-can
   bus, one XCPonCAN frame at a time. */
#include <string.h>

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Callback.h"
#include "Xcp_Types.h"
#include "ComStack_Types.h"
#include "test_stubs.h"

#if defined(_WIN32)
#define BRIDGE_API __declspec(dllexport)
#else
#define BRIDGE_API __attribute__((visibility("default")))
#endif

#define BRIDGE_RX_PDU_ID (0U)
#define BRIDGE_TX_PDU_ID (1U)

#define TEST_FLASH_BASE (0x08000000UL)
#define TEST_FLASH_SIZE (0x20000UL)

extern const Xcp_ConfigType xcp_config;
extern uint32 test_os_ticks;
extern uint32 test_tx_pop(uint8 *out, uint32 *len);
extern uint8 test_flash_mem[TEST_FLASH_SIZE];
extern uint8 test_cal_ram[2][0x1000];
extern uint8 test_cal_rom[2][0x1000];

/* RAM window the master can UPLOAD/DOWNLOAD; address is exposed to Python. */
static uint8 bridge_ram[0x4000];
static uint32 bridge_stim_frames;

BRIDGE_API void bridge_init(void)
{
    Xcp_Init(&xcp_config);
}

BRIDGE_API void bridge_tick(uint32 elapsed_ms)
{
    test_os_ticks += elapsed_ms;
    Xcp_MainFunction();
}

BRIDGE_API uint32 bridge_os_ticks(void)
{
    return test_os_ticks;
}

BRIDGE_API void bridge_rx(const uint8 *data, uint32 length)
{
    PduInfoType pdu;

#if (XCP_DAQ_ENABLED == STD_ON)
    /* The slave routes PIDs below PROGRAM_VERIFY to the STIM path. */
    if ((length > 0U) && (data[0] < XCP_CMD_PROGRAM_VERIFY))
    {
        bridge_stim_frames++;
    }
#endif

    pdu.sdu_data_ptr  = (uint8 *)data;
    pdu.meta_data_ptr = NULL_PTR;
    pdu.sdu_length    = length;

    Xcp_CanIfRxIndication(BRIDGE_RX_PDU_ID, &pdu);
}

BRIDGE_API uint32 bridge_tx_pop(uint8 *out, uint32 *length)
{
    return test_tx_pop(out, length);
}

BRIDGE_API void bridge_tx_confirm(void)
{
    Xcp_CanIfTxConfirmation(BRIDGE_TX_PDU_ID, E_OK);
}

BRIDGE_API void bridge_event(uint16 event_channel)
{
    Xcp_Event(event_channel);
}

BRIDGE_API uint32 bridge_stim_frame_count(void)
{
    return bridge_stim_frames;
}

BRIDGE_API void bridge_stim_frame_reset(void)
{
    bridge_stim_frames = 0U;
}

BRIDGE_API uint32 bridge_dto_drop_count(void)
{
#if (XCP_DAQ_ENABLED == STD_ON) && (XCP_ENABLE_DTO_DROP_STATS == STD_ON)
    return g_xcp_channel.storage_ptr->dropped_dto_counter;
#else
    return 0U;
#endif
}

BRIDGE_API void bridge_dto_drop_reset(void)
{
#if (XCP_DAQ_ENABLED == STD_ON) && (XCP_ENABLE_DTO_DROP_STATS == STD_ON)
    g_xcp_channel.storage_ptr->dropped_dto_counter = 0U;
#endif
}

BRIDGE_API uint32 bridge_ram_addr(void)
{
    return test_mem_register(bridge_ram, (uint32)sizeof(bridge_ram));
}

BRIDGE_API void bridge_ram_read(uint8 *out, uint32 length)
{
    if (length > (uint32)sizeof(bridge_ram))
    {
        length = (uint32)sizeof(bridge_ram);
    }
    memcpy(out, bridge_ram, length);
}

BRIDGE_API void bridge_ram_write(const uint8 *in, uint32 length)
{
    if (length > (uint32)sizeof(bridge_ram))
    {
        length = (uint32)sizeof(bridge_ram);
    }
    memcpy(bridge_ram, in, length);
}

BRIDGE_API uint32 bridge_flash_read(uint32 address, uint8 *out, uint32 length)
{
    uint32 offset;

    if (address < TEST_FLASH_BASE)
    {
        return 0U;
    }
    offset = address - TEST_FLASH_BASE;
    if ((offset > TEST_FLASH_SIZE) || (length > (TEST_FLASH_SIZE - offset)))
    {
        return 0U;
    }
    memcpy(out, &test_flash_mem[offset], length);
    return length;
}

BRIDGE_API uint32 bridge_cal_read(uint8 page, uint8 *out, uint32 length)
{
    if ((page > 1U) || (length > 0x1000U))
    {
        return 0U;
    }
    memcpy(out, test_cal_ram[page], length);
    return length;
}

BRIDGE_API void bridge_cal_write(uint8 page, const uint8 *in, uint32 length)
{
    if ((page <= 1U) && (length <= 0x1000U))
    {
        memcpy(test_cal_ram[page], in, length);
    }
}

BRIDGE_API void bridge_cal_rom_write(uint8 page, const uint8 *in, uint32 length)
{
    if ((page <= 1U) && (length <= 0x1000U))
    {
        memcpy(test_cal_rom[page], in, length);
    }
}

/* C ABI used by the pyxcp integration test. The slave runtime lives in this
   shared library; the Python side moves frames between it and a python-can
   bus, one XCPonCAN frame at a time. */
#include <string.h>

#include "Xcp.h"
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

extern const Xcp_ConfigType xcp_config;
extern uint32 test_os_ticks;
extern uint32 test_tx_pop(uint8 *out, uint32 *len);

/* RAM window the master can UPLOAD/DOWNLOAD; address is exposed to Python. */
static uint8 bridge_ram[256];

BRIDGE_API void bridge_init(void)
{
    Xcp_Init(&xcp_config);
}

BRIDGE_API void bridge_tick(uint32 elapsed_ms)
{
    test_os_ticks += elapsed_ms;
    Xcp_MainFunction();
}

BRIDGE_API void bridge_rx(const uint8 *data, uint32 length)
{
    PduInfoType pdu;

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

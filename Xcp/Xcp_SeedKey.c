#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_SeedKey.h"
#include "Xcp_Callback.h"
#include "Xcp_Daq.h"

#include <string.h>

#if (XCP_SEED_KEY_ENABLED == STD_ON)

static void Xcp_GenerateSeed(uint8 resource, uint8* seed, uint8* seed_length);
static boolean Xcp_VerifyKey(uint8 resource, const uint8* key, uint8 key_length);

sint16 Xcp_CmdGetSeed(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 mode = 0U;
    uint8 resource = 0U;
    uint8 seed_length = 0U;

    (void)cmd_length;

    mode = cmd_data[1];
    resource = cmd_data[2];

    if (mode > 0x01U)
    {

        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (mode == 0x01U)
    {
        if (!g_xcp_manager.runtime_ptr->seed_requested)
        {
            return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
        }

        g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
        g_xcp_manager.storage_ptr->cto_buffer[1] = 0x00U;
        return (sint16)(2U);
    }

    if ((resource != XCP_RESOURCE_CAL_PAG) &&
        (resource != XCP_RESOURCE_DAQ) &&
        (resource != XCP_RESOURCE_STIM) &&
        (resource != XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if ((g_xcp_manager.runtime_ptr->protection_status & resource) == 0U)
    {

        g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
        g_xcp_manager.storage_ptr->cto_buffer[1] = 0x00U;
        return (sint16)(2U);
    }

    Xcp_GenerateSeed(resource, g_xcp_manager.runtime_ptr->seed, &seed_length);

    if (seed_length > XCP_MAX_CTO - 2U)
    {
        seed_length = XCP_MAX_CTO - 2U;
    }

    if (seed_length > (uint8) sizeof(g_xcp_manager.runtime_ptr->seed))
    {
        seed_length = (uint8) sizeof(g_xcp_manager.runtime_ptr->seed);
    }

    g_xcp_manager.runtime_ptr->seed_length = seed_length;
    g_xcp_manager.runtime_ptr->seed_resource = resource;
    g_xcp_manager.runtime_ptr->seed_requested = TRUE;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = seed_length;

    (void)memcpy(&g_xcp_manager.storage_ptr->cto_buffer[2], g_xcp_manager.runtime_ptr->seed, seed_length);

    return (sint16)(2U + seed_length);
}

sint16 Xcp_CmdUnlock(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 key_length = 0U;
    const uint8* key = NULL;

    key_length = cmd_data[1];
    key = &cmd_data[2];

    if (!g_xcp_manager.runtime_ptr->seed_requested)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    if ((key_length == 0U) || (key_length > XCP_MAX_KEY_LENGTH))
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    if ((2U + key_length) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_SYNTAX);
    }

    if (Xcp_VerifyKey(g_xcp_manager.runtime_ptr->seed_resource, key, key_length))
    {

        uint8 unlocked_resource = g_xcp_manager.runtime_ptr->seed_resource;
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)(~unlocked_resource);
        g_xcp_manager.runtime_ptr->seed_requested = FALSE;

        g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
        g_xcp_manager.storage_ptr->cto_buffer[1] = unlocked_resource;

        return (sint16)(2U);
    }
    else
    {

        g_xcp_manager.runtime_ptr->seed_requested = FALSE;
#if (XCP_DAQ_ENABLED == STD_ON)
        Xcp_FreeDaqLists();
#endif
        g_xcp_manager.runtime_ptr->connection_state = XCP_DISCONNECTED;
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
}

boolean Xcp_IsResourceUnlocked(uint8 resource)
{
#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (g_xcp_manager.runtime_ptr == NULL)
    {
        return FALSE;
    }

    return ((g_xcp_manager.runtime_ptr->protection_status & resource) == 0U);
#else
    (void)resource;
    return TRUE;
#endif
}

void Xcp_InitProtection(void)
{
#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (g_xcp_manager.runtime_ptr == NULL)
    {
        return;
    }

    g_xcp_manager.runtime_ptr->protection_status = XCP_RESOURCE_CAL_PAG |
                                    XCP_RESOURCE_DAQ |
                                    XCP_RESOURCE_STIM |
                                    XCP_RESOURCE_PGM;
    g_xcp_manager.runtime_ptr->seed_requested = FALSE;
    g_xcp_manager.runtime_ptr->seed_resource = 0U;
    g_xcp_manager.runtime_ptr->seed_length = 0U;
#else
    if (g_xcp_manager.runtime_ptr != NULL)
    {
        g_xcp_manager.runtime_ptr->protection_status = 0U;
    }
#endif
}

static void Xcp_GenerateSeed(uint8 resource, uint8* seed, uint8* seed_length)
{

    Xcp_GetSeed_Callout(resource, seed, seed_length);
}

static boolean Xcp_VerifyKey(uint8 resource, const uint8* key, uint8 key_length)
{

    return Xcp_VerifyKey_Callout(resource, key, key_length);
}

#endif

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_SeedKey.h"
#include "Xcp_Callback.h"
#include "Xcp_Daq.h"

#include <string.h>

#if (XCP_SEED_KEY_ENABLED == STD_ON)

static void Xcp_GenerateSeed(uint8 resource, uint8* seed, uint8* seed_length);
static boolean Xcp_VerifyKey(uint8 resource, const uint8* key, uint8 key_length);

sint16 Xcp_CmdGetSeed(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 mode = 0U;
    uint8 resource = 0U;
    uint8 seed_length = 0U;

    (void)cmd_length;

    mode = cmd_data[1];
    resource = cmd_data[2];

    if (mode > 0x01U)
    {

        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    if (mode == 0x01U)
    {
        uint8 remaining;
        uint8 chunk;

        if (!ch->runtime_ptr->seed_requested)
        {
            return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
        }

        if (ch->runtime_ptr->seed_length >= ch->runtime_ptr->seed_total)
        {
            ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
            ch->storage_ptr->cto_buffer[1] = 0x00U;
            return (sint16)(2U);
        }

        remaining = (uint8)(ch->runtime_ptr->seed_total - ch->runtime_ptr->seed_length);
        chunk = (remaining > (XCP_MAX_CTO - 2U)) ? (uint8)(XCP_MAX_CTO - 2U) : remaining;

        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        ch->storage_ptr->cto_buffer[1] = chunk;
        (void)memcpy(&ch->storage_ptr->cto_buffer[2],
                     &ch->runtime_ptr->seed[ch->runtime_ptr->seed_length], chunk);

        ch->runtime_ptr->seed_length = (uint8)(ch->runtime_ptr->seed_length + chunk);
        return (sint16)(2U + chunk);
    }

    if ((resource != XCP_RESOURCE_CAL_PAG) &&
        (resource != XCP_RESOURCE_DAQ) &&
        (resource != XCP_RESOURCE_STIM) &&
        (resource != XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if ((ch->runtime_ptr->protection_status & resource) == 0U)
    {

        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        ch->storage_ptr->cto_buffer[1] = 0x00U;
        return (sint16)(2U);
    }

    Xcp_GenerateSeed(resource, ch->runtime_ptr->seed, &seed_length);

    if (seed_length > (uint8) sizeof(ch->runtime_ptr->seed))
    {
        seed_length = (uint8) sizeof(ch->runtime_ptr->seed);
    }

    ch->runtime_ptr->seed_total = seed_length;

    if (seed_length > XCP_MAX_CTO - 2U)
    {
        seed_length = XCP_MAX_CTO - 2U;
    }

    ch->runtime_ptr->seed_length = seed_length;
    ch->runtime_ptr->seed_resource = resource;
    ch->runtime_ptr->seed_requested = TRUE;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = seed_length;

    (void)memcpy(&ch->storage_ptr->cto_buffer[2], ch->runtime_ptr->seed, seed_length);

    return (sint16)(2U + seed_length);
}

sint16 Xcp_CmdUnlock(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 key_length = 0U;
    const uint8* key = NULL;

    key_length = cmd_data[1];
    key = &cmd_data[2];

    if (!ch->runtime_ptr->seed_requested)
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

    if (Xcp_VerifyKey(ch->runtime_ptr->seed_resource, key, key_length))
    {

        uint8 unlocked_resource = ch->runtime_ptr->seed_resource;
        ch->runtime_ptr->protection_status &= (uint8)(~unlocked_resource);
        ch->runtime_ptr->seed_requested = FALSE;

        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        ch->storage_ptr->cto_buffer[1] = unlocked_resource;

        return (sint16)(2U);
    }
    else
    {

        ch->runtime_ptr->seed_requested = FALSE;
        Xcp_SessionClose();
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
}

boolean Xcp_IsResourceUnlocked(Xcp_Channel *ch, uint8 resource)
{
#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (ch->runtime_ptr == NULL)
    {
        return FALSE;
    }

    return ((ch->runtime_ptr->protection_status & resource) == 0U);
#else
    (void)resource;
    return TRUE;
#endif
}

void Xcp_InitProtection(Xcp_Channel *ch)
{
#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (ch->runtime_ptr == NULL)
    {
        return;
    }

    ch->runtime_ptr->protection_status = XCP_RESOURCE_CAL_PAG |
                                    XCP_RESOURCE_DAQ |
                                    XCP_RESOURCE_STIM |
                                    XCP_RESOURCE_PGM;
    ch->runtime_ptr->seed_requested = FALSE;
    ch->runtime_ptr->seed_resource = 0U;
    ch->runtime_ptr->seed_length = 0U;
#else
    if (ch->runtime_ptr != NULL)
    {
        ch->runtime_ptr->protection_status = 0U;
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

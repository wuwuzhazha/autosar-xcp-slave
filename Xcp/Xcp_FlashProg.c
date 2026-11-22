#include "StandardTypes.h"
#include <string.h>
#include "Xcp_Internal.h"
#include "Xcp_Daq.h"
#include "Xcp_Callback.h"
#include "Xcp_SeedKey.h"
#include "Xcp_FlashProg.h"

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)

#define XCP_SECTORS  (g_xcp_channel.config_ptr->xcp_flash_sector_config)

static uint32 Xcp_ExpectedProgramSize = 0U;

static boolean Xcp_CheckProgramRange(Xcp_Channel *ch, uint32 addr, uint32 len)
{
    for (uint8 i = 0U; i < XCP_MAX_FLASH_SECTOR; i++)
    {
        uint32 sector_start = XCP_SECTORS[i].start_address;
        uint32 sector_end   = sector_start + XCP_SECTORS[i].length;

        if ((addr >= sector_start) && (addr < sector_end) && (len <= (sector_end - addr)))
        {
            return (XCP_SECTORS[i].program_sequence_number > 0U) ? TRUE : FALSE;
        }
    }

    return FALSE;
}

/* Block size declared via PROGRAM_PREPARE; 0 = no declared limit */
static boolean Xcp_CheckProgramSize(Xcp_Channel *ch, uint32 data_size)
{
    if (Xcp_ExpectedProgramSize == 0U)
    {
        return TRUE;
    }
    if ((ch->runtime_ptr->pgm_size + data_size) > Xcp_ExpectedProgramSize)
    {
        return FALSE;
    }
    return TRUE;
}

sint16 Xcp_CmdProgramStart(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_PGM_ACTIVE);
    }

#if (XCP_DAQ_ENABLED == STD_ON)
    {
        uint16 i;

        for (i = 0U; i < ch->runtime_ptr->daq_count; i++)
        {
            if (ch->storage_ptr->daq_list[i].state == XCP_DAQ_STATE_RUNNING)
            {
                return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
            }
        }
    }
#endif

    ch->runtime_ptr->pgm_started = TRUE;
    ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_STARTED;
    ch->runtime_ptr->pgm_address = 0U;
    ch->runtime_ptr->pgm_size = 0U;

    Xcp_ExpectedProgramSize = 0U;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 0x00U;

    ch->storage_ptr->cto_buffer[2] = 0x00U;
#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[2] |= XCP_COMM_OPT_MASTER_BLOCK_MODE;
#endif
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[2] |= XCP_COMM_OPT_INTERLEAVED_MODE;
    ch->storage_ptr->cto_buffer[6] = XCP_CMD_QUEUE_SIZE;
#else
    ch->storage_ptr->cto_buffer[6] = 0x00U;
#endif
    ch->storage_ptr->cto_buffer[3] = XCP_MAX_CTO;
    ch->storage_ptr->cto_buffer[4] = XCP_MAX_BS;
    ch->storage_ptr->cto_buffer[5] = XCP_MIN_ST;

    return (sint16)(7U);
}

sint16 Xcp_CmdProgramClear(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8          mode = 0U;
    uint32         clear_range = 0U;
    uint32         clear_address = 0U;
    Std_ReturnType result = E_NOT_OK;

    (void) cmd_length;

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    mode = cmd_data[1];

    if (mode != 0x00U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    clear_range = ((uint32) cmd_data[4]) | ((uint32) cmd_data[5] << 8U) | ((uint32) cmd_data[6] << 16U)
                  | ((uint32) cmd_data[7] << 24U);

    clear_address = ch->runtime_ptr->mta;

    boolean valid_address = FALSE;
    uint32  clear_end = clear_address + clear_range;

    if (clear_end < clear_address)
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    uint32 current_check_addr = clear_address;

    while (current_check_addr < clear_end)
    {
        boolean sector_found = FALSE;

        for (uint8 i = 0U; i < XCP_MAX_FLASH_SECTOR; i++)
        {
            uint32 sector_start = XCP_SECTORS[i].start_address;
            uint32 sector_end_addr = sector_start + XCP_SECTORS[i].length;

            if ((current_check_addr >= sector_start) && (current_check_addr < sector_end_addr))
            {

                if (XCP_SECTORS[i].clear_sequence_number == 0U)
                {

                    return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
                }

                if (clear_end > sector_end_addr)
                {
                    current_check_addr = sector_end_addr;
                }
                else
                {
                    current_check_addr = clear_end;
                }

                sector_found = TRUE;
                break;
            }
        }

        if (!sector_found)
        {

            valid_address = FALSE;
            break;
        }

        if (current_check_addr >= clear_end)
        {
            valid_address = TRUE;
        }
    }

    if (!valid_address)
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_CLEARING;

    result = Xcp_FlashErase_Callout(clear_address, clear_range);

    if (result != E_OK)
    {
        ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_STARTED;
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    ch->runtime_ptr->mta = clear_address + clear_range;
    ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_STARTED;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdProgram(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8          number_of_data_elements = 0U;
    uint8          data_size = 0U;
    Std_ReturnType result = E_NOT_OK;

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    number_of_data_elements = cmd_data[1];

    if (number_of_data_elements == 0U)
    {
        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        return (sint16)(1U);
    }

    data_size = number_of_data_elements;

    if ((data_size + 2U) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (!Xcp_CheckProgramRange(&g_xcp_channel, ch->runtime_ptr->mta, data_size))
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    if (!Xcp_CheckProgramSize(ch, data_size))
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_PROGRAMMING;

    result = Xcp_FlashProgram_Callout(ch->runtime_ptr->mta, &cmd_data[2], data_size);

    if (result != E_OK)
    {
        ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_STARTED;
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    ch->runtime_ptr->mta += data_size;
    ch->runtime_ptr->pgm_size += data_size;
    ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_STARTED;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdProgramReset(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    if (ch->runtime_ptr == NULL)
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    ch->runtime_ptr->pgm_started = FALSE;
    ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_IDLE;
    Xcp_SessionClose();
    Xcp_EcuReset_Callout();

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetPgmProcessorInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

    ch->storage_ptr->cto_buffer[1] = XCP_PGM_PROP_ABSOLUTE_MODE;

    ch->storage_ptr->cto_buffer[2] = XCP_MAX_FLASH_SECTOR;

    return (sint16)(3U);
}

sint16 Xcp_CmdGetSectorInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8                      mode;
    uint8                      sector_number;
    const Xcp_FlashSectorType *sector;

    (void) cmd_length;

    mode = cmd_data[1];
    sector_number = cmd_data[2];

    if (sector_number >= XCP_MAX_FLASH_SECTOR)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    sector = &XCP_SECTORS[sector_number];

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

    if (mode == 0U)
    {

        ch->storage_ptr->cto_buffer[1] = sector->clear_sequence_number;
        ch->storage_ptr->cto_buffer[2] = sector->program_sequence_number;
        ch->storage_ptr->cto_buffer[3] = sector->program_method;
        ch->storage_ptr->cto_buffer[4] = (uint8) (sector->start_address & 0xFFU);
        ch->storage_ptr->cto_buffer[5] = (uint8) ((sector->start_address >> 8U) & 0xFFU);
        ch->storage_ptr->cto_buffer[6] = (uint8) ((sector->start_address >> 16U) & 0xFFU);
        ch->storage_ptr->cto_buffer[7] = (uint8) ((sector->start_address >> 24U) & 0xFFU);

        return (sint16)(8U);
    }
    else if (mode == 1U)
    {

        ch->storage_ptr->cto_buffer[1] = sector->clear_sequence_number;
        ch->storage_ptr->cto_buffer[2] = sector->program_sequence_number;
        ch->storage_ptr->cto_buffer[3] = sector->program_method;
        ch->storage_ptr->cto_buffer[4] = (uint8) (sector->length & 0xFFU);
        ch->storage_ptr->cto_buffer[5] = (uint8) ((sector->length >> 8U) & 0xFFU);
        ch->storage_ptr->cto_buffer[6] = (uint8) ((sector->length >> 16U) & 0xFFU);
        ch->storage_ptr->cto_buffer[7] = (uint8) ((sector->length >> 24U) & 0xFFU);

        return (sint16)(8U);
    }
    else if (mode == 2U)
    {

        ch->storage_ptr->cto_buffer[1] = 0x00U;
        return (sint16)(2U);
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }
}

sint16 Xcp_CmdProgramPrepare(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16 code_size;

    (void) cmd_length;

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    code_size = ((uint16) cmd_data[2]) | ((uint16) cmd_data[3] << 8U);

    if (code_size > XCP_MAX_PGM_BLOCK_SIZE)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    Xcp_ExpectedProgramSize = code_size;
    ch->runtime_ptr->pgm_size    = 0U;
    ch->runtime_ptr->pgm_address = ch->runtime_ptr->mta;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdProgramFormat(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 compression_method;
    uint8 encryption_method;
    uint8 programming_method;
    uint8 access_method;

    (void) cmd_length;

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    compression_method = cmd_data[1];
    encryption_method = cmd_data[2];
    programming_method = cmd_data[3];
    access_method = cmd_data[4];

    if ((compression_method != 0U) || (encryption_method != 0U))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (programming_method != XCP_PGM_MODE_ABSOLUTE)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    if (access_method > 1U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdProgramNext(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8          number_of_data_elements;
    uint8          data_size;
    Std_ReturnType result;

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    number_of_data_elements = cmd_data[1];
    data_size = number_of_data_elements;

    if ((data_size + 2U) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (!Xcp_CheckProgramRange(&g_xcp_channel, ch->runtime_ptr->mta, data_size))
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    if (!Xcp_CheckProgramSize(ch, data_size))
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    result = Xcp_FlashProgram_Callout(ch->runtime_ptr->mta, &cmd_data[2], data_size);

    if (result != E_OK)
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    ch->runtime_ptr->mta += data_size;
    ch->runtime_ptr->pgm_size += data_size;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 0x00U;
    ch->storage_ptr->cto_buffer[2] = 0x00U;
    ch->storage_ptr->cto_buffer[3] = 0x00U;
    ch->storage_ptr->cto_buffer[4] = 0x00U;

    return (sint16)(5U);
}

sint16 Xcp_CmdProgramMax(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8          data_size;
    Std_ReturnType result;

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    data_size = cmd_length - 1U;

    if (data_size == 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (!Xcp_CheckProgramRange(&g_xcp_channel, ch->runtime_ptr->mta, data_size))
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    if (!Xcp_CheckProgramSize(ch, data_size))
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    result = Xcp_FlashProgram_Callout(ch->runtime_ptr->mta, &cmd_data[1], data_size);

    if (result != E_OK)
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }

    ch->runtime_ptr->mta += data_size;
    ch->runtime_ptr->pgm_size += data_size;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdProgramVerify(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8          verification_mode;
    uint16         verification_type;
    uint32         verification_value;
    Std_ReturnType result;

    (void) cmd_length;

    verification_mode = cmd_data[1];
    verification_type = ((uint16) cmd_data[2]) | ((uint16) cmd_data[3] << 8U);
    verification_value = ((uint32) cmd_data[4]) | ((uint32) cmd_data[5] << 8U) | ((uint32) cmd_data[6] << 16U)
                         | ((uint32) cmd_data[7] << 24U);

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_PGM))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (!ch->runtime_ptr->pgm_started)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    (void) verification_mode;
    (void) verification_type;

    result = Xcp_FlashVerify_Callout(ch->runtime_ptr->pgm_address, ch->runtime_ptr->pgm_size,
                                     verification_value);

    if (result != E_OK)
    {
        return Xcp_ResErr(res, XCP_ERR_VERIFY);
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

void Xcp_InitFlashProg(Xcp_Channel *ch)
{
    if (ch->runtime_ptr == NULL)
    {
        return;
    }

    ch->runtime_ptr->pgm_started = FALSE;
    ch->runtime_ptr->pgm_processor = XCP_PGM_STATUS_IDLE;
    ch->runtime_ptr->pgm_address = 0U;
    ch->runtime_ptr->pgm_size = 0U;

    Xcp_ExpectedProgramSize = 0U;
}

const Xcp_FlashSectorType *Xcp_GetSectorConfig(uint8 sector_number)
{
    if (sector_number >= XCP_MAX_FLASH_SECTOR)
    {
        return NULL;
    }

    return &XCP_SECTORS[sector_number];
}

boolean Xcp_IsProgrammingActive(Xcp_Channel *ch)
{
    if (ch->runtime_ptr == NULL)
    {
        return FALSE;
    }

    return ch->runtime_ptr->pgm_started;
}

#endif

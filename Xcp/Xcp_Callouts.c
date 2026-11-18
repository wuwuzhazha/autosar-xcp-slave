#include "Xcp_Config.h"
#include "Xcp_Callback.h"
#include "Xcp_CalPage.h"
#include "Xcp_FlashProg.h"
#include "Xcp_SeedKey.h"

Std_ReturnType Xcp_UserCommand_Callout(const uint8 *cmd_data, uint8 cmd_length)
{
    (void)cmd_data;
    (void)cmd_length;
    return E_NOT_OK;
}

Std_ReturnType Xcp_BuildChecksum_Callout(uint32 addr, uint32 len, uint32 *checksum, uint8 *checksumLen)
{
    (void)addr;
    (void)len;

    if (checksum != NULL)
    {
        *checksum = 0U;
    }

    if (checksumLen != NULL)
    {
        *checksumLen = 0U;
    }

    return E_NOT_OK;
}

Std_ReturnType Xcp_CalcChecksum_Callout(uint32 address, uint32 length, uint8 *checksum_type, uint32 *checksum)
{
    (void)address;
    (void)length;

    if (checksum_type != NULL)
    {
        *checksum_type = 0U;
    }

    if (checksum != NULL)
    {
        *checksum = 0U;
    }

    return E_NOT_OK;
}

Std_ReturnType Xcp_CopyCalPage_Callout(uint8 src_segment, uint8 src_page, uint8 dst_segment, uint8 dst_page)
{
    (void)src_segment;
    (void)src_page;
    (void)dst_segment;
    (void)dst_page;
    return E_NOT_OK;
}

Std_ReturnType Xcp_FlashErase_Callout(uint32 address, uint32 length)
{
    (void)address;
    (void)length;
    return E_NOT_OK;
}

Std_ReturnType Xcp_FlashProgram_Callout(uint32 address, const uint8 *data, uint32 length)
{
    (void)address;
    (void)data;
    (void)length;
    return E_NOT_OK;
}

Std_ReturnType Xcp_FlashVerify_Callout(uint32 address, uint32 length, uint32 expected_value)
{
    (void)address;
    (void)length;
    (void)expected_value;
    return E_NOT_OK;
}

void Xcp_EcuReset_Callout(void)
{

}

void Xcp_GetSeed_Callout(uint8 resource, uint8 *seed, uint8 *seed_length)
{
    (void)resource;

    if (seed != NULL)
    {
        seed[0] = 0U;
    }

    if (seed_length != NULL)
    {
        *seed_length = 0U;
    }
}

boolean Xcp_VerifyKey_Callout(uint8 resource, const uint8 *key, uint8 key_length)
{
    (void)resource;
    (void)key;
    (void)key_length;
    return FALSE;
}

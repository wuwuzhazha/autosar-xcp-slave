#ifndef NVM_H
#define NVM_H

#include "StandardTypes.h"

#define NVM_REQ_OK        (0U)
#define NVM_REQ_NOT_OK    (1U)

Std_ReturnType NvM_ReadBlock(uint16 block_id, void *data_ptr);
Std_ReturnType NvM_WriteBlock(uint16 block_id, const void *data_ptr);
Std_ReturnType NvM_InvalidateNvBlock(uint16 block_id);

#endif /* NVM_H */

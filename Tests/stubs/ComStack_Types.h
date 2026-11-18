#ifndef COMSTACK_TYPES_H
#define COMSTACK_TYPES_H

#include "StandardTypes.h"

typedef uint16 PduIdType;
typedef uint8  NetworkHandleType;

typedef struct
{
    uint8  *sdu_data_ptr;
    uint8  *meta_data_ptr;
    uint32  sdu_length;
} PduInfoType;

#endif /* COMSTACK_TYPES_H */

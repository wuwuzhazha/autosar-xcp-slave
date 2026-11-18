#ifndef STANDARD_TYPES_H
#define STANDARD_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t   sint8;
typedef int16_t  sint16;
typedef int32_t  sint32;
typedef uint8_t  boolean;
typedef uint8_t  Std_ReturnType;

#define E_OK            (0U)
#define E_NOT_OK        (1U)

#define STD_ON          (1U)
#define STD_OFF         (0U)

#ifndef TRUE
#define TRUE            (1U)
#endif
#ifndef FALSE
#define FALSE           (0U)
#endif

typedef struct
{
    uint16 vendorID;
    uint16 moduleID;
    uint8  sw_major_version;
    uint8  sw_minor_version;
    uint8  sw_patch_version;
} Std_VersionInfoType;

#endif /* STANDARD_TYPES_H */

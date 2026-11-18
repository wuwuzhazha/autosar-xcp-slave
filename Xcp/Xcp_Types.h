#ifndef XCP_TYPES_H
#define XCP_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "StandardTypes.h"
#include "ComStack_Types.h"

#define XCP_CMD_CONNECT                 (0xFFU)
#define XCP_CMD_DISCONNECT              (0xFEU)
#define XCP_CMD_GET_STATUS              (0xFDU)
#define XCP_CMD_SYNCH                   (0xFCU)
#define XCP_CMD_GET_COMM_MODE_INFO      (0xFBU)
#define XCP_CMD_GET_ID                  (0xFAU)
#define XCP_CMD_SET_REQUEST             (0xF9U)
#define XCP_CMD_GET_SEED                (0xF8U)
#define XCP_CMD_UNLOCK                  (0xF7U)
#define XCP_CMD_SET_MTA                 (0xF6U)
#define XCP_CMD_UPLOAD                  (0xF5U)
#define XCP_CMD_SHORT_UPLOAD            (0xF4U)
#define XCP_CMD_BUILD_CHECKSUM          (0xF3U)
#define XCP_CMD_TRANSPORT_LAYER_CMD     (0xF2U)
#define XCP_CMD_USER_CMD                (0xF1U)

#define XCP_CMD_DOWNLOAD                (0xF0U)
#define XCP_CMD_DOWNLOAD_NEXT           (0xEFU)
#define XCP_CMD_DOWNLOAD_MAX            (0xEEU)
#define XCP_CMD_SHORT_DOWNLOAD          (0xEDU)
#define XCP_CMD_MODIFY_BITS             (0xECU)

#define XCP_CMD_SET_CAL_PAGE            (0xEBU)
#define XCP_CMD_GET_CAL_PAGE            (0xEAU)
#define XCP_CMD_GET_PAG_PROCESSOR_INFO  (0xE9U)
#define XCP_CMD_GET_SEGMENT_INFO        (0xE8U)
#define XCP_CMD_GET_PAGE_INFO           (0xE7U)
#define XCP_CMD_SET_SEGMENT_MODE        (0xE6U)
#define XCP_CMD_GET_SEGMENT_MODE        (0xE5U)
#define XCP_CMD_COPY_CAL_PAGE           (0xE4U)

#define XCP_CMD_CLEAR_DAQ_LIST          (0xE3U)
#define XCP_CMD_SET_DAQ_PTR             (0xE2U)
#define XCP_CMD_WRITE_DAQ               (0xE1U)
#define XCP_CMD_SET_DAQ_LIST_MODE       (0xE0U)
#define XCP_CMD_GET_DAQ_LIST_MODE       (0xDFU)
#define XCP_CMD_START_STOP_DAQ_LIST     (0xDEU)
#define XCP_CMD_START_STOP_SYNCH        (0xDDU)
#define XCP_CMD_GET_DAQ_CLOCK           (0xDCU)
#define XCP_CMD_READ_DAQ                (0xDBU)
#define XCP_CMD_GET_DAQ_PROCESSOR_INFO  (0xDAU)
#define XCP_CMD_GET_DAQ_RESOLUTION_INFO (0xD9U)
#define XCP_CMD_GET_DAQ_LIST_INFO       (0xD8U)
#define XCP_CMD_GET_DAQ_EVENT_INFO      (0xD7U)
#define XCP_CMD_FREE_DAQ                (0xD6U)
#define XCP_CMD_ALLOC_DAQ               (0xD5U)
#define XCP_CMD_ALLOC_ODT               (0xD4U)
#define XCP_CMD_ALLOC_ODT_ENTRY         (0xD3U)

#define XCP_CMD_PROGRAM_START           (0xD2U)
#define XCP_CMD_PROGRAM_CLEAR           (0xD1U)
#define XCP_CMD_PROGRAM                 (0xD0U)
#define XCP_CMD_PROGRAM_RESET           (0xCFU)
#define XCP_CMD_GET_PGM_PROCESSOR_INFO  (0xCEU)
#define XCP_CMD_GET_SECTOR_INFO         (0xCDU)
#define XCP_CMD_PROGRAM_PREPARE         (0xCCU)
#define XCP_CMD_PROGRAM_FORMAT          (0xCBU)
#define XCP_CMD_PROGRAM_NEXT            (0xCAU)
#define XCP_CMD_PROGRAM_MAX             (0xC9U)
#define XCP_CMD_PROGRAM_VERIFY          (0xC8U)

#define XCP_PID_RES                     (0xFFU)
#define XCP_PID_ERR                     (0xFEU)
#define XCP_PID_EV                      (0xFDU)
#define XCP_PID_SERV                    (0xFCU)

#define XCP_ERR_CMD_SYNCH               (0x00U)
#define XCP_ERR_CMD_BUSY                (0x10U)
#define XCP_ERR_DAQ_ACTIVE              (0x11U)
#define XCP_ERR_PGM_ACTIVE              (0x12U)
#define XCP_ERR_CMD_UNKNOWN             (0x20U)
#define XCP_ERR_CMD_SYNTAX              (0x21U)
#define XCP_ERR_OUT_OF_RANGE            (0x22U)
#define XCP_ERR_WRITE_PROTECTED         (0x23U)
#define XCP_ERR_ACCESS_DENIED           (0x24U)
#define XCP_ERR_ACCESS_LOCKED           (0x25U)
#define XCP_ERR_PAGE_NOT_VALID          (0x26U)
#define XCP_ERR_MODE_NOT_VALID          (0x27U)
#define XCP_ERR_SEGMENT_NOT_VALID       (0x28U)
#define XCP_ERR_SEQUENCE                (0x29U)
#define XCP_ERR_DAQ_CONFIG              (0x2AU)
#define XCP_ERR_MEMORY_OVERFLOW         (0x30U)
#define XCP_ERR_GENERIC                 (0x31U)
#define XCP_ERR_VERIFY                  (0x32U)
#define XCP_ERR_RESOURCE_TEMP_NOT_ACC   (0x33U)

#define XCP_EV_RESUME_MODE              (0x00U)
#define XCP_EV_CLEAR_DAQ                (0x01U)
#define XCP_EV_STORE_DAQ                (0x02U)
#define XCP_EV_STORE_CAL                (0x03U)
#define XCP_EV_CMD_PENDING              (0x05U)
#define XCP_EV_DAQ_OVERLOAD             (0x06U)
#define XCP_EV_SESSION_TERMINATED       (0x07U)
#define XCP_EV_TIME_SYNC               (0x08U)
#define XCP_EV_STIM_TIMEOUT             (0x09U)
#define XCP_EV_SLEEP                    (0x0AU)
#define XCP_EV_WAKE_UP                  (0x0BU)
#define XCP_EV_USER                     (0xFEU)
#define XCP_EV_TRANSPORT               (0xFFU)

#define XCP_SERV_RESET                  (0x00U)
#define XCP_SERV_TEXT                   (0x01U)

#define XCP_CONNECT_MODE_NORMAL         (0x00U)
#define XCP_CONNECT_MODE_USER_DEFINED   (0x01U)

#define XCP_COMM_BASIC_BYTE_ORDER_MOTOROLA  (0x01U)
#define XCP_COMM_BASIC_AG_SHIFT             (1U)
#define XCP_COMM_BASIC_SLAVE_BLOCK_MODE     (0x40U)
#define XCP_COMM_BASIC_OPTIONAL             (0x80U)

#define XCP_COMM_OPT_MASTER_BLOCK_MODE      (0x01U)
#define XCP_COMM_OPT_INTERLEAVED_MODE       (0x02U)

#define XCP_SS_STORE_CAL_REQ                (0x01U)
#define XCP_SS_STORE_DAQ_REQ                (0x04U)
#define XCP_SS_CLEAR_DAQ_REQ                (0x08U)
#define XCP_SS_DAQ_RUNNING                  (0x40U)
#define XCP_SS_RESUME                       (0x80U)

#define XCP_SET_REQ_STORE_CAL_REQ           (0x01U)
#define XCP_SET_REQ_STORE_DAQ_NO_RESUME     (0x02U)
#define XCP_SET_REQ_STORE_DAQ_RESUME        (0x04U)
#define XCP_SET_REQ_CLEAR_DAQ_REQ           (0x08U)

#define XCP_DAQ_PROP_CONFIG_TYPE_DYNAMIC    (0x01U)
#define XCP_DAQ_PROP_PRESCALER_SUPPORTED    (0x02U)
#define XCP_DAQ_PROP_RESUME_SUPPORTED       (0x04U)
#define XCP_DAQ_PROP_BIT_STIM_SUPPORTED     (0x08U)
#define XCP_DAQ_PROP_TIMESTAMP_SUPPORTED    (0x10U)
#define XCP_DAQ_PROP_PID_OFF_SUPPORTED      (0x20U)
#define XCP_DAQ_PROP_OVERLOAD_MSB           (0x40U)
#define XCP_DAQ_PROP_OVERLOAD_EVENT         (0x80U)

#define XCP_DAQ_KEY_OPTIMISATION_SHIFT      (0U)
#define XCP_DAQ_KEY_ADDR_EXT_SHIFT          (4U)
#define XCP_DAQ_KEY_ID_FIELD_SHIFT          (6U)

#define XCP_DAQ_MODE_SELECTED               (0x01U)
#define XCP_DAQ_MODE_DIRECTION_STIM         (0x02U)
#define XCP_DAQ_MODE_TIMESTAMP              (0x10U)
#define XCP_DAQ_MODE_PID_OFF                (0x20U)
#define XCP_DAQ_MODE_RUNNING                (0x40U)
#define XCP_DAQ_MODE_RESUME                 (0x80U)

#define XCP_START_STOP_STOP                 (0x00U)
#define XCP_START_STOP_START                (0x01U)
#define XCP_START_STOP_SELECT               (0x02U)

#define XCP_DAQ_LIST_PROP_PREDEFINED        (0x01U)
#define XCP_DAQ_LIST_PROP_EVENT_FIXED       (0x02U)
#define XCP_DAQ_LIST_PROP_DAQ               (0x04U)
#define XCP_DAQ_LIST_PROP_STIM              (0x08U)

#define XCP_EVENT_PROP_CONSISTENCY_SHIFT    (6U)

#define XCP_TS_MODE_FIXED                   (0x08U)
#define XCP_TS_MODE_UNIT_SHIFT              (4U)

#define XCP_BIT_OFFSET_MAX                  (0x1FU)
#define XCP_BIT_OFFSET_NONE                 (0xFFU)

#define XCP_PAGE_PROP_ECU_ACCESS_WITHOUT_XCP     (0x01U)
#define XCP_PAGE_PROP_ECU_ACCESS_WITH_XCP        (0x02U)
#define XCP_PAGE_PROP_XCP_READ_ACCESS_WITHOUT_ECU  (0x04U)
#define XCP_PAGE_PROP_XCP_READ_ACCESS_WITH_ECU     (0x08U)
#define XCP_PAGE_PROP_XCP_WRITE_ACCESS_WITHOUT_ECU (0x10U)
#define XCP_PAGE_PROP_XCP_WRITE_ACCESS_WITH_ECU    (0x20U)

#define XCP_DAQ_LIST_TYPE_DAQ           (0x00U)
#define XCP_DAQ_LIST_TYPE_STIM          (0x01U)
#define XCP_DAQ_LIST_TYPE_DAQ_STIM      (0x02U)

#define XCP_TIMESTAMP_NO_TIME_STAMP     (0x00U)
#define XCP_TIMESTAMP_ONE_BYTE          (0x01U)
#define XCP_TIMESTAMP_TWO_BYTE          (0x02U)
#define XCP_TIMESTAMP_FOUR_BYTE         (0x04U)

#define XCP_SID_INIT                    (0x00U)
#define XCP_SID_GET_VERSION_INFO        (0x01U)
#define XCP_SID_MAIN_FUNCTION           (0x04U)
#define XCP_SID_SET_TRANSMISSION_MODE   (0x05U)
#define XCP_SID_EVENT                   (0x06U)
#define XCP_SID_SEND_EVENT              (0x07U)
#define XCP_SID_SEND_SERVICE_REQUEST    (0x08U)
#define XCP_SID_PROCESS_DAQ             (0x09U)
#define XCP_SID_PROCESS_STIM            (0x0AU)
#define XCP_SID_TRANSMIT_DTO            (0x0BU)
#define XCP_SID_RECEIVE_STIM_DTO        (0x0CU)
#define XCP_SID_SEND_RESPONSE           (0x0DU)
#define XCP_SID_GET_TIMESTAMP           (0x0EU)
#define XCP_SID_ENQUEUE_COMMAND         (0x0FU)
#define XCP_SID_PROCESS_COMMAND         (0x10U)
#define XCP_SID_TX_CONFIRMATION         (0x40U)
#define XCP_SID_TRIGGER_TRANSMIT        (0x41U)
#define XCP_SID_RX_INDICATION           (0x42U)

#define XCP_E_UNINIT                    (0x02U)
#define XCP_E_INVALID_PDUID             (0x03U)
#define XCP_E_INIT_FAILED               (0x04U)
#define XCP_E_PARAM_POINTER             (0x12U)
#define XCP_E_INV_CONFIG                (0x15U)

#define XCP_MODULE_ID                   (212U)
#define XCP_VENDOR_ID                   (0U)

#define XCP_AR_RELEASE_MAJOR_VERSION    (4U)
#define XCP_AR_RELEASE_MINOR_VERSION    (4U)
#define XCP_AR_RELEASE_REVISION_VERSION (0U)

#define XCP_RESOURCE_CAL_PAG            (0x01U)
#define XCP_RESOURCE_DAQ                (0x04U)
#define XCP_RESOURCE_STIM               (0x08U)
#define XCP_RESOURCE_PGM                (0x10U)

#define XCP_PAGE_MODE_ECU               (0x01U)
#define XCP_PAGE_MODE_XCP               (0x02U)
#define XCP_PAGE_MODE_ALL               (0x80U)

#define XCP_PGM_MODE_ABSOLUTE           (0x01U)
#define XCP_PGM_MODE_FUNCTIONAL         (0x02U)

typedef uint8 Xcp_ResFrame;

struct Xcp_Channel;
typedef struct Xcp_Channel Xcp_Channel;

typedef sint16 (*Xcp_CmdFn)(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

typedef enum
{
    XCP_UNINIT = 0x00U,
    XCP_INIT   = 0x01U
} Xcp_StateType;

typedef enum
{
    XCP_DISCONNECTED = 0x00U,
    XCP_CONNECTED    = 0x01U
} Xcp_ConnectionStateType;

typedef enum
{
    XCP_TX_OFF = 0x00U,
    XCP_TX_ON  = 0x01U
} Xcp_TransmissionModeType;

typedef enum
{
    XCP_DAQ_STATE_FREE = 0x00U,
    XCP_DAQ_STATE_CONFIGURED,
    XCP_DAQ_STATE_RUNNING,
    XCP_DAQ_STATE_STOPPED
} Xcp_DaqStateType;

typedef struct
{
    uint32 address;
    uint8  address_extension;
    uint8  size;
    uint8  bit_offset;
} Xcp_OdtEntryType;

typedef struct
{
    Xcp_OdtEntryType *odt_entry;
    uint8             odt_entry_count;
    uint8             odt_entry_max_count;
    uint8             first_pid;
} Xcp_OdtType;

typedef struct
{
    Xcp_OdtType     *odt;
    uint8            odt_count;
    uint8            odt_max_count;
    uint8            mode;
    uint8            direction;
    uint16           event_channel;
    uint8            prescaler;
    uint8            priority;
    uint8            state;
    uint8            selected;
    uint8            event_fixed;
    uint8            resume;
    uint32           prescaler_counter;
    uint8            resume_pending;
} Xcp_DaqListType;

typedef struct
{
    uint16 event_channel_number;
    uint8  event_channel_priority;
    uint8  max_daq_list;
    uint8  time_cycle;
    uint8  time_unit;
    uint8  event_channel_type;
    uint8  consistency;
} Xcp_EventChannelType;

#define XCP_EVENT_CHANNEL_TYPE_DAQ      (0x04U)
#define XCP_EVENT_CHANNEL_TYPE_STIM     (0x08U)
#define XCP_EVENT_CHANNEL_TYPE_DAQ_STIM (0x0CU)

#define XCP_EVT_CONSISTENCY_ODT         (0x00U)
#define XCP_EVT_CONSISTENCY_DAQ         (0x01U)
#define XCP_EVT_CONSISTENCY_EVENT       (0x02U)

typedef struct
{
    uint16 daq_list_number;
    uint8  event_channel_number;
    uint8  direction;
    uint8  odt_count;
    uint8  odt_entry_count;
    uint8  event_fixed;
    uint8  write_protected;
    const Xcp_OdtEntryType *entries;
} Xcp_StaticDaqListType;

typedef struct
{
    uint32 start_address;
    uint32 length;
    uint8  segment_number;
    uint8  num_pages;
} Xcp_SegmentConfigType;

typedef struct
{
    uint8  active_page_ecu;
    uint8  active_page_xcp;
} Xcp_SegmentType;

typedef struct
{
    uint32 start_address;
    uint32 length;
    uint8  sector_number;
    uint8  clear_sequence_number;
    uint8  program_sequence_number;
    uint8  program_method;
} Xcp_FlashSectorType;

typedef struct
{

    const Xcp_EventChannelType    *xcp_event_channel;
    const Xcp_StaticDaqListType   *xcp_static_daq_list;
    const Xcp_SegmentConfigType   *xcp_segment_config;
    const Xcp_FlashSectorType     *xcp_flash_sector_config;

    PduIdType xcp_rx_pdu_id;
    PduIdType xcp_tx_pdu_id;

    uint16 xcp_fr_lpdu_handle;
    uint16 xcp_fr_frame_id;
    uint8  xcp_fr_channel;
    uint8  xcp_fr_cycle_rep;
    uint16 xcp_fr_offset;
    uint16 xcp_fr_config_idx;
} Xcp_ConfigType;

#ifdef __cplusplus
}
#endif

#ifndef NULL_PTR
#define NULL_PTR                        ((void *)0)
#endif

#ifndef FALSE
#define FALSE                           (0U)
#endif

#ifndef TRUE
#define TRUE                            (1U)
#endif

#endif

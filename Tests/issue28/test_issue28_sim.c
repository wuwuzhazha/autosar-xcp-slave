/* Issue #28 deterministic simulation: one DAQ list, 11 ODTs, absolute PID,
   one shared CAN FD TX mailbox (298 us per frame), event task and main-function
   task driven by an explicit microsecond clock.

   The lower layer is modelled here (this file owns CanIf_Transmit):
     L1  mailbox busy  -> E_NOT_OK, the frame is rejected and never reaches the bus
     L2  mailbox busy  -> one buffer slot, a newer frame overwrites the older one,
                          E_OK is returned for both
   A frame "reaches the bus" when the model starts it; only then does it get a
   completion event, and only at that completion is TxConfirmation delivered.
   On completion the buffered frame is committed to the controller first and the
   confirmation is delivered second.

   Every ODT entry reads one of g_daq_var[0..10]; the event driver stamps the
   generation cycle into all eleven before firing the event, so every DTO payload
   carries the cycle that produced it and the checker never has to guess where a
   burst begins or ends.

   Usage: test_issue28_sim <S1..S9>                                              */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "StandardTypes.h"
#include "ComStack_Types.h"
#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Callback.h"
#include "test_stubs.h"

#define SIM_FRAME_US          (298U)
#define SIM_TOTAL_EVENTS      (120000U)
#define SIM_ODT_COUNT         (11U)
#define SIM_LAST_DTO_PID      (SIM_ODT_COUNT - 1U)
#define SIM_CTO_PERIOD_US     (20000U)
#define SIM_BLOCK_PERIOD_US   (2000000U)
#define SIM_BLOCK_BYTES       (256U)
#define SIM_SETUP_STEPS       (20000U)
#define SIM_INJECT_RETRY_MAX  (8U)
#define SIM_BUFFERED_TRACE    (16U)

#define SIM_LOWER_L1          (0)
#define SIM_LOWER_L2          (1)

#define SIM_FAULT_NONE        (0)
#define SIM_FAULT_DROP        (1)
#define SIM_FAULT_DELAY       (2)
#define SIM_FAULT_DROP_FRAME  (3)
#define SIM_FAULT_REJECT      (4)
#define SIM_FAULT_CONF_ERR    (5)

#define SIM_CONF_NEVER        (0xFFU)
#define SIM_TIME_NONE         (0xFFFFFFFFUL)

typedef struct
{
    const char *id;
    int         lower;
    int         cto_inject;
    int         fault_kind;
    uint32      fault_at_us;
    uint32      fault_delay_us;
    int         block_inject;
    int         commit_first;     /* 1: the buffered frame is committed before the
                                     confirmation is delivered; 0: the confirmation
                                     runs first and may take the mailbox back */
    uint32      event_period_us;
    uint32      main_period_us;
} SimScenario;

static const SimScenario g_scenarios[] =
{
    { "S1",  SIM_LOWER_L1, 0, SIM_FAULT_NONE,  0U,        0U,       0, 1, 5000U, 5000U },
    { "S2",  SIM_LOWER_L1, 1, SIM_FAULT_NONE,  0U,        0U,       0, 1, 5000U, 5000U },
    { "S3",  SIM_LOWER_L1, 0, SIM_FAULT_DROP,  60000000U, 0U,       0, 1, 5000U, 5000U },
    { "S4",  SIM_LOWER_L2, 0, SIM_FAULT_NONE,  0U,        0U,       0, 1, 5000U, 5000U },
    { "S5",  SIM_LOWER_L2, 1, SIM_FAULT_NONE,  0U,        0U,       0, 1, 5000U, 5000U },
    { "S6",  SIM_LOWER_L2, 0, SIM_FAULT_DROP,  60000000U, 0U,       0, 1, 5000U, 5000U },
    { "S7",  SIM_LOWER_L2, 0, SIM_FAULT_DELAY, 60000000U, 1200000U, 0, 1, 5000U, 5000U },
    { "S8",  SIM_LOWER_L2, 1, SIM_FAULT_NONE,  0U,        0U,       0, 1, 5000U, 5001U },
    { "S9",  SIM_LOWER_L2, 0, SIM_FAULT_NONE,  0U,        0U,       1, 1, 5000U, 5000U },
    { "S6b", SIM_LOWER_L2, 0, SIM_FAULT_DROP,  60000000U, 0U,       0, 0, 5000U, 5000U },
    { "S7b", SIM_LOWER_L2, 0, SIM_FAULT_DELAY, 60000000U, 1200000U, 0, 0, 5000U, 5000U },
    { "S10", SIM_LOWER_L2, 0, SIM_FAULT_DROP_FRAME, 60000000U, 0U,  0, 1, 5000U, 5000U },
    { "S11", SIM_LOWER_L2, 0, SIM_FAULT_NONE,  0U,        0U,       0, 1, 3000U, 5000U },
    { "S12", SIM_LOWER_L1, 0, SIM_FAULT_REJECT, 0U,       0U,       0, 1, 5000U, 5000U },
    { "S13", SIM_LOWER_L1, 0, SIM_FAULT_CONF_ERR, 0U,     0U,       0, 1, 5000U, 5000U },
};

typedef struct
{
    uint32 t_us;
    uint32 cycle;
    uint8  pid;
    uint8  is_dto;
    uint8  tx_state;
    uint8  result;
    uint8  on_bus;
    uint8  buffered;
    uint8  overwritten;
    uint8  conf_state;
    uint8  dropped;
    uint8  payload[8];
} SimFrame;

static SimFrame *g_frames;
static size_t    g_frame_count;
static size_t    g_frame_cap;

static const SimScenario *g_sc;

static uint32 g_now_us;
static uint32 g_event_index;
static uint32 g_main_index;
static uint32 g_cto_index;
static uint32 g_block_index;

static uint32 g_inject_at_us = SIM_TIME_NONE;
static uint32 g_inject_retry;
static uint32 g_inject_step;

static struct
{
    int    active;
    uint32 end_us;
    size_t rec;
} g_wire;

static struct
{
    int    full;
    size_t rec;
} g_slot;

static struct
{
    int    pending;
    uint32 at_us;
    size_t rec;
} g_late;

static struct
{
    int    fired;
    int    kind;
    uint32 at_us;
    uint32 delay_us;
} g_fault;

static uint32 g_stat_rejected;
static uint32 g_stat_buffered;
static uint32 g_stat_overwritten;
static uint32 g_stat_lost_conf;
static uint32 g_stat_dropped_frames;
static uint32 g_stat_conf_idle;
static uint32 g_stat_max_queued;
static uint32 g_stat_max_queue_count;
static uint32 g_stat_cto_injected;
static uint32 g_stat_cto_skipped;
static uint32 g_stat_block_uploads;
static uint32 g_stat_conf_error;
static size_t g_buffered_rec[SIM_BUFFERED_TRACE];

/* A fixed LCG stands in for a mailbox the rest of the ECU also uses: the same
   scenario must reject the same frames on every host and on every run. */
static uint32 g_rng_state = 0x2545F491UL;

static int sim_rand_hit(uint32 per_10000)
{
    g_rng_state = (g_rng_state * 1103515245UL) + 12345UL;

    return (((g_rng_state >> 16U) % 10000U) < per_10000) ? 1 : 0;
}

static uint32 g_daq_var[SIM_ODT_COUNT];
static uint8  g_block_src[SIM_BLOCK_BYTES];

static void sim_fail(const char *what)
{
    (void)printf("SIM-ERROR %s\n", what);
    (void)fflush(stdout);
    exit(2);
}

static size_t sim_record(uint8 pid, uint8 is_dto, uint8 tx_state)
{
    SimFrame *rec;

    if (g_frame_count == g_frame_cap)
    {
        size_t    cap = (g_frame_cap == 0U) ? 4096U : (g_frame_cap * 2U);
        SimFrame *grown = (SimFrame *)realloc(g_frames, cap * sizeof(SimFrame));

        if (grown == NULL)
        {
            sim_fail("frame log growth failed");
        }
        g_frames    = grown;
        g_frame_cap = cap;
    }

    rec = &g_frames[g_frame_count];
    (void)memset(rec, 0, sizeof(*rec));
    rec->t_us       = g_now_us;
    rec->pid        = pid;
    rec->is_dto     = is_dto;
    rec->tx_state   = tx_state;
    rec->result     = (uint8)E_OK;
    rec->conf_state = SIM_CONF_NEVER;

    return g_frame_count++;
}

static uint32 payload_cycle(const SimFrame *f)
{
    const uint8 *body = &f->payload[1];

    return (uint32)body[0] | ((uint32)body[1] << 8U)
           | ((uint32)body[2] << 16U) | ((uint32)body[3] << 24U);
}

static void wire_start(size_t rec)
{
    g_frames[rec].t_us   = g_now_us;
    g_frames[rec].cycle  = (g_frames[rec].is_dto != 0U) ? payload_cycle(&g_frames[rec]) : 0U;
    g_frames[rec].on_bus = 1U;
    g_wire.active        = 1;
    g_wire.end_us        = g_now_us + SIM_FRAME_US;
    g_wire.rec           = rec;
}

Std_ReturnType CanIf_Transmit(PduIdType tx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    uint8  pid    = pdu_info_ptr->sdu_data_ptr[0];
    uint8  is_dto = (pid <= SIM_LAST_DTO_PID) ? 1U : 0U;
    uint8  state  = (uint8)g_xcp_channel.runtime_ptr->tx.state;
    size_t rec;

    (void)tx_pdu_id;

    rec = sim_record(pid, is_dto, state);
    (void)memcpy(g_frames[rec].payload, pdu_info_ptr->sdu_data_ptr,
                 (pdu_info_ptr->sdu_length > 8U) ? 8U : pdu_info_ptr->sdu_length);

    if ((g_fault.fired == 0) && (g_fault.kind == SIM_FAULT_DROP_FRAME)
        && (g_now_us >= g_fault.at_us))
    {
        g_fault.fired             = 1;
        g_frames[rec].dropped     = 1U;
        g_stat_dropped_frames++;
        return E_OK;
    }

    /* Another PDU may already own the mailbox when the queued frame is handed
       down, so the refusal can hit any transmit call, free mailbox or not. */
    if ((g_fault.kind == SIM_FAULT_REJECT) && (sim_rand_hit(100U) != 0))
    {
        g_frames[rec].result = (uint8)E_NOT_OK;
        g_stat_rejected++;
        return E_NOT_OK;
    }

    if (g_wire.active == 0)
    {
        wire_start(rec);
        return E_OK;
    }

    if (g_sc->lower == SIM_LOWER_L1)
    {
        g_frames[rec].result = (uint8)E_NOT_OK;
        g_stat_rejected++;
        return E_NOT_OK;
    }

    if (g_slot.full != 0)
    {
        g_frames[g_slot.rec].overwritten = 1U;
        g_stat_overwritten++;
    }
    else if (g_stat_buffered < SIM_BUFFERED_TRACE)
    {
        g_buffered_rec[g_stat_buffered] = rec;
    }

    g_slot.full    = 1;
    g_slot.rec     = rec;
    g_frames[rec].buffered = 1U;
    g_stat_buffered++;

    return E_OK;
}

static void slot_commit(void)
{
    size_t rec;

    if ((g_wire.active != 0) || (g_slot.full == 0))
    {
        return;
    }

    g_slot.full = 0;
    rec         = g_slot.rec;
    wire_start(rec);
}

static void confirm_deliver(size_t rec, Std_ReturnType result)
{
    uint8 state = (uint8)g_xcp_channel.runtime_ptr->tx.state;

    g_frames[rec].conf_state = state;

    if (state == (uint8)XCP_TX_IDLE)
    {
        g_stat_conf_idle++;
    }

    Xcp_CanIfTxConfirmation(g_xcp_channel.config_ptr->xcp_tx_pdu_id, result);
}

static void wire_finish(void)
{
    size_t rec    = g_wire.rec;
    int    action = 0;

    g_wire.active = 0;

    if (g_sc->commit_first != 0)
    {
        slot_commit();
    }

    if ((g_fault.fired == 0) && (g_fault.kind != SIM_FAULT_NONE)
        && (g_now_us >= g_fault.at_us))
    {
        g_fault.fired = 1;
        action = (g_fault.kind == SIM_FAULT_DROP) ? 1 : 2;
    }

    if (action == 0)
    {
        if ((g_fault.kind == SIM_FAULT_CONF_ERR) && (sim_rand_hit(10U) != 0))
        {
            /* The controller took the frame and then lost it: the completion
               reports the error, and nothing of that frame ever saw the bus. */
            g_frames[rec].on_bus = 0U;
            g_stat_conf_error++;
            confirm_deliver(rec, E_NOT_OK);
        }
        else
        {
            confirm_deliver(rec, E_OK);
        }
    }
    else if (action == 1)
    {
        g_frames[rec].conf_state = SIM_CONF_NEVER;
        g_stat_lost_conf++;
    }
    else
    {
        g_late.pending = 1;
        g_late.at_us   = g_now_us + g_fault.delay_us;
        g_late.rec     = rec;
    }

    if (g_sc->commit_first == 0)
    {
        slot_commit();
    }
}

static void late_deliver(void)
{
    size_t rec = g_late.rec;

    g_late.pending = 0;
    confirm_deliver(rec, E_OK);
}

static boolean sim_quiescent(void)
{
    return ((g_wire.active == 0) && (g_slot.full == 0) && (g_late.pending == 0)
            && (g_xcp_channel.runtime_ptr->tx.state == XCP_TX_IDLE)
            && (g_xcp_channel.runtime_ptr->tx.cto_count == 0U)
            && (g_xcp_channel.runtime_ptr->cmd_pending == FALSE)
            && (g_xcp_channel.runtime_ptr->block_transfer_active == FALSE)
            && (g_xcp_channel.storage_ptr->dto_queue_count == 0U)) ? TRUE : FALSE;
}

static void sim_inject(const uint8 *cmd, uint8 len)
{
    PduInfoType info;

    info.sdu_data_ptr  = (uint8 *)cmd;
    info.sdu_length    = len;
    info.meta_data_ptr = NULL_PTR;

    Xcp_CanIfRxIndication(g_xcp_channel.config_ptr->xcp_rx_pdu_id, &info);
}

static void sim_pump_setup(void)
{
    uint32 step;

    for (step = 0U; step < SIM_SETUP_STEPS; step++)
    {
        if (sim_quiescent() != FALSE)
        {
            return;
        }

        if (g_wire.active != 0)
        {
            g_now_us = g_wire.end_us;
            wire_finish();
        }
        else if (g_late.pending != 0)
        {
            g_now_us = g_late.at_us;
            late_deliver();
        }
        else
        {
            g_now_us += (g_sc != NULL) ? g_sc->main_period_us : 5000U;
            Xcp_MainFunction();
        }
    }

    sim_fail("setup did not settle");
}

static size_t g_last_response = (size_t)-1;

static void sim_cmd(const uint8 *cmd, uint8 len)
{
    size_t i;
    size_t last = (size_t)-1;

    sim_inject(cmd, len);
    sim_pump_setup();

    for (i = g_frame_count; i > 0U; i--)
    {
        if (g_frames[i - 1U].is_dto == 0U)
        {
            last = i - 1U;
            break;
        }
    }

    g_last_response = last;

    if ((last == (size_t)-1) || (g_frames[last].pid != XCP_PID_RES))
    {
        (void)printf("command %02X %02X failed: pid=%02X code=%02X\n",
                     (unsigned)cmd[0], (unsigned)((len > 1U) ? cmd[1] : 0U),
                     (unsigned)((last == (size_t)-1) ? 0xEEU : g_frames[last].pid),
                     (unsigned)((last == (size_t)-1) ? 0xEEU : g_frames[last].payload[1]));
        sim_fail("command rejected during setup");
    }
}

static void sim_inject_connect(void)
{
    static const uint8 connect_cmd[2] = { 0xFFU, 0x00U };

    sim_cmd(connect_cmd, 2U);

    if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
    {
        sim_fail("CONNECT did not open the session");
    }
}

static void sim_unlock(uint8 resource)
{
    uint8 cmd[8];
    uint8 seed[4];
    uint8 i;

    cmd[0] = 0xF8U; cmd[1] = 0x00U; cmd[2] = resource;
    sim_cmd(cmd, 3U);

    for (i = 0U; i < 4U; i++)
    {
        seed[i] = g_frames[g_last_response].payload[2U + i];
    }

    cmd[0] = 0xF7U; cmd[1] = 0x04U;
    for (i = 0U; i < 4U; i++)
    {
        cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU);
    }
    sim_cmd(cmd, 6U);
}

static void sim_setup_daq(void)
{
    static const uint8 cmd_free_daq[1]  = { 0xD6U };
    static const uint8 cmd_alloc_daq[4] = { 0xD5U, 0x00U, 0x01U, 0x00U };
    static const uint8 cmd_alloc_odt[5] = { 0xD4U, 0x00U, 0x00U, 0x00U, SIM_ODT_COUNT };
    uint8  cmd[16];
    uint32 odt;

    sim_cmd(cmd_free_daq, 1U);
    sim_cmd(cmd_alloc_daq, 4U);
    sim_cmd(cmd_alloc_odt, 5U);

    for (odt = 0U; odt < SIM_ODT_COUNT; odt++)
    {
        uint32 addr = TEST_ADDR(g_daq_var[odt]);

        cmd[0] = 0xD3U; cmd[1] = 0x00U; cmd[2] = 0x00U;
        cmd[3] = 0x00U; cmd[4] = (uint8)odt; cmd[5] = 0x01U;
        sim_cmd(cmd, 6U);

        cmd[0] = 0xE2U; cmd[1] = 0x00U; cmd[2] = 0x00U;
        cmd[3] = 0x00U; cmd[4] = (uint8)odt; cmd[5] = 0x00U;
        sim_cmd(cmd, 6U);

        cmd[0] = 0xE1U; cmd[1] = 0xFFU; cmd[2] = 0x04U; cmd[3] = 0x00U;
        cmd[4] = (uint8)(addr & 0xFFU);
        cmd[5] = (uint8)((addr >> 8U) & 0xFFU);
        cmd[6] = (uint8)((addr >> 16U) & 0xFFU);
        cmd[7] = (uint8)((addr >> 24U) & 0xFFU);
        sim_cmd(cmd, 8U);
    }

    cmd[0] = 0xE0U; cmd[1] = 0x00U; cmd[2] = 0x00U; cmd[3] = 0x00U;
    cmd[4] = 0x00U; cmd[5] = 0x00U; cmd[6] = 0x01U; cmd[7] = 0x00U;
    sim_cmd(cmd, 8U);

    cmd[0] = 0xDEU; cmd[1] = 0x01U; cmd[2] = 0x00U; cmd[3] = 0x00U;
    sim_cmd(cmd, 4U);

    if (g_xcp_channel.storage_ptr->daq_list[0].state != (uint8)XCP_DAQ_STATE_RUNNING)
    {
        sim_fail("DAQ list 0 is not running");
    }
    if (g_xcp_channel.storage_ptr->daq_list[0].odt_count != (uint8)SIM_ODT_COUNT)
    {
        sim_fail("DAQ list 0 does not carry 11 ODTs");
    }
}

static void sim_fire_event(uint32 cycle)
{
    uint32 i;

    for (i = 0U; i < SIM_ODT_COUNT; i++)
    {
        g_daq_var[i] = cycle;
    }

    Xcp_Event(0U);
}

static void sim_observe_load(void)
{
    uint32 queued  = (uint32)g_xcp_channel.storage_ptr->dto_queued[0];
    uint32 q_count = (uint32)g_xcp_channel.storage_ptr->dto_queue_count;

    if (queued > g_stat_max_queued)
    {
        g_stat_max_queued = queued;
    }
    if (q_count > g_stat_max_queue_count)
    {
        g_stat_max_queue_count = q_count;
    }
}

static void sim_inject_get_status(void)
{
    static const uint8 get_status[1] = { 0xFDU };

    if (g_xcp_channel.runtime_ptr->cmd_pending != FALSE)
    {
        g_stat_cto_skipped++;
        return;
    }

    sim_inject(get_status, 1U);
    g_stat_cto_injected++;
}

static void sim_inject_set_mta(void)
{
    uint8  cmd[8];
    uint32 addr = TEST_ADDR(g_block_src);

    cmd[0] = 0xF6U; cmd[1] = 0x00U; cmd[2] = 0x00U; cmd[3] = 0x00U;
    cmd[4] = (uint8)(addr & 0xFFU);
    cmd[5] = (uint8)((addr >> 8U) & 0xFFU);
    cmd[6] = (uint8)((addr >> 16U) & 0xFFU);
    cmd[7] = (uint8)((addr >> 24U) & 0xFFU);

    sim_inject(cmd, 8U);
}

static void sim_inject_block_upload(void)
{
    uint8 cmd[2];

    cmd[0] = 0xF5U;
    cmd[1] = 0xC8U;

    sim_inject(cmd, 2U);
    g_stat_block_uploads++;
}

static void sim_inject_step_run(uint32 t)
{
    if (g_xcp_channel.runtime_ptr->cmd_pending != FALSE)
    {
        if (g_inject_retry >= SIM_INJECT_RETRY_MAX)
        {
            sim_fail("block injection blocked by a pending command");
        }
        g_inject_retry++;
        g_inject_at_us = t + 1000U;
        return;
    }

    g_inject_retry = 0U;

    if (g_inject_step == 0U)
    {
        sim_inject_set_mta();
        g_inject_step   = 1U;
        g_inject_at_us  = t + 1000U;
    }
    else
    {
        sim_inject_block_upload();
        g_inject_step  = 0U;
        g_inject_at_us = SIM_TIME_NONE;
    }
}

static void sim_run(void)
{
    uint32 horizon = (uint32)SIM_TOTAL_EVENTS * g_sc->event_period_us;

    for (;;)
    {
        uint32 t       = SIM_TIME_NONE;
        uint32 t_event = (uint32)(g_event_index * g_sc->event_period_us);
        uint32 t_main  = (uint32)(g_main_index * g_sc->main_period_us);
        uint32 t_cto   = (uint32)(g_cto_index * SIM_CTO_PERIOD_US);
        uint32 t_block = (uint32)(g_block_index * SIM_BLOCK_PERIOD_US);

        if (g_event_index >= SIM_TOTAL_EVENTS)
        {
            t_event = SIM_TIME_NONE;
        }
        if (t_main > horizon)
        {
            t_main = SIM_TIME_NONE;
        }
        if ((g_sc->cto_inject == 0) || (t_cto > horizon))
        {
            t_cto = SIM_TIME_NONE;
        }
        if ((g_sc->block_inject == 0) || (t_block > horizon))
        {
            t_block = SIM_TIME_NONE;
        }

        if (g_wire.active != 0)
        {
            t = (g_wire.end_us < t) ? g_wire.end_us : t;
        }
        if (g_late.pending != 0)
        {
            t = (g_late.at_us < t) ? g_late.at_us : t;
        }
        t = (t_event < t) ? t_event : t;
        t = (t_main < t) ? t_main : t;
        t = (t_cto < t) ? t_cto : t;
        t = (t_block < t) ? t_block : t;
        t = (g_inject_at_us < t) ? g_inject_at_us : t;

        if ((t == SIM_TIME_NONE) || (t > horizon))
        {
            break;
        }

        g_now_us = t;

        if ((g_wire.active != 0) && (g_wire.end_us == t))
        {
            wire_finish();
        }

        if ((g_late.pending != 0) && (g_late.at_us == t))
        {
            late_deliver();
        }

        if ((t_block == t) && (t_block != SIM_TIME_NONE))
        {
            g_inject_at_us = t;
            g_inject_step  = 0U;
            g_inject_retry = 0U;
            g_block_index++;
        }

        if ((g_inject_at_us == t) && (g_inject_at_us != SIM_TIME_NONE))
        {
            sim_inject_step_run(t);
        }

        if ((t_cto == t) && (t_cto != SIM_TIME_NONE))
        {
            sim_inject_get_status();
            g_cto_index++;
        }

        if ((t_main == t) && (t_main != SIM_TIME_NONE))
        {
            Xcp_MainFunction();
            g_main_index++;
            sim_observe_load();
        }

        if ((t_event == t) && (t_event != SIM_TIME_NONE))
        {
            sim_fire_event(g_event_index);
            g_event_index++;
            sim_observe_load();
        }
    }
}

typedef struct
{
    uint32 ok;
    uint32 skip;
    uint32 missing_partial;
    uint32 duplicate;
    uint32 out_of_order;
    uint32 cycles_with_frames;
    uint32 late_frames;
    uint32 first_late_us;
    size_t first_late_rec;
    uint32 first_violation_us;
    uint32 first_violation_cycle;
    const char *first_violation_kind;
    size_t first_violation_rec;
    uint32 max_gap_us;
    uint32 max_gap_at_us;
} SimVerdict;

static int frame_on_bus(const SimFrame *f)
{
    return ((f->on_bus != 0U) && (f->is_dto != 0U)) ? 1 : 0;
}

static SimVerdict sim_check(void)
{
    SimVerdict v;
    size_t     i          = 0U;
    uint32     last_cycle = 0U;
    int        have_last  = 0;

    (void)memset(&v, 0, sizeof(v));
    v.first_violation_rec = (size_t)-1;
    v.first_late_rec      = (size_t)-1;
    v.first_late_us       = SIM_TIME_NONE;

    while (i < g_frame_count)
    {
        uint32 cycle;
        uint32 count    = 0U;
        uint16 mask     = 0U;
        int    ordered  = 1;
        int    dup      = 0;
        int    last_pid = -1;
        uint32 window_end;
        size_t j;
        size_t first_rec;

        while ((i < g_frame_count) && (frame_on_bus(&g_frames[i]) == 0))
        {
            i++;
        }
        if (i >= g_frame_count)
        {
            break;
        }

        first_rec  = i;
        cycle      = g_frames[i].cycle;
        window_end = (cycle + 1U) * g_sc->event_period_us;

        for (j = i; j < g_frame_count; j++)
        {
            uint16 bit;

            if (frame_on_bus(&g_frames[j]) == 0)
            {
                continue;
            }
            if (g_frames[j].cycle != cycle)
            {
                break;
            }

            bit = (uint16)(1U << g_frames[j].pid);
            if ((mask & bit) != 0U)
            {
                dup = 1;
            }
            mask |= bit;
            if ((int)g_frames[j].pid <= last_pid)
            {
                ordered = 0;
            }
            last_pid = (int)g_frames[j].pid;
            count++;

            if (g_frames[j].t_us >= window_end)
            {
                if (v.first_late_rec == (size_t)-1)
                {
                    v.first_late_rec = j;
                    v.first_late_us  = g_frames[j].t_us;
                }
                v.late_frames++;
            }
        }
        i = j;

        if (have_last == 0)
        {
            v.cycles_with_frames++;
        }
        else if (cycle < last_cycle)
        {
            v.out_of_order++;
        }
        else if (cycle == last_cycle)
        {
            v.duplicate++;
        }
        else
        {
            v.cycles_with_frames++;
        }
        have_last  = 1;
        last_cycle = cycle;

        if (dup != 0)
        {
            v.duplicate++;
        }
        if ((ordered == 0) && (dup == 0))
        {
            v.out_of_order++;
        }
        if ((dup == 0) && (ordered != 0) && (count < SIM_ODT_COUNT))
        {
            v.missing_partial++;
        }

        if ((dup == 0) && (ordered != 0) && (count == SIM_ODT_COUNT))
        {
            v.ok++;
        }
        else if (v.first_violation_rec == (size_t)-1)
        {
            v.first_violation_rec   = first_rec;
            v.first_violation_us    = g_frames[first_rec].t_us;
            v.first_violation_cycle = cycle;
            v.first_violation_kind  = (dup != 0) ? "duplicate"
                                                 : ((ordered == 0) ? "out_of_order" : "partial");
        }
    }

    v.skip = SIM_TOTAL_EVENTS - v.cycles_with_frames;

    /* The longest silence on the bus between two DTOs: a slave that stops
       handing frames down shows up here before it shows up in the counters.
       Records are logged in call order and an L2-buffered frame is stamped
       when it finally starts, so only strictly later starts extend the line. */
    {
        uint32 prev = SIM_TIME_NONE;
        size_t k;

        for (k = 0U; k < g_frame_count; k++)
        {
            uint32 t;

            if (frame_on_bus(&g_frames[k]) == 0)
            {
                continue;
            }
            t = g_frames[k].t_us;
            if (prev == SIM_TIME_NONE)
            {
                prev = t;
            }
            else if (t > prev)
            {
                if ((t - prev) > v.max_gap_us)
                {
                    v.max_gap_us    = t - prev;
                    v.max_gap_at_us = t;
                }
                prev = t;
            }
        }
    }

    return v;
}

static const char *tx_state_name(uint8 state)
{
    if (state == (uint8)XCP_TX_IDLE)
    {
        return "IDLE";
    }
    return (state == (uint8)XCP_TX_DTO) ? "DTO" : "CTO";
}

static void sim_print_frame_line(size_t idx, size_t mark, const char *note)
{
    const SimFrame *f    = &g_frames[idx];
    char            conf[16];

    if (f->conf_state == SIM_CONF_NEVER)
    {
        (void)snprintf(conf, sizeof(conf), "NEVER");
    }
    else
    {
        (void)snprintf(conf, sizeof(conf), "%s", tx_state_name(f->conf_state));
    }

    (void)printf("  %7u %10u %6u %3u %4s %7s %5s %5s %4s %3s %-6s%s%s\n",
                 (unsigned)idx, (unsigned)f->t_us, (unsigned)f->cycle,
                 (unsigned)f->pid, (f->is_dto != 0U) ? "dto" : "cto",
                 tx_state_name(f->tx_state),
                 (f->on_bus != 0U) ? "yes" : "no",
                 (f->buffered != 0U) ? "yes" : "no",
                 (f->overwritten != 0U) ? "yes" : "no",
                 (f->dropped != 0U) ? "DROP" : ((f->result == (uint8)E_OK) ? "OK" : "NOK"),
                 conf, (idx == mark) ? "  <== " : "", note);
}

static void sim_print_trace(const char *label, size_t center, const SimVerdict *v)
{
    size_t from;
    size_t to;
    size_t k;

    if (center == (size_t)-1)
    {
        return;
    }

    from = (center > 10U) ? (center - 10U) : 0U;
    to   = center + 10U;
    if (to >= g_frame_count)
    {
        to = g_frame_count - 1U;
    }

    (void)printf("  %s (cycle %u at %u us)\n", label,
                 (unsigned)g_frames[center].cycle, (unsigned)g_frames[center].t_us);
    (void)printf("  %7s %10s %6s %3s %4s %7s %5s %5s %4s %3s %-6s\n",
                 "idx", "t_us", "cycle", "pid", "kind", "state", "onbus", "bufrd",
                 "ovwr", "res", "conf");
    for (k = from; k <= to; k++)
    {
        sim_print_frame_line(k, center, (k == center) ? "window edge" : "");
    }
    (void)v;
}

static const char *fault_name(int kind)
{
    switch (kind)
    {
    case SIM_FAULT_DROP:       return "drop_conf@60s";
    case SIM_FAULT_DELAY:      return "late_conf+1.2s@60s";
    case SIM_FAULT_DROP_FRAME: return "drop_frame@60s";
    case SIM_FAULT_REJECT:     return "reject_1pct";
    case SIM_FAULT_CONF_ERR:   return "conf_err_0.1pct";
    default:                   return "none";
    }
}

static int sim_id_equal(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0'))
    {
        char ca = (*a >= 'a') ? (char)(*a - 'a' + 'A') : *a;
        char cb = (*b >= 'a') ? (char)(*b - 'a' + 'A') : *b;

        if (ca != cb)
        {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b) ? 1 : 0;
}

int main(int argc, char **argv)
{
    SimVerdict         verdict;
    const SimScenario *picked = NULL;
    size_t             s;

    if (argc != 2)
    {
        (void)printf("usage: %s <S1..S9>\n", argv[0]);
        return 2;
    }

    for (s = 0U; s < (sizeof(g_scenarios) / sizeof(g_scenarios[0])); s++)
    {
        if (sim_id_equal(argv[1], g_scenarios[s].id) != 0)
        {
            picked = &g_scenarios[s];
        }
    }
    if (picked == NULL)
    {
        (void)printf("unknown scenario %s\n", argv[1]);
        return 2;
    }

    g_sc = picked;

    Xcp_Init(NULL_PTR);
    sim_inject_connect();
    sim_unlock(XCP_RESOURCE_DAQ);
    sim_setup_daq();

    g_now_us         = 0U;
    g_inject_at_us   = SIM_TIME_NONE;
    g_fault.kind     = g_sc->fault_kind;
    g_fault.at_us    = g_sc->fault_at_us;
    g_fault.delay_us = g_sc->fault_delay_us;

    sim_run();

    verdict = sim_check();

    (void)printf("SCENARIO %s lower=%s cto_every_20ms=%d fault=%s order=%s event=%uus main=%uus\n",
                 g_sc->id, (g_sc->lower == SIM_LOWER_L1) ? "L1" : "L2",
                 g_sc->cto_inject, fault_name(g_sc->fault_kind),
                 (g_sc->commit_first != 0) ? "commit-first" : "confirm-first",
                 (unsigned)g_sc->event_period_us, (unsigned)g_sc->main_period_us);
    (void)printf("RESULT id=%s skip=%u missing_partial=%u duplicate=%u out_of_order=%u ok=%u\n",
                 g_sc->id, (unsigned)verdict.skip, (unsigned)verdict.missing_partial,
                 (unsigned)verdict.duplicate, (unsigned)verdict.out_of_order,
                 (unsigned)verdict.ok);
    (void)printf("STALL max_gap_us=%u at_us=%u\n",
                 (unsigned)verdict.max_gap_us, (unsigned)verdict.max_gap_at_us);
    if (verdict.first_violation_rec == (size_t)-1)
    {
        (void)printf("FIRST_VIOLATION none\n");
    }
    else
    {
        (void)printf("FIRST_VIOLATION t_us=%u cycle=%u kind=%s\n",
                     (unsigned)verdict.first_violation_us,
                     (unsigned)verdict.first_violation_cycle,
                     verdict.first_violation_kind);
    }
    if (verdict.late_frames == 0U)
    {
        (void)printf("LATE cycles=0 frames=0 first_us=none\n");
    }
    else
    {
        (void)printf("LATE cycles=1 frames=%u first_us=%u\n",
                     (unsigned)verdict.late_frames, (unsigned)verdict.first_late_us);
    }
    (void)printf("STATE dto_queued[0]=%u dto_queue_count=%u max_queued=%u max_queue_count=%u\n",
                 (unsigned)g_xcp_channel.storage_ptr->dto_queued[0],
                 (unsigned)g_xcp_channel.storage_ptr->dto_queue_count,
                 (unsigned)g_stat_max_queued, (unsigned)g_stat_max_queue_count);
    (void)printf("STATS frames=%u rejected=%u buffered=%u overwritten=%u dropped_frames=%u lost_conf=%u "
                 "conf_error=%u conf_while_idle=%u cto_injected=%u cto_skipped=%u block_uploads=%u\n",
                 (unsigned)g_frame_count, (unsigned)g_stat_rejected,
                 (unsigned)g_stat_buffered, (unsigned)g_stat_overwritten,
                 (unsigned)g_stat_dropped_frames,
                 (unsigned)g_stat_lost_conf, (unsigned)g_stat_conf_error,
                 (unsigned)g_stat_conf_idle,
                 (unsigned)g_stat_cto_injected, (unsigned)g_stat_cto_skipped,
                 (unsigned)g_stat_block_uploads);

    sim_print_trace("TRACE-FIRST-VIOLATION", verdict.first_violation_rec, &verdict);
    sim_print_trace("TRACE-FIRST-LATE", verdict.first_late_rec, &verdict);

    if (g_stat_buffered != 0U)
    {
        size_t k;
        size_t shown = (g_stat_buffered < SIM_BUFFERED_TRACE) ? g_stat_buffered : SIM_BUFFERED_TRACE;

        (void)printf("  BUFFERED-FRAMES (first %u of %u)\n",
                     (unsigned)shown, (unsigned)g_stat_buffered);
        for (k = 0U; k < shown; k++)
        {
            sim_print_frame_line(g_buffered_rec[k], (size_t)-1, "buffered");
        }
    }

    free(g_frames);
    return 0;
}

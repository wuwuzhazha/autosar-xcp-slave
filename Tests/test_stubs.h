#ifndef TEST_STUBS_H
#define TEST_STUBS_H

/* Register a host object to obtain a stable 32-bit XCP address for it. Repeat
   calls for the same object return the same address. */
unsigned int test_mem_register(void *ptr, unsigned int size);

#define TEST_ADDR(obj) test_mem_register((void *)&(obj), (unsigned int)sizeof(obj))

extern uint32 test_schm_enter_count;
extern uint32 test_schm_exit_count;
extern uint32 test_schm_depth;
extern uint32 test_schm_nested_count;
extern boolean test_schm_tx_under_lock;
extern uint32 test_tx_fail_count;

/* Set to deliver Xcp_HandleTxConfirmation(E_OK) from inside CanIf_Transmit. */
extern boolean test_tx_confirm_in_send;

/* Set to queue a DTO from inside CanIf_Transmit. */
extern boolean test_tx_enqueue_in_send;

/* Set to deliver a command from inside CanIf_Transmit. */
extern boolean test_tx_rx_in_send;

#endif

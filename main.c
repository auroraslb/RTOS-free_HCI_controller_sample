#include <stdio.h>
#include <string.h>

#include "nrfx_uarte.h"

#include "mpsl.h"
#include "mpsl_timeslot.h"

#include "sdc.h"
#include "sdc_hci.h"
/* The H4 packet types. */
typedef enum
{
    H4_UART_HCI_COMMAND_PACKET = 0x01,
    H4_UART_HCI_ACL_DATA_PACKET = 0x02,
    H4_UART_HCI_SYNCHRONOUS_DATA_PACKET = 0x03,
    H4_UART_HCI_EVENT_PACKET = 0x04
} h4_uart_pkt_type_t;
/* Make UART instance and define config */
static nrfx_uarte_t uarte_instance = {.p_reg = NRF_UARTE0};

static nrfx_uarte_config_t uarte_config =
    {
        .pselrts = 5,
        .pseltxd = 6,
        .pselcts = 7,
        .pselrxd = 8,
        .p_context = NULL,
        .baudrate = NRF_UARTE_BAUDRATE_1000000,
        .interrupt_priority = 2,
        .hal_cfg = {
            .hwfc = NRF_UARTE_HWFC_ENABLED,
            .parity = NRF_UARTE_PARITY_EXCLUDED,
#if defined(UARTE_CONFIG_STOP_Msk)
            .stop = NRF_UARTE_STOP_ONE,
#endif
        },
};

/* MPSL clock config */
static mpsl_clock_lfclk_cfg_t clock_config =
    {
        .source = MPSL_CLOCK_LF_SRC_XTAL,
        .rc_ctiv = 0,
        .rc_temp_ctiv = 0,
        .accuracy_ppm = 250,
        .skip_wait_lfclk_started = false,
};

/* Fault handler */
__NO_RETURN void m_fault_handler(const char *file, const uint32_t line)
{
    nrfx_uarte_config_t *p_uarte_config;
    static uint8_t assert_event[100];

    assert_event[0] = H4_UART_HCI_EVENT_PACKET;
    assert_event[1] = 0xFF; /* Vendor specific */
    /* size */
    assert_event[3] = 0xAA; /* Vendor specific ASSERT */

    (void)snprintf((char *)&assert_event[4], sizeof(assert_event) - 4, "Line: %04d, File: %s", line, file);
    int size = strlen((const char *)&assert_event[4]);

    assert_event[2] = size + 1; /* Length of HCI packet */

    /* Re-initialize UART to be used in blocking mode. */
    nrfx_uarte_uninit(&uarte_instance);

    p_uarte_config = &uarte_config;
    (void)nrfx_uarte_init(&uarte_instance, p_uarte_config, NULL);

    (void)nrfx_uarte_tx(&uarte_instance, (const uint8_t *)assert_event, size + 4);

    while (1)
        ;
}
/* Buffer for writing to UART */
char tmp_buff[128];

int main()
{
    /* For checking the returns of the init procedures */
    int32_t retcode;

    retcode = mpsl_init(&clock_config, SWI5_IRQn, m_fault_handler);
    NRFX_ASSERT(retcode == 0);

    retcode = sdc_init(m_fault_handler);
    NRFX_ASSERT(retcode == 0);

    /* Print Hello World to UART */
    /*
    nrfx_uarte_uninit(&uarte_instance);

    (void)nrfx_uarte_init(&uarte_instance, &uarte_config, NULL);

    char *string = "Hello, World!";

    sprintf((char *)tmp_buff, "%s", string);
    nrfx_uarte_tx(&uarte_instance, (const uint8_t *)tmp_buff, strlen((const char *)string));
    */

    sprintf((char*)tmp_buff, "%s", string);
    nrfx_uarte_tx(&uarte_instance, (const uint8_t*) tmp_buff, strlen((const char*)string));

    return 0;
}

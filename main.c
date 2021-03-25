#include <stdio.h>
#include <string.h>

#include "nrfx_uarte.h"

#include "mpsl.h"
#include "mpsl_timeslot.h"

#include "sdc.h"
#include "sdc_hci.h"

/* The UART header is 1 byte */
#define H4_UART_HEADER_SIZE 1
#define M_H4_RX_BUFFER_SIZE (H4_UART_HEADER_SIZE + HCI_MSG_BUFFER_MAX_SIZE)

typedef enum
{
  STATE_RECV_H4_HEADER = 0,
  STATE_RECV_ACL_DATA_HEADER = 1,
  STATE_RECV_CMD_HEADER = 2,
  STATE_RECV_PACKET_CONTENT,
} recv_state_t;

static recv_state_t m_recv_state;

/* The H4 packet types. */
typedef enum
{
    H4_UART_HCI_COMMAND_PACKET = 0x01,
    H4_UART_HCI_ACL_DATA_PACKET = 0x02,
    H4_UART_HCI_SYNCHRONOUS_DATA_PACKET = 0x03,
    H4_UART_HCI_EVENT_PACKET = 0x04
} h4_uart_pkt_type_t;

static uint8_t m_h4_rx_buffer[M_H4_RX_BUFFER_SIZE];

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


/* Get length */
static uint16_t * m_p_to_acl_data_length_get(uint8_t const * p_h4_buf)
{
  return (uint16_t*)&p_h4_buf[H4_UART_HEADER_SIZE + 2];
}

static uint8_t * m_p_to_cmd_length_get(uint8_t const * p_h4_buf)
{
  return (uint8_t*)&p_h4_buf[H4_UART_HEADER_SIZE + 2];
}


static void m_state_recv_h4_header_enter(void)
{
  NRFX_ASSERT(nrfx_uarte_rx(&uarte_instance, &m_h4_rx_buffer[0], 1) == NRFX_SUCCESS);
}


/* Receive data or command from host to controller */
static void m_start_recv_h4_header_from_host(void)
{
    NRFX_ASSERT(nrfx_uarte_rx(&uarte_instance, &m_h4_rx_buffer[0], 1) == NRFX_SUCCESS);
}

static void m_on_packet_received_from_host(void)
{
    switch (m_h4_rx_buffer[0])
    {
    case H4_UART_HCI_ACL_DATA_PACKET:
        NRFX_ASSERT(sdc_hci_data_put(&m_h4_rx_buffer[H4_UART_HEADER_SIZE]) == 0);
        break;
    case H4_UART_HCI_COMMAND_PACKET:
        NRFX_ASSERT(sdc_hci_cmd_put(&m_h4_rx_buffer[H4_UART_HEADER_SIZE]) == 0);
        break;
    default:
        NRFX_ASSERT(false);
        break;
    }
}

static void m_continue_recv_packet_from_host(nrfx_uarte_xfer_evt_t const *p_transfer_evt)
{
    recv_state_t next_state = STATE_RECV_H4_HEADER;
    switch (m_recv_state)
    {
    case STATE_RECV_H4_HEADER:
        NRFX_ASSERT(p_transfer_evt->bytes == 1);
        switch (m_h4_rx_buffer[0])
        {
        case H4_UART_HCI_ACL_DATA_PACKET:
            next_state = STATE_RECV_ACL_DATA_HEADER;
            break;
        case H4_UART_HCI_COMMAND_PACKET:
            next_state = STATE_RECV_CMD_HEADER;
            break;
        default:
            NRFX_ASSERT(false);
            break;
        }
        break;
    case STATE_RECV_ACL_DATA_HEADER:
        if (*m_p_to_acl_data_length_get(m_h4_rx_buffer) > 0)
        {
            next_state = STATE_RECV_PACKET_CONTENT;
        }
        else
        {
            m_on_packet_received_from_host();
            next_state = STATE_RECV_H4_HEADER;
        }
        break;
    case STATE_RECV_CMD_HEADER:
        if (*m_p_to_cmd_length_get(m_h4_rx_buffer) > 0)
        {
            next_state = STATE_RECV_PACKET_CONTENT;
        }
        else
        {
            m_on_packet_received_from_host();
            next_state = STATE_RECV_H4_HEADER;
        }
        break;
    case STATE_RECV_PACKET_CONTENT:
        m_on_packet_received_from_host();
        next_state = STATE_RECV_H4_HEADER;
        break;
    default:
        NRFX_ASSERT(false);
        break;
    }

    switch (next_state)
    {
    case STATE_RECV_H4_HEADER:
        m_start_recv_h4_header_from_host();
        break;
    case STATE_RECV_ACL_DATA_HEADER:
        NRFX_ASSERT(nrfx_uart_rx(&uart_instance, &m_h4_rx_buffer[H4_UART_HEADER_SIZE], 4) == NRFX_SUCCESS);
        break;
    case STATE_RECV_CMD_HEADER:
        NRFX_ASSERT(nrfx_uart_rx(&uart_instance, &m_h4_rx_buffer[H4_UART_HEADER_SIZE], 3) == NRFX_SUCCESS);
        break;
    case STATE_RECV_PACKET_CONTENT:
        switch (m_h4_rx_buffer[0])
        {
        case H4_UART_HCI_ACL_DATA_PACKET:
            NRFX_ASSERT(nrfx_uart_rx(&uart_instance,
                                     &m_h4_rx_buffer[H4_UART_HEADER_SIZE + 4],
                                     *m_p_to_acl_data_length_get(m_h4_rx_buffer)) == NRFX_SUCCESS);
            break;
        case H4_UART_HCI_COMMAND_PACKET:
            NRFX_ASSERT(nrfx_uart_rx(&uart_instance,
                                     &m_h4_rx_buffer[H4_UART_HEADER_SIZE + 3],
                                     *m_p_to_cmd_length_get(m_h4_rx_buffer)) == NRFX_SUCCESS);
            break;
        default:
            NRFX_ASSERT(false);
            break;
        }
        break;
    }

    m_recv_state = next_state;
}


void nrfx_uarte_event_handler(nrfx_uarte_event_t const *p_event,
                              void *p_context)
{
    switch (p_event->type)
    {
//    case NRFX_UARTE_EVT_TX_DONE:
//        m_tx_buffer_available = true;
//        break;
    case NRFX_UARTE_EVT_RX_DONE:
        m_continue_recv_packet_from_host(&p_event->data.rxtx);
        break;
    case NRFX_UARTE_EVT_ERROR:
       // m_fault_handler(__MODULE__, __LINE__);
        break;
    }
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

    NRFX_ASSERT(nrfx_uarte_init(&uarte_instance, p_uarte_config, nrfx_uarte_event_handler) == NRFX_SUCCESS);
    //nrfx_uarte_rx_enable(&uarte_instance);
    m_state_recv_h4_header_enter();

    return 0;
}

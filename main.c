#include <stdio.h>
#include <string.h>

#include "nrfx_uarte.h"

#include "mpsl.h"
#include "mpsl_timeslot.h"

#include "sdc.h"
#include "sdc_hci.h"
#include "sdc_hci_vs.h"

#include "sdc_soc.h"

#include "rand_numbers.h"
#include "nrfx_rng.h"

#define MASTER_COUNT 2
#define SLAVE_COUNT 2
#define TX_SIZE 251
#define RX_SIZE 251
#define TX_COUNT 3
#define RX_COUNT 3

#define SOC_CONFIG_PRIO_HIGH 0
#define SOC_CONFIG_PRIO_LOW 4

#define SDC_MEM_REQUIRED_MAX(master_count, slave_count, tx_size, rx_size, tx_count, rx_count) \
    ( (SDC_MEM_PER_MASTER_LINK(tx_size, rx_size, tx_count, rx_count) * master_count) + \
      (SDC_MEM_PER_SLAVE_LINK(tx_size, rx_size, tx_count, rx_count) * slave_count) + \
      ((master_count > 0) ? SDC_MEM_MASTER_LINKS_SHARED : 0) + \
      ((slave_count > 0) ? SDC_MEM_SLAVE_LINKS_SHARED : 0))

/*lint -emacro(506, BLE_REQUIRED_MEMORY) Constant value Boolean */
#define BLE_REQUIRED_MEMORY SDC_MEM_REQUIRED_MAX(MASTER_COUNT, \
                                                 SLAVE_COUNT,  \
                                                 TX_SIZE,      \
                                                 RX_SIZE,      \
                                                 TX_COUNT,     \
                                                 RX_COUNT)

/* HCI message packet element definition */
typedef uint8_t hci_element_t;

#define M_DEFAULT_DEVICE_ADDRESS    {0x56, 0xFF, 0x99, 0x00, 0xCD, 0x29} // The default public address

/* The UART header is 1 byte */
#define H4_UART_HEADER_SIZE 1
#define M_H4_RX_BUFFER_SIZE (H4_UART_HEADER_SIZE + HCI_MSG_BUFFER_MAX_SIZE)
#define M_H4_TX_BUFFER_SIZE (H4_UART_HEADER_SIZE + HCI_MSG_BUFFER_MAX_SIZE)

/* Receive states */
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

/* Buffers for sending and receiving */
static uint8_t m_h4_rx_buffer[M_H4_RX_BUFFER_SIZE];
static uint8_t m_h4_tx_buffer[M_H4_TX_BUFFER_SIZE];

static volatile bool m_tx_buffer_available = true;

static uint8_t m_sdc_dynamic_mem[BLE_REQUIRED_MEMORY];

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

static uint8_t * m_p_to_event_length_get(uint8_t const * p_h4_buf)
{
  return (uint8_t*)&p_h4_buf[H4_UART_HEADER_SIZE + 1];
}

static uint8_t * m_p_to_cmd_length_get(uint8_t const * p_h4_buf)
{
  return (uint8_t*)&p_h4_buf[H4_UART_HEADER_SIZE + 2];
}

static uint32_t m_data_to_host_get(void)
{
  const uint8_t acl_packet_header_size = 2;
  const uint8_t acl_packet_len_size = 2;
  uint32_t packet_length = 0;

  if (sdc_hci_data_get(&m_h4_tx_buffer[H4_UART_HEADER_SIZE]) == 0)
  {
    m_h4_tx_buffer[0] = (uint8_t)H4_UART_HCI_ACL_DATA_PACKET;
    packet_length = H4_UART_HEADER_SIZE + acl_packet_header_size + acl_packet_len_size + *m_p_to_acl_data_length_get(m_h4_tx_buffer);
  }

  return packet_length;
}

static uint32_t m_evt_to_host_get(void)
{
  const uint8_t evt_packet_header_size = 1;
  const uint8_t evt_packet_len_size = 1;
  uint32_t packet_length = 0;

  if (sdc_hci_evt_get(&m_h4_tx_buffer[H4_UART_HEADER_SIZE]) == 0)
  {
    m_h4_tx_buffer[0] = (uint8_t)H4_UART_HCI_EVENT_PACKET;
    packet_length = H4_UART_HEADER_SIZE + evt_packet_header_size + evt_packet_len_size + *m_p_to_event_length_get(m_h4_tx_buffer);
  }

  return packet_length;

}

static void m_try_send_evt_or_data_to_host(void)
{
  /* Alternate priority between data & event. This needs to be done because there may be
  a case where the Tx channel will work but the Rx channel will stall. This can happen in
  a situation where the time between connection events is only that big where only one
  dh_job() can run which will always be occupied to send a num_complete_event and the
  data pkt get will starve. */
  static bool last_packet_to_host_was_evt = false;

  uint32_t length_to_host;

  if (last_packet_to_host_was_evt)
  {
    length_to_host = m_data_to_host_get();
    last_packet_to_host_was_evt = false;
  }
  else
  {
    length_to_host = m_evt_to_host_get();
    last_packet_to_host_was_evt = true;
  }

  if (length_to_host != 0)
  {
    m_tx_buffer_available = false;
    nrfx_uarte_tx(&uarte_instance, m_h4_tx_buffer, length_to_host);
  }
}


static void m_state_recv_h4_header_enter(void)
{
  nrfx_uarte_rx(&uarte_instance, &m_h4_rx_buffer[0], 1);
}


/* Receive data or command from host to controller */
static void m_start_recv_h4_header_from_host(void)
{
    nrfx_uarte_rx(&uarte_instance, &m_h4_rx_buffer[0], 1);
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
        nrfx_uarte_rx(&uarte_instance, &m_h4_rx_buffer[H4_UART_HEADER_SIZE], 4);
        break;
    case STATE_RECV_CMD_HEADER:
        nrfx_uarte_rx(&uarte_instance, &m_h4_rx_buffer[H4_UART_HEADER_SIZE], 3);
        break;
    case STATE_RECV_PACKET_CONTENT:
        switch (m_h4_rx_buffer[0])
        {
        case H4_UART_HCI_ACL_DATA_PACKET:
            nrfx_uarte_rx(&uarte_instance,
                          &m_h4_rx_buffer[H4_UART_HEADER_SIZE + 4],
                          *m_p_to_acl_data_length_get(m_h4_rx_buffer));
            break;
        case H4_UART_HCI_COMMAND_PACKET:
            nrfx_uarte_rx(&uarte_instance,
                          &m_h4_rx_buffer[H4_UART_HEADER_SIZE + 3],
                          *m_p_to_cmd_length_get(m_h4_rx_buffer));
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
    case NRFX_UARTE_EVT_TX_DONE:
        m_tx_buffer_available = true;
        break;
    case NRFX_UARTE_EVT_RX_DONE:
        m_continue_recv_packet_from_host(&p_event->data.rxtx);
        break;
    case NRFX_UARTE_EVT_ERROR:
        //m_fault_handler();
        break;
    }
}

/* Buffer for writing to UART */
char tmp_buff[128];


static void sample_job(void)
{
  if (m_tx_buffer_available)
  {
    m_try_send_evt_or_data_to_host();
  }
}

static void host_event_interrupt(void)
{
  /* Do nothing */
}


int main()
{
    // For checking the returns of the init procedures
    int32_t retcode;

    retcode = mpsl_init(&clock_config, SWI5_IRQn, m_fault_handler);
    NRFX_ASSERT(retcode == 0);

    retcode = sdc_init(m_fault_handler);
    NRFX_ASSERT(retcode == 0);

    // Enable all configurable features
#ifdef INCLUDE_FEATURE_SLAVE_ROLE
#ifdef INCLUDE_FEATURE_ADV_EXTENSIONS
  retcode = sdc_support_ext_adv();
  NRFX_ASSERT(retcode == 0);
#else
  retcode = sdc_support_adv();
  NRFX_ASSERT(retcode == 0);
#endif
  retcode = sdc_support_slave();
  NRFX_ASSERT(retcode == 0);
#endif

#ifdef INCLUDE_FEATURE_SCANNER_ROLE
#ifdef INCLUDE_FEATURE_ADV_EXTENSIONS
  retcode = sdc_support_ext_scan();
  NRFX_ASSERT(retcode == 0);
#else
  retcode = sdc_support_scan();
  NRFX_ASSERT(retcode == 0);
#endif
#endif

#ifdef INCLUDE_FEATURE_MASTER_ROLE
  retcode = sdc_support_master();
  NRFX_ASSERT(retcode == 0);
#endif
#ifdef INCLUDE_FEATURE_DLE
  retcode = sdc_support_dle();
  NRFX_ASSERT(retcode == 0);
#endif

  retcode = sdc_support_le_2m_phy();
  NRFX_ASSERT(retcode == 0);

#ifdef INCLUDE_FEATURE_CODED_PHY
  retcode = sdc_support_le_coded_phy();
  NRFX_ASSERT(retcode == 0);
#endif

    rand_init();

    sdc_rand_source_t rand_functions = {
        .rand_prio_low_get = rand_prio_low_vector_get,
        .rand_prio_high_get = rand_prio_high_vector_get,
        .rand_poll = rand_prio_low_vector_get_blocking
    };

    retcode = sdc_rand_source_register(&rand_functions);
    NRFX_ASSERT(retcode == 0);

  int32_t bytes_needed = sdc_cfg_set(SDC_DEFAULT_RESOURCE_CFG_TAG,
      SDC_CFG_TYPE_NONE,
      NULL);

  sdc_cfg_t resource_cfg;

  resource_cfg.buffer_cfg.tx_packet_count = TX_COUNT;
  resource_cfg.buffer_cfg.rx_packet_count = RX_COUNT;

#ifndef INCLUDE_FEATURE_DLE
  resource_cfg.buffer_cfg.tx_packet_size  = SDC_DEFAULT_TX_PACKET_SIZE;
  resource_cfg.buffer_cfg.rx_packet_size  = SDC_DEFAULT_RX_PACKET_SIZE;
#else
  resource_cfg.buffer_cfg.tx_packet_size  = TX_SIZE;
  resource_cfg.buffer_cfg.rx_packet_size  = RX_SIZE;
#endif

  bytes_needed += sdc_cfg_set(SDC_DEFAULT_RESOURCE_CFG_TAG,
                              SDC_CFG_TYPE_BUFFER_CFG,
                              &resource_cfg);

  NRFX_ASSERT(bytes_needed > 0);
  NRFX_ASSERT(bytes_needed <= sizeof(m_sdc_dynamic_mem));

#ifdef INCLUDE_FEATURE_MASTER_ROLE
  resource_cfg.master_count.count = MASTER_COUNT;
  bytes_needed = sdc_cfg_set(SDC_DEFAULT_RESOURCE_CFG_TAG,
                             SDC_CFG_TYPE_MASTER_COUNT,
                             &resource_cfg);
  NRFX_ASSERT(bytes_needed > 0);
  NRFX_ASSERT(bytes_needed <= sizeof(m_sdc_dynamic_mem));

#endif

  resource_cfg.slave_count.count = SLAVE_COUNT;
  bytes_needed = sdc_cfg_set(SDC_DEFAULT_RESOURCE_CFG_TAG,
                             SDC_CFG_TYPE_SLAVE_COUNT,
                             &resource_cfg);
  NRFX_ASSERT(bytes_needed > 0);
  NRFX_ASSERT(bytes_needed <= sizeof(m_sdc_dynamic_mem));

  retcode = sdc_enable(host_event_interrupt, m_sdc_dynamic_mem);
  NRFX_ASSERT(retcode >= 0);

    nrfx_uarte_uninit(&uarte_instance);

    (void)nrfx_uarte_init(&uarte_instance, &uarte_config, nrfx_uarte_event_handler);

    m_state_recv_h4_header_enter();


    NVIC_SetPriority(RADIO_IRQn,   SOC_CONFIG_PRIO_HIGH);
    NVIC_SetPriority(RTC0_IRQn,    SOC_CONFIG_PRIO_HIGH);
    NVIC_SetPriority(SWI5_IRQn,    SOC_CONFIG_PRIO_LOW);
    NVIC_SetPriority(RNG_IRQn,     SOC_CONFIG_PRIO_LOW);
    NVIC_SetPriority(UARTE0_UART0_IRQn,   SOC_CONFIG_PRIO_LOW + 1);

    for(;;)
    {
        sample_job();
    }

    return 0;
}

void POWER_CLOCK_IRQHandler(void)
{
  MPSL_IRQ_CLOCK_Handler();
}

void RADIO_IRQHandler(void)
{
  MPSL_IRQ_RADIO_Handler();
}

void TIMER0_IRQHandler(void)
{
  MPSL_IRQ_TIMER0_Handler();
}

void RTC0_IRQHandler(void)
{
  MPSL_IRQ_RTC0_Handler();
}

void RNG_IRQHandler(void)
{
  nrfx_rng_irq_handler();
}

void SWI5_IRQHandler(void)
{
  mpsl_low_priority_process();
}

void UARTE0_UART0_IRQHandler(void)
{
  nrfx_uarte_0_irq_handler();
}

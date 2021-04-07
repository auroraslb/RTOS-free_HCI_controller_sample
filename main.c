#include <stdio.h>
#include "nrfx_uarte.h"
#include <string.h>

static nrfx_uarte_t uarte_instance = {.p_reg = NRF_UARTE0};

static nrfx_uarte_config_t uarte_config =
{
  .pselrts            = 5,
  .pseltxd            = 6,
  .pselcts            = 7,
  .pselrxd            = 8,
  .p_context          = NULL,
  .baudrate           = NRF_UARTE_BAUDRATE_1000000,
  .interrupt_priority = 2,
  .hal_cfg = {
    .hwfc = NRF_UARTE_HWFC_ENABLED,
    .parity = NRF_UARTE_PARITY_EXCLUDED,
  #if defined(UARTE_CONFIG_STOP_Msk)
    .stop = NRF_UARTE_STOP_ONE,
  #endif
  },
};

char tmp_buff[128];

int main() {
    //printf("Hello, World!");

    nrfx_uarte_uninit(&uarte_instance);

    (void)nrfx_uarte_init(&uarte_instance, &uarte_config, NULL);

    char * string = "Hello, World!";

    sprintf((char*)tmp_buff, "%s", string);
    nrfx_uarte_tx(&uarte_instance, (const uint8_t*) tmp_buff, strlen((const char*)string));

   return 0;
}

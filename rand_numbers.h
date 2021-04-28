#include <stdint.h>

void rand_init(void);

uint8_t rand_prio_low_vector_get(uint8_t * p_buff, uint8_t length);
uint8_t rand_prio_high_vector_get(uint8_t * p_buff, uint8_t length);
void rand_prio_low_vector_get_blocking(uint8_t * p_buff, uint8_t length);

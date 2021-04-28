#include "rand_numbers.h"
#include "nrfx_rng.h"
#include "nrf.h"

#ifndef NRFX_RNG_DEFAULT_CONFIG_IRQ_PRIORITY
#define NRFX_RNG_DEFAULT_CONFIG_IRQ_PRIORITY 7
#endif

#define RAND_POOL_CAPACITY    (64)
#define RAND_POOL_SIZE        ( RAND_POOL_CAPACITY + 1 )

typedef struct
{
  uint8_t    q[RAND_POOL_SIZE];
  uint8_t    in;
  uint8_t    out;
} rand_queue_t;


/** Threadsafe queues for random numbers */
static  rand_queue_t m_prio_low_pool;    ///< PRIO_LOW pool
static  rand_queue_t m_prio_high_pool;      ///< PRIO_HIGH pool


/** @brief Initialize a pool. */
static void m_pool_init(rand_queue_t * p_queue);

/** @brief Check if a pool is full. */
static bool m_pool_full(rand_queue_t * p_queue);

/** @brief Get the number of elements in the pool. */
static uint8_t m_pool_count(rand_queue_t * p_queue);

/** @brief Add a random number to the pool. */
static void m_pool_enqueue(rand_queue_t * p_queue, uint8_t value);

/** @brief Get a random number from the pool. */
static void m_pool_dequeue(rand_queue_t * p_queue, uint8_t * p_rand);

/** @brief Get N random numbers from the pool. */
static uint8_t m_pool_vector_get(rand_queue_t * p_queue, uint8_t * p_buff, uint8_t length);

/** @brief Get N random numbers from the pool. */
static uint8_t m_pool_vector_get(rand_queue_t * p_queue, uint8_t * p_buff, uint8_t length);



static void rng_handler(uint8_t rng_val)
{
    static rand_queue_t * all_pools[] = {&m_prio_high_pool, &m_prio_low_pool};

    /* Find first pool that is not full (in priority order) and enqueue. */
    for (uint8_t i = 0; i < (sizeof((all_pools)) / sizeof(all_pools[0])); i++)
    {
        rand_queue_t *p_pool = all_pools[i];
        if (!m_pool_full(p_pool))
        {
            m_pool_enqueue(p_pool, rng_val);
        break;
        }
    }
}

void rand_init(void) {
    nrfx_rng_config_t config = NRFX_RNG_DEFAULT_CONFIG;

    m_pool_init(&m_prio_low_pool);
    m_pool_init(&m_prio_high_pool);

    uint32_t err_code = nrfx_rng_init(&config, rng_handler);
    NRFX_ASSERT(err_code == NRFX_SUCCESS);

    nrfx_rng_start();
}

uint8_t rand_prio_low_vector_get(uint8_t * p_buff, uint8_t length)
{
    bool status = false;

    if ( length <= m_pool_count(&m_prio_low_pool) )
    {
        for(uint8_t i = 0; i < length; i++)
        {
            m_pool_dequeue(&m_prio_low_pool, &p_buff[i]);
        }
        status = true;
    }

    if (status) {
        return length;
    }
    return 0;
}

uint8_t rand_prio_high_vector_get(uint8_t * p_buff, uint8_t length)
{
    bool status = false;

    if ( length <= m_pool_count(&m_prio_high_pool) )
    {
        for(uint8_t i = 0; i < length; i++)
        {
            m_pool_dequeue(&m_prio_high_pool, &p_buff[i]);
        }
        status = true;
    }

    if (status) {
        return length;
    }
    return 0;
}

void rand_prio_low_vector_get_blocking(uint8_t * p_buff, uint8_t length)
{
    NRFX_ASSERT(length <= RAND_POOL_SIZE);

    bool current_prio_blocks_rng_irq =
        (NVIC_GetPriority((IRQn_Type)(((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk)>>SCB_ICSR_VECTACTIVE_Pos)-16)) & 0xFF)
        <=
        (NVIC_GetPriority(RNG_IRQn) & 0xFF);

    while (!m_pool_vector_get(&m_prio_low_pool, p_buff, length))
    {
        if(current_prio_blocks_rng_irq)
        {
            while (!nrf_rng_event_check(NRF_RNG, NRF_RNG_EVENT_VALRDY)) {}
            nrfx_rng_irq_handler();
        }
        else {}
    }
}


static void m_pool_init(rand_queue_t * p_queue)
{
    p_queue->in = p_queue->out = 0;
}

static bool m_pool_full(rand_queue_t * p_queue)
{
    return (p_queue->in + 1) % RAND_POOL_SIZE == p_queue->out;
}

static uint8_t m_pool_count(rand_queue_t * p_queue)
{
    return p_queue->in >= p_queue->out
         ? p_queue->in - p_queue->out
         : (RAND_POOL_SIZE - p_queue->out) + p_queue->in;
}

static void m_pool_enqueue(rand_queue_t * p_queue, uint8_t value)
{
    p_queue->q[p_queue->in] = value;
    p_queue->in = (p_queue->in + 1) % RAND_POOL_SIZE;
}

static void m_pool_dequeue(rand_queue_t * p_queue, uint8_t * p_rand)
{
    *p_rand = p_queue->q[p_queue->out];
    p_queue->out = (p_queue->out + 1) % RAND_POOL_SIZE;
}

static uint8_t m_pool_vector_get(rand_queue_t * p_queue, uint8_t * p_buff, uint8_t length)
{
    bool status = false;

    if ( length <= m_pool_count(p_queue) )
    {
        for(uint8_t i = 0; i < length; i++)
        {
            m_pool_dequeue(p_queue, &p_buff[i]);
        }
        status = true;
    }

    return status ? length : 0;
}

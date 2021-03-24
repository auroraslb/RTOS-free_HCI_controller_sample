/*
 * Copyright (c) Nordic Semiconductor ASA. All Rights Reserved.
 *
 * The information contained herein is confidential property of Nordic Semiconductor ASA.
 * The use, copying, transfer or disclosure of such information is prohibited except by
 * express written agreement with Nordic Semiconductor ASA.
 */

#ifndef NRFX_GLUE_H__
#define NRFX_GLUE_H__

#define NRFX_ASSERT(expression)
#define NRFX_STATIC_ASSERT(expression)
#define NRFX_IRQ_PRIORITY_SET(irq_number, priority) NVIC_SetPriority(irq_number, priority);
#define NRFX_IRQ_ENABLE(irq_number) NVIC_EnableIRQ(irq_number);
#define NRFX_IRQ_DISABLE(irq_number) NVIC_DisableIRQ(irq_number);
#define NRFX_DELAY_US(us_time);

#endif // NRFX_GLUE_H__
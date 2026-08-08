/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZENBEDDED_SETPOINT_H_
#define ZENBEDDED_SETPOINT_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A command handed from the comms thread to the control thread.
 *
 * Always moved as a whole. The control thread never observes a half-updated
 * setpoint, so there is no state in which it can act on this frame's position
 * with the previous frame's sequence number.
 */
struct zb_setpoint {
	/** Host sequence number, echoed back in telemetry. */
	uint32_t seq;
	/** Target position, radians. */
	float position;
	/** Target velocity, radians per second. */
	float velocity;
	/** Device-time cycle count at which the command arrived. */
	uint64_t arrival_cycles;
};

/**
 * @brief Wait-free single-producer/single-consumer setpoint channel.
 *
 * Three slots and one atomic word. The producer (comms) always writes into a
 * slot nobody else can see, then exchanges its slot index for whatever was
 * published; the consumer (control) exchanges its own slot index for the
 * published one. Because both operations are a single atomic exchange, and the
 * three indices are always a permutation of {0,1,2}, neither side can ever be
 * writing the slot the other is reading.
 *
 * Both sides are wait-free: exactly one atomic exchange, no retry loop, no
 * spinning. That matters more than it looks. A seqlock would be simpler to
 * write, but its reader retries, and "retries until the writer stops" is not a
 * bound the control loop is allowed to have.
 *
 * The producer may die at any point without leaving the channel wedged: the
 * consumer simply stops seeing fresh data, which is exactly the condition the
 * failover state machine is built to detect.
 */
struct zb_sp_chan {
	struct zb_setpoint slot[3];
	/** Published slot index, or'd with ZB_SP_FRESH when unconsumed. */
	atomic_t shared;
	/** Producer-private slot index. Touched only by the comms thread. */
	uint8_t producer_idx;
	/** Consumer-private slot index. Touched only by the control thread. */
	uint8_t consumer_idx;
};

#define ZB_SP_IDX_MASK 0x3U
#define ZB_SP_FRESH    0x4U

/**
 * @brief Initialise a setpoint channel.
 *
 * Must be called before either thread touches the channel.
 */
static inline void zb_sp_init(struct zb_sp_chan *c)
{
	c->producer_idx = 0U;
	c->consumer_idx = 1U;
	atomic_set(&c->shared, 2);
}

/**
 * @brief Publish a setpoint. Producer side, wait-free.
 *
 * Overwrites any previously published setpoint that the consumer has not taken
 * yet. That is deliberate: the control loop wants the newest command, not a
 * backlog of stale ones.
 */
static inline void zb_sp_publish(struct zb_sp_chan *c, const struct zb_setpoint *sp)
{
	c->slot[c->producer_idx] = *sp;

	atomic_val_t prev = atomic_set(&c->shared, (atomic_val_t)(c->producer_idx | ZB_SP_FRESH));

	c->producer_idx = (uint8_t)(prev & ZB_SP_IDX_MASK);
}

/**
 * @brief Take the newest setpoint, if one was published. Consumer side, wait-free.
 *
 * @retval true  @p out was filled with a setpoint not previously returned.
 * @retval false nothing new; @p out untouched.
 */
static inline bool zb_sp_consume(struct zb_sp_chan *c, struct zb_setpoint *out)
{
	if ((atomic_get(&c->shared) & ZB_SP_FRESH) == 0) {
		return false;
	}

	atomic_val_t prev = atomic_set(&c->shared, (atomic_val_t)c->consumer_idx);

	c->consumer_idx = (uint8_t)(prev & ZB_SP_IDX_MASK);
	*out = c->slot[c->consumer_idx];

	return true;
}

#ifdef __cplusplus
}
#endif

#endif /* ZENBEDDED_SETPOINT_H_ */

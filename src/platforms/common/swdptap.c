/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file implements the SW-DP interface. */

#include "general.h"
#include "timing.h"
#include "adiv5.h"

typedef enum swdio_status_e {
	SWDIO_STATUS_FLOAT = 0,
	SWDIO_STATUS_DRIVE
} swdio_status_t;

static void swdptap_turnaround(swdio_status_t dir) __attribute__((optimize(3)));
static uint32_t swdptap_seq_in(size_t clock_cycles) __attribute__((optimize(3)));
static bool swdptap_seq_in_parity(uint32_t *ret, size_t clock_cycles) __attribute__((optimize(3)));
static void swdptap_seq_out(uint32_t tms_states, size_t clock_cycles) __attribute__((optimize(3)));
static void swdptap_seq_out_parity(uint32_t tms_states, size_t clock_cycles) __attribute__((optimize(3)));

static void swdptap_turnaround(const swdio_status_t dir)
{
	static swdio_status_t olddir = SWDIO_STATUS_FLOAT;
	/* Don't turnaround if direction not changing */
	if (dir == olddir)
		return;
	olddir = dir;

#ifdef DEBUG_SWD_BITS
	DEBUG("%s", dir ? "\n-> " : "\n<- ");
#endif

	if (dir == SWDIO_STATUS_FLOAT)
		SWDIO_MODE_FLOAT();
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for (volatile int32_t cnt = swd_delay_cnt; --cnt > 0;)
		continue;
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	for (volatile int32_t cnt = swd_delay_cnt; --cnt > 0;)
		continue;
	if (dir == SWDIO_STATUS_DRIVE)
		SWDIO_MODE_DRIVE();
}

static uint32_t swdptap_seq_in_swd_delay(size_t clock_cycles) __attribute__((optimize(3)));
static uint32_t swdptap_seq_in_swd_delay(size_t clock_cycles)
{
	size_t index = 0;
	uint32_t value = 0;
	while (clock_cycles--) {
		if (gpio_get(SWDIO_PORT, SWDIO_PIN))
			value |= (1U << index);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
		++index;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
	}
	return value;
}

static uint32_t swdptap_seq_in_no_delay(size_t clock_cycles) __attribute__((optimize(3)));
static uint32_t swdptap_seq_in_no_delay(size_t clock_cycles)
{
	size_t index = 0;
	uint32_t value = 0;
	while (clock_cycles--) {
		if (gpio_get(SWDIO_PORT, SWDIO_PIN))
			value |= (1U << index);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		++index;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
	return value;
}

static uint32_t swdptap_seq_in(size_t clock_cycles)
{
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (swd_delay_cnt)
		return swdptap_seq_in_swd_delay(clock_cycles);
	else // NOLINT(readability-else-after-return)
		return swdptap_seq_in_no_delay(clock_cycles);
}

static bool swdptap_seq_in_parity(uint32_t *ret, size_t ticks)
{
	uint32_t index = 1;
	uint32_t res = 0;
	bool bit;
	int len = ticks;
	register volatile int32_t cnt;

	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (swd_delay_cnt) {
		while (len--) {
			bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
			res |= (bit) ? index : 0;
			index <<= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
		}
	} else {
		while (len--) {
			bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			res |= (bit) ? index : 0;
			index <<= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
		}
	}
	int parity = __builtin_popcount(res);
	bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
	parity += (bit) ? 1 : 0;
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < len; i++)
		DEBUG("%d", (res & (1 << i)) ? 1 : 0);
#endif
	*ret = res;
	/* Terminate the read cycle now */
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	return (parity & 1);
}

static void swdptap_seq_out(uint32_t tms_states, size_t ticks)
{
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (tms_states & (1 << i)) ? 1 : 0);
#endif
	register volatile int32_t cnt;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, tms_states & 1);
	if (swd_delay_cnt) {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
			tms_states >>= 1;
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, tms_states & 1);
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
		}
	} else {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			tms_states >>= 1;
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, tms_states & 1);
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
		}
	}
}

static void swdptap_seq_out_parity(uint32_t tms_states, size_t ticks)
{
	int parity = __builtin_popcount(tms_states);
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (tms_states & (1 << i)) ? 1 : 0);
#endif
	register volatile int32_t cnt;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, tms_states & 1);
	tms_states >>= 1;
	if (swd_delay_cnt) {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, tms_states & 1);
			tms_states >>= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
		}
	} else {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, tms_states & 1);
			tms_states >>= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
		}
	}
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, parity & 1);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
}

int swdptap_init(ADIv5_DP_t *dp)
{
	dp->seq_in  = swdptap_seq_in;
	dp->seq_in_parity  = swdptap_seq_in_parity;
	dp->seq_out = swdptap_seq_out;
	dp->seq_out_parity  = swdptap_seq_out_parity;

	return 0;
}

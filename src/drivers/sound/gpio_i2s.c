/*
 * Copyright (C) 2015 Google Inc.
 * Copyright (C) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.
 */

#include <libpayload.h>
#include <stdint.h>
#include <sysinfo.h>

#include "base/container_of.h"
#include "drivers/sound/gpio_i2s.h"

/* Generate square wave sound data for 1 second. */
static void i2s_square_wave(uint16_t *data, uint16_t samples,
		uint32_t frequency, uint16_t volume)
{
	unsigned period = samples / frequency;
	unsigned half = period / 2;

	while (samples) {
		for (int i = 0; samples && i < half; samples--, i++)
				*data++ = volume;
		for (int i = 0; samples && i < period - half; samples--, i++)
				*data++ = -volume;
	}
}

static void gpio_i2s_send_bit(GpioI2s *i2s, int channel_select, int bit)
{
	gpio_set(i2s->sfrm_gpio, channel_select & 1);
	gpio_set(i2s->bclk_gpio, 1);
	udelay(1);
	gpio_set(i2s->data_gpio, bit);
	gpio_set(i2s->bclk_gpio, 0);
}

/* Send I2S data */
static void gpio_i2s_send(GpioI2s *i2s, uint16_t *data, size_t length)
{
	for (; length > 0; length--, data++) {
		/* Left channel */
		for (int i = 15; i >= 0; i--)
			gpio_i2s_send_bit(i2s, 0, !!(*data & (1 << i)));
		/* Right channel */
		for (int i = 15; i >= 0; i--)
			gpio_i2s_send_bit(i2s, 1, !!(*data & (1 << i)));
	}
}

static int gpio_i2s_play(SoundOps *me, uint32_t msec, uint32_t frequency)
{
	GpioI2s *i2s = container_of(me, GpioI2s, ops);

	unsigned bytes = i2s->sample_rate * (sizeof(uint16_t));
	uint16_t *data = xmalloc(bytes);

	/* Generate a square wave */
	i2s_square_wave(data, i2s->sample_rate,
			frequency, i2s->volume);

	/* Send 1 second chunks */
	while (msec >= 1000) {
		gpio_i2s_send(i2s, data, bytes / sizeof(uint16_t));
		msec -= 1000;
	}
	/* Send remaining portion of a second */
	if (msec) {
		size_t len = (bytes * msec) / (sizeof(uint16_t) * 1000);

		gpio_i2s_send(i2s, data, len);
	}

	free(data);

	return 0;
}

GpioI2s *new_gpio_i2s(GpioOps *bclk_gpio, GpioOps *sfrm_gpio,
		      GpioOps *data_gpio, uint16_t sample_rate,
		      uint8_t channels, uint16_t volume)
{
	GpioI2s *i2s = xzalloc(sizeof(*i2s));

	i2s->ops.play = &gpio_i2s_play;

	i2s->bclk_gpio		= bclk_gpio;
	i2s->sfrm_gpio		= sfrm_gpio;
	i2s->data_gpio		= data_gpio;
	i2s->sample_rate	= sample_rate;
	i2s->channels		= channels;
	i2s->volume		= volume;

	return i2s;
}

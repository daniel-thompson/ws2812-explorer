/*
 * This file is part of the ws2812-explorer project.
 *
 * Copyright (C) 2014 Daniel Thompson <daniel@redfelineninja.org.uk>
 * Copyright (C) 2019 Daniel Thompson <daniel@redfelineninja.org.uk>
 * 
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/desig.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <librfn/console.h>
#include <librfn/fibre.h>
#include <librfn/regdump.h>
#include <librfn/time.h>
#include <librfn/util.h>

#include "led.h"

static console_t cdcacm_console;
static volatile bool animate = false;

static void __attribute__((noinline)) do_animation(void)
{
	static int led = 25;
	static uint32_t goal, next = 0;
	static int32_t step = 0;

	if (!step) {
		led = (led + 1) % LED_COUNT;
		goal = next;
		next = led_data[led];
		step = ((int32_t) (goal - next) / 16) + 1;
	}

	uint32_t oldval = led_data[led];
	uint32_t newval = oldval + step;

	/* if we overflow or reach past the goal then make it right */
	if (((oldval ^ newval) & 0x80000000) ||
	    (step > 0 && newval >= goal) ||
	    (step < 0 && newval <= goal)) {
		newval = goal;
		step = 0;
	}

	led_data[led] = newval;
}

static int animation_fibre(fibre_t *fibre)
{
	static uint32_t time;

	PT_BEGIN_FIBRE(fibre);

	time = time_now();

	while (animate) {
		do_animation();

		time += 1000000 / 32;
		//time += 1000000;
		PT_WAIT_UNTIL(fibre_timeout(time));
	}

	PT_END();
}
static fibre_t animation_task = FIBRE_VAR_INIT(animation_fibre);

static void jump_to_bootloader(void)
{
	char * const marker = (char *)0x20004800; /* RAM@18K */
	const char key[] = "remain-in-loader";

	memcpy(marker, key, sizeof(key));
	scb_reset_system(); /* Will never return. */
}

static pt_state_t do_id(console_t *c)
{
	char serial_no[25];

	desig_get_unique_id_as_string(serial_no, sizeof(serial_no));
	fprintf(c->out, "%s\n", serial_no);

	return PT_EXITED;
}

static pt_state_t do_reboot(console_t *c)
{
	(void) c;
	jump_to_bootloader();
	return PT_EXITED;
}

static pt_state_t do_uptime(console_t *c)
{
	unsigned int hours, minutes, seconds, microseconds;

	uint64_t t = time64_now();

	/* get to 32-bit values as directly as possible */
	minutes = t / (60 * 1000000);
	microseconds = t % (60 * 1000000);

	hours = minutes / 60;
	minutes %= 60;
	seconds = microseconds / 1000000;
	microseconds %= 1000000;

	fprintf(c->out, "%02u:%02u:%02u.%03u\n", hours, minutes, seconds,
		microseconds / 1000);

	return PT_EXITED;
}

static pt_state_t do_version(console_t *c)
{
	fprintf(c->out, "usb-ws2812 version %s (%s %s)\n", VERSION, __DATE__,
		__TIME__);
	return PT_EXITED;
}

static pt_state_t do_single(console_t *c)
{
	const char *lamp = c->argv[1];
	const char *colour = c->argv[2];

	int offset = strtol(lamp, NULL, 0);
	if (offset < 0 || offset >= LED_COUNT) {
		fprintf(c->out, "bad offset\n");
		return PT_EXITED;
	}

	uint32_t rgb = strtol(colour, NULL, 0);
	led_data[offset] = rgb;

	return PT_EXITED;
}

static pt_state_t do_right(console_t *c)
{
	(void) c;
	uint32_t last = led_data[LED_COUNT-1];
	for (int i=LED_COUNT-2; i>0; i--)
		led_data[i+1] = led_data[i];
	led_data[0] = last;
	return PT_EXITED;
}

static pt_state_t do_left(console_t *c)
{
	(void) c;
	uint32_t first = led_data[0];
	for (int i=0; i<LED_COUNT-2; i++)
		led_data[i] = led_data[i+1];
	led_data[LED_COUNT-1] = first;
	return PT_EXITED;
}

static pt_state_t do_set(console_t *c)
{
	int led;
	char num[7] = { 0 };
	char *nend = num + 1;

	bool repeat = 0 == strcmp(c->argv[0], "repeat");
	char *values = c->argv[1];

	for (led=0; *values && led<LED_COUNT; led++) {
		if (0 == strncmp("on", values, 2)) {
			led_data[led] = 0x404040;
			values += 2;
		} else if (0 == strncmp("off", values, 3)) {
			led_data[led] = 0;
			values += 3;
		} else if (0 == strncmp("red", values, 3)) {
			led_data[led] = 0x008000;
			values += 3;
		} else if (0 == strncmp("green", values, 5)) {
			led_data[led] = 0x800000;
			values += 5;
		} else if (0 == strncmp("blue", values, 4)) {
			led_data[led] = 0x000080;
			values += 4;
		} else if (0 == strncmp("yellow", values, 6)) {
			led_data[led] = 0x606000;
			values += 6;
		} else {
			strncpy(num, values, 6);
			led_data[led] = strtol(num, &nend, 16);
			values += nend - num;

			if (num == nend) {
				printf("bad value %s\n", num);

				values = strchr(values, ',');
				if (!values)
					values = num+6;
			}
		}
		while (*values == ',')
			values++;
	}


	if (led == 0)
		led_data[led++] = 0;

	if (led == 1)
		repeat = true;

	if (repeat) {
		for (int nvalues = led; led<LED_COUNT; led++)
			led_data[led] = led_data[led % nvalues];
	} else {
		for ( ; led<LED_COUNT; led++)
			led_data[led] = 0;
	}

	return PT_EXITED;
}


static pt_state_t do_animate_cmd(console_t *c)
{
	if (0 == strcmp("step", c->argv[1])) {
		if (animate)
			animate = false;
		else
			do_animation();
	} else if (0 == strcmp("on", c->argv[1])) {
		animate = true;
	} else {
		animate = strtol(c->argv[1], NULL, 0);
	}

	if (animate == true)
		fibre_run(&animation_task);

	return PT_EXITED;
}

static const console_cmd_t cmds[] = {
	CONSOLE_CMD_VAR_INIT("id", do_id),
	CONSOLE_CMD_VAR_INIT("reboot", do_reboot),
	CONSOLE_CMD_VAR_INIT("uptime", do_uptime),
	CONSOLE_CMD_VAR_INIT("version", do_version),
	CONSOLE_CMD_VAR_INIT("single", do_single),
	CONSOLE_CMD_VAR_INIT("right", do_right),
	CONSOLE_CMD_VAR_INIT("left", do_left),
	CONSOLE_CMD_VAR_INIT("animate", do_animate_cmd),
	CONSOLE_CMD_VAR_INIT("set", do_set),
	CONSOLE_CMD_VAR_INIT("repeat", do_set),
};

const console_gpio_t gpios[] = {
	CONSOLE_GPIO_VAR_INIT("led", GPIOC, GPIO13, console_gpio_active_low |
						    console_gpio_default_on),
};

int main(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable clocks for GPIO ports */
	rcc_periph_clock_enable(RCC_GPIOC);

	time_init();

	console_init(&cdcacm_console, stdout);
	for (unsigned int i=0; i<lengthof(cmds); i++)
		console_register(&cmds[i]);
	for (unsigned int i=0; i<lengthof(gpios); i++)
		console_gpio_register(&gpios[i]);

	led_init();

	fibre_scheduler_main_loop();
}


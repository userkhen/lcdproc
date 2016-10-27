#ifndef HD_LCDGPIO_H
#define HD_LCDGPIO_H

#include "lcd.h"		/* for Driver */
#include <stdlib.h>
/* initialize this particular driver */
int hd_init_gpio(Driver *drvthis);

typedef struct gpio_pin_t {
	int num;
	FILE *fd;
} gpio_pin;

/**
 * gpio_map is addressed through the hd44780_private_data struct. Data
 * stored here is used for mapping physical GPIO pins to SoC pins. */
struct gpio_map {
	gpio_pin en;
	gpio_pin en2;
	gpio_pin rs;
	gpio_pin d7;
	gpio_pin d6;
	gpio_pin d5;
	gpio_pin d4;
	gpio_pin backlight;
};

#endif				// HD_LCDGPIO_H

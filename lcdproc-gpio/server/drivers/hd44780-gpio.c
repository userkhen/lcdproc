/** \file server/drivers/hd44780-gpio.c
 * \c gpio connection type of \c hd44780 driver for Hitachi HD44780 based LCD
 * displays connected to the GPIO pins on the SoC boards.
 *
 * The LCD is operated in its 4 bit-mode. R/W (5) on the LCD MUST be hard wired
 * low to prevent 5V logic appearing on the GPIO pins.
 *
 * The default connections are:
 * header(GPIO)	  LCD
 * P1-12 (18)	  D7 (14)
 * P1-16 (23)	  D6 (13)
 * P1-18 (24)	  D5 (12)
 * P1-22 (25)	  D4 (11)
 * P1-24 (8)	  EN (6)
 * GND		  R/W (5)
 * P1-26 (7)	  RS (4)
 * P1-15 (22)	  EN2 (second controller, optional)
 * P1-11 (17)	  BL (backlight optional)
 *
 * Mappings can be set in the config file using the key-words:
 * pin_EN, pin_EN2, pin_RS, pin_D7, pin_D6, pin_D5, pin_D4, pin_BL
 * in the [HD44780] section.
 */

/*-
 * Copyright (c) 2012-2013 Paul Corner <paul_c@users.sourceforge.net>
 *                    2013 warhog <warhog@gmx.de> (Conditional ARM test)
 *                    2013 Serac <Raspberry Pi forum>
 *                               (Backlight & Rev2 board support)
 *
 * The code to access the GPIO on a Raspberry Pi draws on an example program
 * by Dom and Gert van Loo from:
 * http://elinux.org/Rpi_Low-level_peripherals#GPIO_Driving_Example_.28C.29
 *
 * This file is released under the GNU General Public License. Refer to the
 * COPYING file distributed with this package.
 */

/* Default GPIO pin assignment */
#define GPIO_DEF_D7  18
#define GPIO_DEF_D6  23
#define GPIO_DEF_D5  24
#define GPIO_DEF_D4  25
#define GPIO_DEF_RS   7
#define GPIO_DEF_EN   8
#define GPIO_DEF_EN2 22
#define GPIO_DEF_BL  17

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include "hd44780-gpio.h"
#include "hd44780-low.h"
#include "report.h"

void lcdgpio_HD44780_senddata(PrivateData *p, unsigned char displayID, unsigned char flags, unsigned char ch);
void lcdgpio_HD44780_backlight(PrivateData *p, unsigned char state);
void lcdgpio_HD44780_close(PrivateData *p);

static const char *gpio_prefix = "/sys/class/gpio/";

/**
 * Configures a GPIO pin: Disable pull-up/down and set it up as output.
 * \param drvthis  Pointer to driver structure.
 * \param gpio     Number of the GPIO pin to use.
 */
static int
setup_gpio_pin(Driver *drvthis, gpio_pin *gpio)
{
	FILE *fp;
	char filename[256];

	sprintf(filename, "%sexport", gpio_prefix);
	fp = fopen(filename, "w");

	if(NULL == fp)
    {
	    report(RPT_ERR, "Couldn't open %s", filename);;
        return -1;
    }

	fprintf(fp, "%d\n",gpio->num);
	fclose(fp);

	sprintf(filename, "%sgpio%d/direction", gpio_prefix, gpio->num);
	fp = fopen(filename, "w");
	if (NULL == fp)
	{
		printf("Couldn't open %s", filename);
	}

	fprintf(fp, "low");
	fclose(fp);

	sprintf(filename, "%sgpio%d/value", gpio_prefix, gpio->num);
	fp = fopen(filename, "w");
	if (NULL == fp)                                                                                           
    {
		printf("Couldn't open %s", filename);
	}

	gpio->fd = fp;

	return 0;	
}

static void                                                                                                   
close_gpio_pin(gpio_pin *gpio)                                                                   
{    
    FILE *fp;                                                                                                 
    char filename[256];                                                                                       

    sprintf(filename, "%sgpio%d/direction", gpio_prefix, gpio->num);                                          
    fp = fopen(filename, "w");                                                                                

    fprintf(fp, "in");                                                                                       
    fclose(fp);

    if( NULL != gpio->fd)
    {
        fclose(gpio->fd);
    }
	
    sprintf(filename, "%sunexport", gpio_prefix);                                                               
    fp = fopen(filename, "w");                                                                                
                                                                                                              
    fprintf(fp, "%d\n",gpio->num);                                                                            
    fclose(fp);

}



/**
 * Free resources used by this connection type.
 * \param p  Pointer to driver's PrivateData structure.
 */
void
lcdgpio_HD44780_close(PrivateData *p)
{
	/* Configure all pins as input */
	close_gpio_pin(&(p->gpio_pinout->en));
	close_gpio_pin(&(p->gpio_pinout->rs));
	close_gpio_pin(&(p->gpio_pinout->d7));
	close_gpio_pin(&(p->gpio_pinout->d6));
	close_gpio_pin(&(p->gpio_pinout->d5));
	close_gpio_pin(&(p->gpio_pinout->d4));

	if (p->have_backlight) 
            close_gpio_pin(&(p->gpio_pinout->backlight));
	if (p->numDisplays > 1)
		close_gpio_pin(&(p->gpio_pinout->en2));

	/* Unmap and free memory */
	if (p->gpio_pinout != NULL)
		free(p->gpio_pinout);
	p->gpio_pinout = NULL;
}

/**
 * Initialize the driver.
 * \param drvthis  Pointer to driver structure.
 * \retval 0       Success.
 * \retval -1      Error.
 */
int
hd_init_gpio(Driver *drvthis)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;
	
	/* Get GPIO configuration */
	p->gpio_pinout = malloc(sizeof(struct gpio_map));
	if (p->gpio_pinout == NULL) {
		report(RPT_ERR, "hd_init_gpio: unable to allocate memory");
		return -1;
	}

	p->gpio_pinout->en.num = drvthis->config_get_int(drvthis->name, "pin_EN", 0, GPIO_DEF_EN);
	p->gpio_pinout->rs.num = drvthis->config_get_int(drvthis->name, "pin_RS", 0, GPIO_DEF_RS);
	p->gpio_pinout->d7.num = drvthis->config_get_int(drvthis->name, "pin_D7", 0, GPIO_DEF_D7);
	p->gpio_pinout->d6.num = drvthis->config_get_int(drvthis->name, "pin_D6", 0, GPIO_DEF_D6);
	p->gpio_pinout->d5.num = drvthis->config_get_int(drvthis->name, "pin_D5", 0, GPIO_DEF_D5);
	p->gpio_pinout->d4.num = drvthis->config_get_int(drvthis->name, "pin_D4", 0, GPIO_DEF_D4);

	debug(RPT_INFO, "hd_init_gpio: Pin EN mapped to GPIO%d", p->gpio_pinout->en.num);
	debug(RPT_INFO, "hd_init_gpio: Pin RS mapped to GPIO%d", p->gpio_pinout->rs.num);
	debug(RPT_INFO, "hd_init_gpio: Pin D4 mapped to GPIO%d", p->gpio_pinout->d4.num);
	debug(RPT_INFO, "hd_init_gpio: Pin D5 mapped to GPIO%d", p->gpio_pinout->d5.num);
	debug(RPT_INFO, "hd_init_gpio: Pin D6 mapped to GPIO%d", p->gpio_pinout->d6.num);
	debug(RPT_INFO, "hd_init_gpio: Pin D7 mapped to GPIO%d", p->gpio_pinout->d7.num);

	if (p->numDisplays > 1) /* For displays with two controllers */
	{
		p->gpio_pinout->en2.num = drvthis->config_get_int(drvthis->name, "pin_EN2", 0, GPIO_DEF_EN2);
		debug(RPT_INFO, "hd_init_gpio: Pin EN2 mapped to GPIO%d", p->gpio_pinout->en2.num);
	}

	if (p->have_backlight)	/* Backlight setup is optional */
	{
		p->backlight_bit = drvthis->config_get_int(drvthis->name, "pin_BL", 0, GPIO_DEF_BL);
		p->gpio_pinout->backlight.num = p->backlight_bit;
		debug(RPT_INFO, "hd_init_gpio: Backlight mapped to GPIO%d", p->gpio_pinout->backlight.num);
	}

	if (setup_gpio_pin(drvthis, &(p->gpio_pinout->en)) < 0) {
		report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as EN", p->gpio_pinout->en.num);
		return -1;
	}

	if (setup_gpio_pin(drvthis, &(p->gpio_pinout->rs)) < 0) {
		report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as RS", p->gpio_pinout->rs.num);
		return -1;
	}
	if (setup_gpio_pin(drvthis, &(p->gpio_pinout->d7)) < 0) {
		report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as D7", p->gpio_pinout->d7.num);
		return -1;
	}
	if (setup_gpio_pin(drvthis, &(p->gpio_pinout->d6)) < 0) {
		report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as D6", p->gpio_pinout->d6.num);
		return -1;
	}
	if (setup_gpio_pin(drvthis, &(p->gpio_pinout->d5)) < 0) {
		report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as D5", p->gpio_pinout->d5.num);
		return -1;
	}
	if (setup_gpio_pin(drvthis, &(p->gpio_pinout->d4)) < 0) {
		report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as D4", p->gpio_pinout->d4.num);
		return -1;
	}

	p->hd44780_functions->senddata = lcdgpio_HD44780_senddata;
	p->hd44780_functions->close = lcdgpio_HD44780_close;

	if (p->have_backlight) {
		if (setup_gpio_pin(drvthis, &(p->gpio_pinout->backlight)) < 0) {
			report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as D5", p->gpio_pinout->backlight.num);
			return -1;
		}
		p->hd44780_functions->backlight = lcdgpio_HD44780_backlight;
	}

	if (p->numDisplays > 1) {
		if (setup_gpio_pin(drvthis, &(p->gpio_pinout->en2)) < 0) {
			report(RPT_ERR, "hd_init_gpio: Failed to set up GPIO %d as EN2", p->gpio_pinout->en2.num);
			return -1;
		}
	}

	/* Setup the lcd in 4 bit mode: Send (FUNCSET | IF_8BIT) three times
	 * followed by (FUNCSET | IF_4BIT) using four nibbles. Timing is not
	 * exactly what is required by HD44780. */
	p->hd44780_functions->senddata(p, 0, RS_INSTR, 0x33);
	p->hd44780_functions->uPause(p, 4100);
	p->hd44780_functions->senddata(p, 0, RS_INSTR, 0x32 );
	p->hd44780_functions->uPause(p, 150);

	common_init(p, IF_4BIT);

	return 0;
}

void SET_GPIO( gpio_pin *pin, uint value)
{
	if(value)
	{	
		fprintf(pin->fd, "1");
	}
	else
	{
		fprintf(pin->fd, "0");
	}
	fseek(pin->fd, 0, SEEK_SET);
}

/**
 * Send data or commands to the display.
 * \param p          Pointer to driver's private data structure.
 * \param displayID  ID of the display (or 0 for all) to send data to.
 * \param flags      Defines whether to end a command or data.
 * \param ch         The value to send.
 */
void
lcdgpio_HD44780_senddata(PrivateData *p, unsigned char displayID, unsigned char flags, unsigned char ch)
{
	if (flags == RS_INSTR) {
		SET_GPIO(&(p->gpio_pinout->rs), 0);
	}
	else {			/* flags == RS_DATA */
		SET_GPIO(&(p->gpio_pinout->rs), 1);
	}
	/* Clear data lines ready for nibbles */
	SET_GPIO(&(p->gpio_pinout->d7), 0);
	SET_GPIO(&(p->gpio_pinout->d6), 0);
	SET_GPIO(&(p->gpio_pinout->d5), 0);
	SET_GPIO(&(p->gpio_pinout->d4), 0);
	p->hd44780_functions->uPause(p, 50);

	/* Output upper nibble first */
	SET_GPIO(&(p->gpio_pinout->d7), (ch & 0x80));
	SET_GPIO(&(p->gpio_pinout->d6), (ch & 0x40));
	SET_GPIO(&(p->gpio_pinout->d5), (ch & 0x20));
	SET_GPIO(&(p->gpio_pinout->d4), (ch & 0x10));
	p->hd44780_functions->uPause(p, 50);

	/* Data is clocked on the falling edge of EN */
	if (displayID == 1 || displayID == 0)
		SET_GPIO(&(p->gpio_pinout->en), 1);
	if (displayID == 2 || (p->numDisplays > 1 && displayID == 0))
		SET_GPIO(&(p->gpio_pinout->en2), 1);
	p->hd44780_functions->uPause(p, 50);

	if (displayID == 1 || displayID == 0)
		SET_GPIO(&(p->gpio_pinout->en), 0);
	if (displayID == 2 || (p->numDisplays > 1 && displayID == 0))
		SET_GPIO(&(p->gpio_pinout->en2), 0);
	p->hd44780_functions->uPause(p, 50);

	/* Do same for lower nibble */
	SET_GPIO(&(p->gpio_pinout->d7), 0);
	SET_GPIO(&(p->gpio_pinout->d6), 0);
	SET_GPIO(&(p->gpio_pinout->d5), 0);
	SET_GPIO(&(p->gpio_pinout->d4), 0);
	p->hd44780_functions->uPause(p, 50);

	SET_GPIO(&(p->gpio_pinout->d7), (ch & 0x08));
	SET_GPIO(&(p->gpio_pinout->d6), (ch & 0x04));
	SET_GPIO(&(p->gpio_pinout->d5), (ch & 0x02));
	SET_GPIO(&(p->gpio_pinout->d4), (ch & 0x01));
	p->hd44780_functions->uPause(p, 50);

	if (displayID == 1 || displayID == 0)
		SET_GPIO(&(p->gpio_pinout->en), 1);
	if (displayID == 2 || (p->numDisplays > 1 && displayID == 0))
		SET_GPIO(&(p->gpio_pinout->en2), 1);
	p->hd44780_functions->uPause(p, 50);

	if (displayID == 1 || displayID == 0)
		SET_GPIO(&(p->gpio_pinout->en), 0);
	if (displayID == 2 || (p->numDisplays > 1 && displayID == 0))
		SET_GPIO(&(p->gpio_pinout->en2), 0);
	p->hd44780_functions->uPause(p, 50);
}


/**
 * Turn display backlight on or off.
 * \param p      Pointer to driver's private data structure.
 * \param state  New backlight status.
 */
void
lcdgpio_HD44780_backlight(PrivateData *p, unsigned char state)
{
	if (p->backlight_bit > -1 && p->backlight_bit < 32)
		SET_GPIO(&(p->gpio_pinout->backlight), (state == BACKLIGHT_ON) ? 1 : 0);
}

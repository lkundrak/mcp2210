/*
 * MCP2210 USB SPI bridge library
 * Copyright (C) 2016  Lubomir Rintel <lkundrak@v3.sk>
 *
 * Implements the protocol and functionality described here:
 * http://ww1.microchip.com/downloads/en/DeviceDoc/22288A.pdf
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

#ifndef __MCP2210_H
#define __MCP2210_H

#include <string.h>
#include <unistd.h>

#define MCP2210_PACKET_SIZE		64
#define MCP2210_SPI_TX_MAX		65535
#define MCP2210_SPI_CHUNK		58
#define MCP2210_GPIO_PINS		8
#define MCP2210_USB_STRING		58
#define MCP2210_PASSWORD_LEN		8

#define MCP2210_STATUS_GET		0x10
#define MCP2210_STATUS_SPI_OWNER_NONE	0x00
#define MCP2210_STATUS_SPI_OWNER_US	0x01
#define MCP2210_STATUS_SPI_OWNER_USB	0x01
#define MCP2210_STATUS_SPI_OWNER_EXT	0x02

#define MCP2210_SPI_CANCEL		0x11
#define MCP2210_GP6_COUNT_GET		0x12

#define MCP2210_CHIP_GET		0x20
#define MCP2210_CHIP_SET		0x21
#define MCP2210_CHIP_PIN_GPIO		0x00
#define MCP2210_CHIP_PIN_CS		0x01
#define MCP2210_CHIP_PIN_FUNC		0x02
#define MCP2210_CHIP_GP6_CNT_HI_PULSE	0x4
#define MCP2210_CHIP_GP6_CNT_LO_PULSE	0x3
#define MCP2210_CHIP_GP6_CNT_UP_EDGE	0x2
#define MCP2210_CHIP_GP6_CNT_DN_EDGE	0x1
#define MCP2210_CHIP_GP6_CNT_NONE	0x0
#define MCP2210_CHIP_PROTECT_NONE	0x00
#define MCP2210_CHIP_PROTECT_PASSWD	0x40
#define MCP2210_CHIP_PROTECT_LOCKED	0x80

#define MCP2210_GPIO_VAL_SET		0x30
#define MCP2210_GPIO_VAL_GET		0x31
#define MCP2210_GPIO_DIR_SET		0x32
#define MCP2210_GPIO_DIR_GET		0x33

#define MCP2210_SPI_SET			0x40
#define MCP2210_SPI_GET			0x41
#define MCP2210_SPI_TRANSFER		0x42
#define MCP2210_SPI_END			0x10
#define MCP2210_SPI_STARTED		0x20
#define MCP2210_SPI_DATA		0x30

#define MCP2210_EEPROM_READ		0x50
#define MCP2210_EEPROM_WRITE		0x51

#define MCP2210_NVRAM_SET		0x60
#define MCP2210_NVRAM_GET		0x61
#define MCP2210_NVRAM_PARAM_SPI		0x10
#define MCP2210_NVRAM_PARAM_CHIP	0x20
#define MCP2210_NVRAM_PARAM_USB_KEY	0x30
#define MCP2210_NVRAM_PARAM_PRODUCT	0x40
#define MCP2210_NVRAM_PARAM_MANUFACT	0x50

#define MCP2210_SEND_PASSWORD		0x70

#define MCP2210_GP7_SPI_RELEASE		0x80

/* Device error codes.  */

#define MCP2210_ESPIBUSY		0xf7
#define MCP2210_ESPIINPROGRESS		0xf8
#define MCP2210_ENOCMD			0xf9
#define MCP2210_EWRFAIL			0xfa
#define MCP2210_ELOCKED			0xfb
#define MCP2210_ENOACCESS		0xfc
#define MCP2210_ECONDACCESS		0xfd

/* Library error codes.  */

#define MCP2210_EWRSHORT		0x101
#define MCP2210_ERDSHORT		0x102
#define MCP2210_EBADCMD			0x103
#define MCP2210_EBADSUBCMD		0x104
#define MCP2210_EBADADDR		0x105
#define MCP2210_EBADTXSTAT		0x106

typedef unsigned char mcp2210_packet[MCP2210_PACKET_SIZE];

const char *mcp2210_strerror (int mcp2210_errno);
int mcp2210_command (int fd, mcp2210_packet packet, unsigned short command);
int mcp2210_subcommand (int fd, mcp2210_packet packet, unsigned short command, unsigned short subcommand);
int mcp2210_read_eeprom (int fd, mcp2210_packet packet, unsigned short addr);
int mcp2210_write_eeprom (int fd, mcp2210_packet packet, unsigned short addr, unsigned short val);
int mcp2210_unlock_eeprom (int fd, mcp2210_packet packet, const char *passwd);
int mcp2210_gp6_count_get (int fd, mcp2210_packet packet, unsigned short no_reset);
int mcp2210_spi_transfer (int fd, mcp2210_packet spi_packet, char *data, short len);

/*
 * mcp2210_command() wrappers that do some extra bits if necessary, such as set
 * the command code or clean the structure for reading.
 */

static inline int
mcp2210_get_command (int fd, mcp2210_packet packet, unsigned short command)
{
	memset (packet, 0, MCP2210_PACKET_SIZE);
	return mcp2210_command (fd, packet, command);
}

static inline int
mcp2210_get_nvram (int fd, mcp2210_packet packet, unsigned short subcommand)
{
	memset (packet, 0, MCP2210_PACKET_SIZE);
	return mcp2210_subcommand (fd, packet, MCP2210_NVRAM_GET, subcommand);
}

static inline int
mcp2210_set_nvram (int fd, mcp2210_packet packet, unsigned short subcommand)
{
	return mcp2210_subcommand (fd, packet, MCP2210_NVRAM_SET, subcommand);
}

/*
 * Utility functions for getting information from Status packets (section 3.6).
 * Issue a MCP2210_STATUS_GET command to fill the packet buffer.
 */

static inline unsigned short
mcp2210_status_no_ext_request (mcp2210_packet status_packet)
{
	return status_packet[2];
}

static inline unsigned short
mcp2210_status_bus_owner (mcp2210_packet status_packet)
{
	return status_packet[3];
}

static inline unsigned short
mcp2210_status_password_count (mcp2210_packet status_packet)
{
	return status_packet[4];
}

static inline unsigned short
mcp2210_status_password_guessed (mcp2210_packet status_packet)
{
	return status_packet[5];
}

/*
 * Utilities to read and change the chip settings packets.
 * Read and change runtime chip settings with MCP2210_CHIP_GET and
 * MCP2210_CHIP_SET commands. The power-up settings are read and saved
 * using MCP2210_NVRAM_PARAM_CHIP sub-command of MCP2210_NVRAM_SET and
 * MCP2210_NVRAM_GET.
 */

static inline unsigned short
mcp2210_chip_get_function (mcp2210_packet chip_packet, unsigned short pin)
{
	return chip_packet[4 + pin];
}

static inline void
mcp2210_chip_set_function (mcp2210_packet chip_packet, unsigned short pin, unsigned short func)
{
	chip_packet[4 + pin] = func;
}

static inline unsigned short
mcp2210_chip_get_default_output (mcp2210_packet chip_packet, unsigned short pin)
{
	return ((chip_packet[14] << 8 | chip_packet[13]) >> pin) & 1;
}

static inline void
mcp2210_chip_set_default_output (mcp2210_packet chip_packet, unsigned short pin, unsigned short val)
{
	if (pin < 8) {
		if (val) {
			chip_packet[13] |= 1 << pin;
		} else {
			chip_packet[13] &= ~(1 << pin);
		}
	} else {
		if (val) {
			chip_packet[14] |= 1 << (pin - 8);
		} else {
			chip_packet[14] &= ~(1 << (pin - 8));
		}
	}
}

static inline unsigned short
mcp2210_chip_get_default_direction (mcp2210_packet chip_packet, unsigned short pin)
{
	return ((chip_packet[16] << 8 | chip_packet[15]) >> pin) & 1;
}

static inline void
mcp2210_chip_set_default_direction (mcp2210_packet chip_packet, unsigned short pin, unsigned short val)
{
	if (pin < 8) {
		if (val) {
			chip_packet[15] |= 1 << pin;
		} else {
			chip_packet[15] &= ~(1 << pin);
		}
	} else {
		if (val) {
			chip_packet[16] |= 1 << (pin - 8);
		} else {
			chip_packet[16] &= ~(1 << (pin - 8));
		}
	}
}

static inline unsigned short
mcp2210_chip_get_wakeup (mcp2210_packet chip_packet)
{
	return !!(chip_packet[17] & 0x10);
}

static inline void
mcp2210_chip_set_wakeup (mcp2210_packet chip_packet, unsigned short enabled)
{
	chip_packet[17] &= ~0x10;
	chip_packet[17] |= !!enabled << 4;
}

static inline unsigned short
mcp2210_chip_get_gp6_mode (mcp2210_packet chip_packet)
{
	return (chip_packet[17] >> 1) & 7;
}

static inline void
mcp2210_chip_set_gp6_mode (mcp2210_packet chip_packet, unsigned short mode)
{
	chip_packet[17] &= ~0xe;
	chip_packet[17] |= mode << 1;
}

static inline unsigned short
mcp2210_chip_get_no_spi_release (mcp2210_packet chip_packet)
{
	return !!(chip_packet[17] & 0x1);
}

static inline void
mcp2210_chip_set_no_spi_release (mcp2210_packet chip_packet, unsigned short no_release)
{
	chip_packet[17] &= ~0x1;
	chip_packet[17] |= !!no_release;
}

static inline unsigned short
mcp2210_chip_get_access_control (mcp2210_packet chip_packet)
{
	return chip_packet[18];
}

static inline void
mcp2210_chip_set_access_control (mcp2210_packet chip_packet, unsigned short setting)
{
	chip_packet[18] = setting;
}

static inline void
mcp2210_chip_set_access_password (mcp2210_packet chip_packet, const char *passwd)
{
	chip_packet[19] = passwd[0];
	chip_packet[20] = passwd[1];
	chip_packet[21] = passwd[2];
	chip_packet[22] = passwd[3];
	chip_packet[23] = passwd[4];
	chip_packet[24] = passwd[5];
	chip_packet[25] = passwd[6];
	chip_packet[26] = passwd[7];
}

/*
 * GPIO pin packet utility functions. These can be used to access both the pin
 * value and pin directions. Use with MCP2210_GPIO_VAL_SET, MCP2210_GPIO_VAL_GET,
 * MCP2210_GPIO_DIR_SET and MCP2210_GPIO_DIR_GET respectively.
 */

static inline unsigned short
mcp2210_gpio_get_pin (mcp2210_packet gpio_packet, unsigned short pin)
{
	return ((gpio_packet[5] << 8 | gpio_packet[4]) >> pin) & 1;
}

static inline void
mcp2210_gpio_set_pin (mcp2210_packet gpio_packet, unsigned short pin, unsigned short val)
{
	if (pin < 8) {
		if (val) {
			gpio_packet[4] |= 1 << pin;
		} else {
			gpio_packet[4] &= ~(1 << pin);
		}
	} else {
		if (val) {
			gpio_packet[5] |= 1 << (pin - 8);
		} else {
			gpio_packet[5] &= ~(1 << (pin - 8));
		}
	}
}

/*
 * SPI settings functions. Use with MCP2210_SPI_SET and MCP2210_SPI_GET
 * for runtime changes, and MCP2210_NVRAM_PARAM_SPI subcommand of
 * MCP2210_NVRAM_SET and MCP2210_NVRAM_GET for power-on defaults.
 */

static inline long
mcp2210_spi_get_bitrate (mcp2210_packet spi_packet)
{
	return spi_packet[4] | (spi_packet[5] << 8) |
		(spi_packet[6] << 16) | (spi_packet[7] << 24);
}

static inline void
mcp2210_spi_set_bitrate (mcp2210_packet spi_packet, long bitrate)
{
	spi_packet[4] = bitrate & 0xff;
	spi_packet[5] = (bitrate >> 8) & 0xff;
	spi_packet[6] = (bitrate >> 16)& 0xff;
	spi_packet[7] = (bitrate >> 24) & 0xff;
}

static inline unsigned short
mcp2210_spi_get_pin_active_cs (mcp2210_packet spi_packet, unsigned short pin)
{
	return ((spi_packet[9] << 8 | spi_packet[8]) >> pin) & 1;
}

static inline void
mcp2210_spi_set_pin_active_cs (mcp2210_packet spi_packet, unsigned short pin, unsigned short val)
{
	if (pin < 8) {
		if (val) {
			spi_packet[8] |= 1 << pin;
		} else {
			spi_packet[8] &= ~(1 << pin);
		}
	} else {
		if (val) {
			spi_packet[9] |= 1 << (pin - 8);
		} else {
			spi_packet[9] &= ~(1 << (pin - 8));
		}
	}
}

static inline unsigned short
mcp2210_spi_get_pin_idle_cs (mcp2210_packet spi_packet, unsigned short pin)
{
	return ((spi_packet[11] << 8 | spi_packet[10]) >> pin) & 1;
}

static inline void
mcp2210_spi_set_pin_idle_cs (mcp2210_packet spi_packet, unsigned short pin, unsigned short val)
{
	if (pin < 8) {
		if (val) {
			spi_packet[10] |= 1 << pin;
		} else {
			spi_packet[10] &= ~(1 << pin);
		}
	} else {
		if (val) {
			spi_packet[11] |= 1 << (pin - 8);
		} else {
			spi_packet[11] &= ~(1 << (pin - 8));
		}
	}
}

static inline unsigned int
mcp2210_spi_get_cs_data_delay_100us (mcp2210_packet spi_packet)
{
	return spi_packet[12] | spi_packet[13] << 8;
}

static inline void
mcp2210_spi_set_cs_data_delay_100us (mcp2210_packet spi_packet, unsigned int delay_100us)
{
	spi_packet[12] = delay_100us & 0xff;
	spi_packet[13] = (delay_100us >> 8) & 0xff;
}

static inline unsigned int
mcp2210_spi_get_data_cs_delay_100us (mcp2210_packet spi_packet)
{
	return spi_packet[14] | spi_packet[15] << 8;
}

static inline void
mcp2210_spi_set_data_cs_delay_100us (mcp2210_packet spi_packet, unsigned int delay_100us)
{
	spi_packet[14] = delay_100us & 0xff;
	spi_packet[15] = (delay_100us >> 8) & 0xff;
}

static inline unsigned int
mcp2210_spi_get_byte_delay_100us (mcp2210_packet spi_packet)
{
	return spi_packet[16] | spi_packet[17] << 8;
}

static inline void
mcp2210_spi_set_byte_delay_100us (mcp2210_packet spi_packet, unsigned int delay_100us)
{
	spi_packet[16] = delay_100us & 0xff;
	spi_packet[17] = (delay_100us >> 8) & 0xff;
}

static inline unsigned int
mcp2210_spi_get_transaction_size (mcp2210_packet spi_packet)
{
	return spi_packet[18] | spi_packet[19] << 8;
}

static inline void
mcp2210_spi_set_transaction_size (mcp2210_packet spi_packet, unsigned int size)
{
	spi_packet[18] = size & 0xff;
	spi_packet[19] = (size >> 8) & 0xff;
}

static inline unsigned short
mcp2210_spi_get_mode (mcp2210_packet spi_packet)
{
	return spi_packet[20];
}

static inline void
mcp2210_spi_set_mode (mcp2210_packet spi_packet, unsigned short mode)
{
	spi_packet[20] = mode;
}

/*
 * USB key functions. Use with MCP2210_NVRAM_PARAM_USB_KEY sub-command
 * of MCP2210_NVRAM_SET and MCP2210_NVRAM_GET to access the values.
 */

static inline unsigned short
mcp2210_usb_key_get_vid (mcp2210_packet usb_key_packet)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_SET)
		return usb_key_packet[4] | (usb_key_packet[5] << 8);
	return usb_key_packet[12] | (usb_key_packet[13] << 8);
}

static inline void
mcp2210_usb_key_set_vid (mcp2210_packet usb_key_packet, unsigned short vid)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_GET) {
		usb_key_packet[12] = vid & 0xff;
		usb_key_packet[13] = (vid >> 8) & 0xff;
	} else {
		usb_key_packet[4] = vid & 0xff;
		usb_key_packet[5] = (vid >> 8) & 0xff;
	}
}

static inline unsigned short
mcp2210_usb_key_get_pid (mcp2210_packet usb_key_packet)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_SET)
		return usb_key_packet[6] | (usb_key_packet[7] << 8);
	return usb_key_packet[14] | (usb_key_packet[15] << 8);
}

static inline void
mcp2210_usb_key_set_pid (mcp2210_packet usb_key_packet, unsigned short pid)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_GET) {
		usb_key_packet[14] = pid & 0xff;
		usb_key_packet[15] = (pid >> 8) & 0xff;
	} else {
		usb_key_packet[6] = pid & 0xff;
		usb_key_packet[7] = (pid >> 8) & 0xff;
	}
}

static inline unsigned short
mcp2210_usb_key_get_host_powered (mcp2210_packet usb_key_packet)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_SET)
		return !!(usb_key_packet[8] & 0x80);
	return !!(usb_key_packet[29] & 0x80);
}

static inline void
mcp2210_usb_key_set_host_powered (mcp2210_packet usb_key_packet, unsigned short on)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_GET) {
		usb_key_packet[29] &= ~0x80;
		usb_key_packet[29] |= (!!on) << 7;
	} else {
		usb_key_packet[8] &= ~0x80;
		usb_key_packet[8] |= (!!on) << 7;
	}
}

static inline unsigned short
mcp2210_usb_key_get_self_powered (mcp2210_packet usb_key_packet)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_SET)
		return !!(usb_key_packet[8] & 0x40);
	return !!(usb_key_packet[29] & 0x40);
}

static inline void
mcp2210_usb_key_set_self_powered (mcp2210_packet usb_key_packet, unsigned short on)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_GET) {
		usb_key_packet[29] &= ~0x40;
		usb_key_packet[29] |= (!!on) << 6;
	} else {
		usb_key_packet[8] &= ~0x40;
		usb_key_packet[8] |= (!!on) << 6;
	}
}

static inline unsigned short
mcp2210_usb_key_get_remote_wakeup (mcp2210_packet usb_key_packet)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_SET)
		return !!(usb_key_packet[8] & 0x20);
	return !!(usb_key_packet[29] & 0x20);
}

static inline void
mcp2210_usb_key_set_remote_wakeup (mcp2210_packet usb_key_packet, unsigned short on)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_GET) {
		usb_key_packet[29] &= ~0x20;
		usb_key_packet[29] |= (!!on) << 5;
	} else {
		usb_key_packet[8] &= ~0x20;
		usb_key_packet[8] |= (!!on) << 5;
	}
}

static inline unsigned short
mcp2210_usb_key_get_current_2ma (mcp2210_packet usb_key_packet)
{
	if (usb_key_packet[0] == MCP2210_NVRAM_SET)
		return usb_key_packet[9];
	return usb_key_packet[30];
}

static inline void
mcp2210_usb_key_set_current_2ma (mcp2210_packet usb_key_packet, unsigned short current)
{
	usb_key_packet[usb_key_packet[0] == MCP2210_NVRAM_GET ? 30 : 9] = current;
}

static inline void
mcp2210_usb_key_get_to_set (mcp2210_packet get, mcp2210_packet set)
{
	set[0] = MCP2210_NVRAM_SET;
	set[1] = get[1];
	set[4] = get[12];
	set[5] = get[13];
	set[6] = get[14];
	set[7] = get[15];
	set[8] = get[29];
	set[9] = get[30];
}

/*
 * USB string descriptor utilities. These return length and contents of
 * 16-bit Unicode strings in Product and Manufacturer descriptors. Use with
 * MCP2210_NVRAM_PARAM_PRODUCT and MCP2210_NVRAM_PARAM_MANUFACT sub-commands
 * of MCP2210_NVRAM_SET and MCP2210_NVRAM_GET.
 */

static inline char *
mcp2210_usb_string_get (mcp2210_packet packet)
{
	return (char *)&packet[6];
}

static inline unsigned short
mcp2210_usb_string_get_len (mcp2210_packet packet)
{
	return packet[4] - 2;
}

static inline void
mcp2210_usb_string_set (mcp2210_packet packet, const char *string, short len)
{
	memcpy (&packet[6], string, len);
	packet[4] = len + 2;
}

#endif /* defined(__MCP2210_H) */

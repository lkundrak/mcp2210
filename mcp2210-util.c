/*
 * MCP2210 USB SPI bridge utility
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

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

#include "mcp2210.h"

unsigned short gpio_val_mod = 0;
unsigned short gpio_dir_mod = 0;
unsigned short nvram_chip_mod = 0;
unsigned short nvram_manufact_mod = 0;
unsigned short nvram_product_mod = 0;
unsigned short nvram_spi_mod = 0;
unsigned short nvram_usb_key_mod = 0;
unsigned short spi_mod = 0;
unsigned short chip_mod = 0;
unsigned short spi_tx_len = 0;

mcp2210_packet gpio_val_packet = { 0, };
mcp2210_packet gpio_dir_packet = { 0, };
mcp2210_packet status_packet = { 0, };
mcp2210_packet nvram_chip_packet = { 0, };
mcp2210_packet nvram_manufact_packet = { 0, };
mcp2210_packet nvram_product_packet = { 0, };
mcp2210_packet nvram_spi_packet = { 0, };
mcp2210_packet nvram_usb_key_packet = { 0, };
mcp2210_packet spi_packet = { 0, };
mcp2210_packet chip_packet = { 0, };

char spi_tx[MCP2210_SPI_TX_MAX];

static void
print_in_out (int i)
{
	printf ("%s", i ? "in" : "out");
}

static inline void
maybe_get (int fd, mcp2210_packet packet, unsigned short command)
{
	int ret;

	if (packet[0])
		return;
	ret = mcp2210_command (fd, packet, command);
	if (ret < 0) {
		fprintf (stderr, "Error reading from the device: %s\n",
			mcp2210_strerror (ret));
		exit (1);
	}
}

static inline void
maybe_get_nvram (int fd, mcp2210_packet packet, unsigned short subcommand)
{
	int ret;

	if (packet[0])
		return;
	ret = mcp2210_subcommand (fd, packet, MCP2210_NVRAM_GET, subcommand);
	if (ret < 0) {
		fprintf (stderr, "Error reading NVRAM: %s\n",
			mcp2210_strerror (ret));
		exit (1);
	}
}

void
hex_dump (const char *data, int len)
{
	int i;

	printf ("      ");
	for (i = 0; i < 16; i++)
		printf ("%02x ", i);
	for (i = 0; i < len; i++) {
		if (i % 16 == 0)
			printf ("\n%04x:", i);
		printf (" %02x", data[i]);
	}
	putchar ('\n');
}

void
status_dump (mcp2210_packet packet)
{
	printf ("External SPI bus request: %s\n",
		mcp2210_status_no_ext_request (packet) ? "no" : "yes");
	printf ("Current SPI bus owner: ");
	switch (mcp2210_status_bus_owner (packet)) {
	case MCP2210_STATUS_SPI_OWNER_NONE:
		printf ("none\n");
		break;
	case MCP2210_STATUS_SPI_OWNER_US:
		printf ("USB\n");
		break;
	case MCP2210_STATUS_SPI_OWNER_EXT:
		printf ("external\n");
		break;
	default:
		printf ("unknown\n");
		break;
	}
	printf ("Attempted password accesses: %d\n",
		mcp2210_status_password_count (packet));
	printf ("Password guessed: %s\n",
		mcp2210_status_password_guessed (packet) ? "yes" : "no");
}

void
chip_dump (mcp2210_packet packet)
{
	int i;

	printf ("Pin designation: ");
	for (i = MCP2210_GPIO_PINS; i >= 0; i--) {
		switch (mcp2210_chip_get_function (packet, i)) {
		case MCP2210_CHIP_PIN_GPIO:
			printf ("gpio");
			break;
		case MCP2210_CHIP_PIN_CS:
			printf ("cs");
			break;
		case MCP2210_CHIP_PIN_FUNC:
			printf ("func");
			break;
		default:
			printf ("unknown");
			break;
		}
		putchar (i ? ' ' : '\n');
	}

	printf ("Default pin output: ");
	for (i = MCP2210_GPIO_PINS; i >= 0; i--)
		putchar (mcp2210_chip_get_default_output (packet, i) ? '1' : '0');
	printf ("\n");

	printf ("Default pin direction: ");
	for (i = MCP2210_GPIO_PINS; i >= 0; i--) {
		print_in_out (mcp2210_chip_get_default_direction (packet, i));
		putchar (i ? ' ' : '\n');
	}

	printf ("Remote wake-up: %s\n", mcp2210_chip_get_wakeup (packet) ? "enabled" : "disabled");

	printf ("GP6 count mode: ");
	switch (mcp2210_chip_get_gp6_mode (packet)) {
	case MCP2210_CHIP_GP6_CNT_HI_PULSE:
		printf ("high pulses");
		break;
	case MCP2210_CHIP_GP6_CNT_LO_PULSE:
		printf ("low pulses");
		break;
	case MCP2210_CHIP_GP6_CNT_UP_EDGE:
		printf ("rising edges");
		break;
	case MCP2210_CHIP_GP6_CNT_DN_EDGE:
		printf ("falling edges");
		break;
	case MCP2210_CHIP_GP6_CNT_NONE:
		printf ("none");
		break;
	default:
		printf ("unknown");
		break;
	}
	putchar ('\n');

	printf ("Release bus between transfers: %s\n", mcp2210_chip_get_no_spi_release (packet) ? "no" : "yes");

	printf ("Settings access control: ");
	switch (mcp2210_chip_get_access_control (packet)) {
	case MCP2210_CHIP_PROTECT_NONE:
		printf ("not protected");
		break;
	case MCP2210_CHIP_PROTECT_PASSWD:
		printf ("protected with password");
		break;
	case MCP2210_CHIP_PROTECT_LOCKED:
		printf ("permanently locked");
		break;
	default:
		printf ("unknown");
		break;
	}
	putchar ('\n');
}

void
gpio_dump (mcp2210_packet packet)
{
	int i;

	for (i = MCP2210_GPIO_PINS; i >= 0; i--)
		putchar (mcp2210_gpio_get_pin (packet, i) ? '1' : '0');
	printf ("\n");
}

void
spi_dump (mcp2210_packet packet)
{
	int i;

	printf ("SPI bit rate: %ld\n", mcp2210_spi_get_bitrate (packet));
	printf ("Active CS: ");
	for (i = MCP2210_GPIO_PINS; i >= 0; i--)
		putchar (mcp2210_spi_get_pin_active_cs (packet, i) ? '1' : '0');
	printf ("\n");
	printf ("Idle CS: ");
	for (i = MCP2210_GPIO_PINS; i >= 0; i--)
		putchar (mcp2210_spi_get_pin_idle_cs (packet, i) ? '1' : '0');
	printf ("\n");
	printf ("CS to data delay: %d us\n", mcp2210_spi_get_cs_data_delay_100us (packet) * 100);
	printf ("Data to CS delay: %d us\n", mcp2210_spi_get_data_cs_delay_100us (packet) * 100);
	printf ("Delay between bytes: %d us\n", mcp2210_spi_get_byte_delay_100us (packet) * 100);
	printf ("Transaction size: %d B\n", mcp2210_spi_get_transaction_size (packet));
	printf ("SPI mode: %d\n", mcp2210_spi_get_mode (packet));
}

void
usb_key_dump (mcp2210_packet packet)
{
	printf ("Vendor ID: 0x%04x\n", mcp2210_usb_key_get_vid (packet));
	printf ("Product ID: 0x%04x\n", mcp2210_usb_key_get_pid (packet));
	printf ("USB host-powered: %s\n", mcp2210_usb_key_get_host_powered (packet) ? "yes" : "no");
	printf ("USB self-powered: %s\n", mcp2210_usb_key_get_self_powered (packet) ? "yes" : "no");
	printf ("USB remote wake-up capable: %s\n", mcp2210_usb_key_get_remote_wakeup (packet) ? "yes" : "no");
	printf ("USB host current amount: %d mA\n", mcp2210_usb_key_get_current_2ma (packet) * 2);
}

void
usb_string_dump (mcp2210_packet packet)
{
	int i;
	const char *string = mcp2210_usb_string_get (packet);

	for (i = 0; i < mcp2210_usb_string_get_len (packet); i += 2) {
		if (string[i + 1]) {
			printf ("\\u%02x%02x", string[i + 1], string[i]);
		} else {
			putchar (string[i]);
		}
	}
	putchar ('\n');
}

/*********************************************************************/

void
dump_eeprom (int fd)
{
	mcp2210_packet packet;
	int i;
	int b;

	printf ("EEPROM dump:\n\n");
	for (i = 0; i <= 0xff; i++) {
		b = mcp2210_read_eeprom (fd, packet, i);
		if (b < 0) {
			fprintf (stderr, "Error reading NVRAM byte %02x: %s\n",
				i, mcp2210_strerror (b));
			exit (1);
		}
		printf ("%02x%c", b, (i + 1) % 16 ? ' ' : '\n');
	}
}

void
dump_status (int fd)
{
	printf ("Runtime status:\n\n");
	maybe_get (fd, status_packet, MCP2210_STATUS_GET);
	status_dump (status_packet);

}
void
dump_runtime_spi (int fd)
{
	printf ("Runtime SPI settings:\n\n");
	maybe_get (fd, spi_packet, MCP2210_SPI_GET);
	spi_dump (spi_packet);
}

void
dump_runtime_gpio (int fd)
{
	printf ("Runtime GPIO values: ");
	maybe_get (fd, gpio_val_packet, MCP2210_GPIO_VAL_GET);
	gpio_dump (gpio_val_packet);

	printf ("Runtime GPIO directions: ");
	maybe_get (fd, gpio_dir_packet, MCP2210_GPIO_DIR_GET);
	gpio_dump (gpio_dir_packet);
}

void
dump_runtime_chip (int fd)
{
	printf ("Runtime chip settings:\n\n");
	maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
	chip_dump (chip_packet);
}

void
dump_nvram_spi (int fd)
{
	printf ("NVRAM SPI settings:\n\n");
	maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
	spi_dump (nvram_spi_packet);
}

void
dump_nvram_chip (int fd)
{
	printf ("NVRAM chip settings:\n\n");
	maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
	chip_dump (nvram_chip_packet);
}

void
dump_nvram_usb (int fd)
{
	printf ("NVRAM USB key settings:\n\n");
	maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
	usb_key_dump (nvram_usb_key_packet);

	printf ("\nNVRAM USB product: ");
	maybe_get_nvram (fd, nvram_product_packet, MCP2210_NVRAM_PARAM_PRODUCT);
	usb_string_dump (nvram_product_packet);

	printf ("NVRAM USB manufacturer: ");
	maybe_get_nvram (fd, nvram_manufact_packet, MCP2210_NVRAM_PARAM_MANUFACT);
	usb_string_dump (nvram_manufact_packet);
}

void
dump_runtime (int fd)
{
	dump_status (fd);
	putchar ('\n');
	dump_runtime_spi (fd);
	putchar ('\n');
	dump_runtime_gpio (fd);
	putchar ('\n');
	dump_runtime_chip (fd);
}

void
dump_nvram (int fd)
{
	dump_nvram_spi (fd);
	putchar ('\n');
	dump_nvram_chip (fd);
	putchar ('\n');
	dump_nvram_usb (fd);
}

void
dump_all (int fd)
{
	dump_runtime (fd);
	putchar ('\n');
	dump_nvram (fd);
	putchar ('\n');
	dump_eeprom (fd);
}

/*********************************************************************/

long long
get_num (int argc, char *argv[], int i)
{
	long long num;

	if (i + 1 >= argc) {
		fprintf (stderr, "Missing numeric argument to '%s'\n", argv[i]);
		exit (1);
	}

	if (sscanf (argv[i + 1], strncmp("0x", argv[i + 1], 2) ? "%lld" : "0x%llx", &num) == 1)
		return num;

	fprintf (stderr, "Failed to parse numeric argument to '%s': '%s'\n", argv[i], argv[i + 1]);
	exit (1);
}

unsigned short
get_pin (int argc, char *argv[], int i)
{
	long long pin;

	pin = get_num (argc, argv, i);
	if (pin < 0 || pin > MCP2210_GPIO_PINS) {
		fprintf (stderr, "Pin number for '%s' out of range (0 - 8): '%lld'\n", argv[i], pin);
		exit (1);
	}

	return pin;
}

unsigned long
get_bitrate (int argc, char *argv[], int i)
{
	long long rate;

	rate = get_num (argc, argv, i);
	if (rate < 1464 || rate > 12000000) {
		fprintf (stderr, "Bit rate out of range (1464 - 12000000): '%lld'\n", rate);
		exit (1);
	}

	return rate;
}

unsigned int
get_delay (int argc, char *argv[], int i)
{
	long long delay;

	delay = get_num (argc, argv, i);
	if (delay % 100) {
		fprintf (stderr, "Microsecond delay for '%s' not a multiple of 100 us: '%lld'\n",
			argv[i], delay);
		exit (1);
	}
	if (delay < 0 || delay > 0xffff) {
		fprintf (stderr, "Microsecond for '%s' out of range: '%lld'\n", argv[i], delay);
		exit (1);
	}

	return delay / 100;
}

unsigned int
get_tx_size (int argc, char *argv[], int i)
{
	long long size;

	size = get_num (argc, argv, i);
	if (size < 1 || size > 0xffff) {
		fprintf (stderr, "Invalid transaction size (1 - 4): '%lld'\n", size);
		exit (1);
	}

	return size;
}

unsigned short
get_spi_mode (int argc, char *argv[], int i)
{
	long long mode;

	mode = get_num (argc, argv, i);
	if (mode < 0 || mode > 4) {
		fprintf (stderr, "Invalid SPI mode (1 - 4): '%lld'\n", mode);
		exit (1);
	}

	return mode;
}

unsigned int
get_usb_id (int argc, char *argv[], int i)
{
	long long id;

	id = get_num (argc, argv, i);
	if (id < 0 || id > 0xffff) {
		fprintf (stderr, "Invalid USB ID: '0x%04llx'\n", id);
		exit (1);
	}

	return id;
}

unsigned short
get_current (int argc, char *argv[], int i)
{
	long long current;

	current = get_num (argc, argv, i);
	if (current & 1) {
		fprintf (stderr, "Current amount is not a multiple of 2 mA: '%lld'\n", current);
		exit (1);
	}
	if (current < 0 || current > 0x1ff) {
		fprintf (stderr, "Requested current is out of range: '%lld'\n", current);
		exit (1);
	}

	return current / 2;
}

unsigned short
get_usb_string (int argc, char *argv[], int i, char string[])
{
	char *p;
	short c = 0;

	for (p = argv[i + 1]; *p; p++) {
		if (c >= MCP2210_USB_STRING) {
			fprintf (stderr, "Parameter to '%s' too long.\n", argv[i]);
			exit (1);
		}
		if (*p == '\\') {
			p++;
			if (*p == 'u') {
				int hi, lo;
				p++;
				if (sscanf (p, "%02x%02x", &hi, &lo) != 2) {
					fprintf (stderr, "Invalid '\\u' sequence "
						"in parameter to '%s'.\n", argv[i]);
					exit (1);
				}
				string[c++] = lo;
				string[c++] = hi;
				p += 3;
				continue;
			} else if (*p != '\\') {
				fprintf (stderr, "Invalid character following '\\' "
					"in parameter to '%s'.\n", argv[i]);
				exit (1);
			}
		}
		string[c++] = *p;
		string[c++] = '\0';
	}

	return c;
}

int
get_string (int argc, char *argv[], int i, char string[], int len)
{
	char *p;
	int c = 0;

	for (p = argv[i + 1]; *p; p++) {
		if (c >= len) {
			fprintf (stderr, "Parameter to '%s' too long.\n", argv[i]);
			exit (1);
		}
		if (*p == '\\') {
			p++;
			if (*p == 'x') {
				int ch;
				p++;
				if (sscanf (p, "%02x", &ch) != 1) {
					fprintf (stderr, "Invalid '\\x' sequence "
						"in parameter to '%s'.\n", argv[i]);
					exit (1);
				}
				string[c++] = ch;
				p++;
				continue;
			} else if (*p != '\\') {
				fprintf (stderr, "Invalid character following '\\' "
					"in parameter to '%s'.\n", argv[i]);
				exit (1);
			}
		}
		string[c++] = *p;
	}

	while (c < len)
		string[--len] = '\0';

	return c;
}

int
main (int argc, char *argv[])
{
	int fd;
	int i;
	unsigned short runtime = 1;
	unsigned short nvram = 0;
	int ret;

	if (argc < 3) {
		fprintf (stderr, "Usage: %s /dev/hidraw<n> option [option ...]\n", argv[0]);
		return 1;
	}

	fd = open (argv[1], O_RDWR);
	if (fd == -1) {
		perror (argv[1]);
		return 1;
	}

	for (i = 2; i < argc; i++) {
		if (strcmp (argv[i], "--runtime") == 0) {
			runtime = 1;
			nvram = 0;
		} else if (strcmp (argv[i], "--nvram") == 0) {
			runtime = 0;
			nvram = 1;
		} else if (strcmp (argv[i], "--both") == 0) {
			runtime = nvram = 1;
		} else if (strcmp (argv[i], "--dump-all") == 0) {
			dump_all (fd);
		} else if (strcmp (argv[i], "--dump-nvram") == 0) {
			dump_nvram (fd);
		} else if (strcmp (argv[i], "--dump-nvram-usb") == 0) {
			dump_nvram_usb (fd);
		} else if (strcmp (argv[i], "--dump-status") == 0) {
			dump_status (fd);
		} else if (strcmp (argv[i], "--dump-runtime") == 0) {
			dump_runtime (fd);
		} else if (strcmp (argv[i], "--dump-runtime-gpio") == 0) {
			dump_runtime_gpio (fd);
		} else if (strcmp (argv[i], "--dump-spi") == 0) {
			if (runtime)
				dump_runtime_spi (fd);
			if (runtime && nvram)
				putchar ('\n');
			if (nvram)
				dump_nvram_spi (fd);
		} else if (strcmp (argv[i], "--dump-chip") == 0) {
			if (runtime)
				dump_runtime_chip (fd);
			if (runtime && nvram)
				putchar ('\n');
			if (nvram)
				dump_nvram_chip (fd);
		} else if (strcmp (argv[i], "--dump-eeprom") == 0) {
			dump_eeprom (fd);
		} else if (strcmp (argv[i], "--on") == 0) {
			short pin = get_pin (argc, argv, i++);

			maybe_get (fd, gpio_val_packet, MCP2210_GPIO_VAL_GET);
			gpio_val_mod = 1;
			mcp2210_gpio_set_pin (gpio_val_packet, pin, 1);
		} else if (strcmp (argv[i], "--off") == 0) {
			short pin = get_pin (argc, argv, i++);

			maybe_get (fd, gpio_val_packet, MCP2210_GPIO_VAL_GET);
			gpio_val_mod = 1;
			mcp2210_gpio_set_pin (gpio_val_packet, pin, 0);
		} else if (strcmp (argv[i], "--out") == 0) {
			short pin = get_pin (argc, argv, i++);

			maybe_get (fd, gpio_dir_packet, MCP2210_GPIO_DIR_GET);
			gpio_dir_mod = 1;
			mcp2210_gpio_set_pin (gpio_dir_packet, pin, 0);
		} else if (strcmp (argv[i], "--in") == 0) {
			short pin = get_pin (argc, argv, i++);

			maybe_get (fd, gpio_dir_packet, MCP2210_GPIO_DIR_GET);
			gpio_dir_mod = 1;
			mcp2210_gpio_set_pin (gpio_dir_packet, pin, 1);
		} else if (strcmp (argv[i], "--val") == 0) {
			short pin = get_pin (argc, argv, i++);

			maybe_get (fd, gpio_val_packet, MCP2210_GPIO_VAL_GET);
			putchar (mcp2210_gpio_get_pin (gpio_val_packet, pin) ? '1' : '0');
			putchar ('\n');
		} else if (strcmp (argv[i], "--dir") == 0) {
			short pin = get_pin (argc, argv, i++);

			maybe_get (fd, gpio_dir_packet, MCP2210_GPIO_DIR_GET);
			print_in_out (mcp2210_gpio_get_pin (gpio_dir_packet, pin));
			putchar ('\n');
		} else if (strcmp (argv[i], "--gpio") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_function (chip_packet, pin, MCP2210_CHIP_PIN_GPIO);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_function (nvram_chip_packet, pin, MCP2210_CHIP_PIN_GPIO);
			}
		} else if (strcmp (argv[i], "--cs") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_function (chip_packet, pin, MCP2210_CHIP_PIN_CS);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_function (nvram_chip_packet, pin, MCP2210_CHIP_PIN_CS);
			}
		} else if (strcmp (argv[i], "--func") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_function (chip_packet, pin, MCP2210_CHIP_PIN_FUNC);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_function (nvram_chip_packet, pin, MCP2210_CHIP_PIN_FUNC);
			}
		} else if (strcmp (argv[i], "--default-on") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_default_output (chip_packet, pin, 1);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_default_output (nvram_chip_packet, pin, 1);
			}
		} else if (strcmp (argv[i], "--default-off") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_default_output (chip_packet, pin, 0);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_default_output (nvram_chip_packet, pin, 0);
			}
		} else if (strcmp (argv[i], "--default-val") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				putchar (mcp2210_chip_get_default_output (chip_packet, pin) ? '1' : '0');
			}
			if (runtime && nvram)
				putchar (' ');
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				putchar (mcp2210_chip_get_default_output (nvram_chip_packet, pin) ? '1' : '0');
			}
			putchar ('\n');
		} else if (strcmp (argv[i], "--default-out") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_default_direction (chip_packet, pin, 0);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_default_direction (nvram_chip_packet, pin, 0);
			}
		} else if (strcmp (argv[i], "--default-in") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_default_direction (chip_packet, pin, 1);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_default_direction (nvram_chip_packet, pin, 1);
			}
		} else if (strcmp (argv[i], "--default-dir") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				print_in_out (mcp2210_chip_get_default_direction (chip_packet, pin));
			}
			if (runtime && nvram)
				putchar (' ');
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				print_in_out (mcp2210_chip_get_default_direction (nvram_chip_packet, pin));
			}
			putchar ('\n');
		} else if (strcmp (argv[i], "--gp6-count-high") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_gp6_mode (chip_packet, MCP2210_CHIP_GP6_CNT_HI_PULSE);
			}
			if (nvram) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				nvram_chip_mod = 1;
				mcp2210_chip_set_gp6_mode (nvram_chip_packet, MCP2210_CHIP_GP6_CNT_HI_PULSE);
			}
		} else if (strcmp (argv[i], "--gp6-count-low") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_gp6_mode (chip_packet, MCP2210_CHIP_GP6_CNT_LO_PULSE);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_gp6_mode (nvram_chip_packet, MCP2210_CHIP_GP6_CNT_LO_PULSE);
			}
		} else if (strcmp (argv[i], "--gp6-count-rising") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_gp6_mode (chip_packet, MCP2210_CHIP_GP6_CNT_UP_EDGE);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_gp6_mode (nvram_chip_packet, MCP2210_CHIP_GP6_CNT_UP_EDGE);
			}
		} else if (strcmp (argv[i], "--gp6-count-falling") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_gp6_mode (chip_packet, MCP2210_CHIP_GP6_CNT_DN_EDGE);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_gp6_mode (nvram_chip_packet, MCP2210_CHIP_GP6_CNT_DN_EDGE);
			}
		} else if (strcmp (argv[i], "--usb-wakeup") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_wakeup (chip_packet, 1);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_wakeup (nvram_chip_packet, 1);
			}
		} else if (strcmp (argv[i], "--no-usb-wakeup") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_wakeup (chip_packet, 0);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_wakeup (nvram_chip_packet, 0);
			}
		} else if (strcmp (argv[i], "--spi-release") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_no_spi_release (chip_packet, 0);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_no_spi_release (nvram_chip_packet, 0);
			}
		} else if (strcmp (argv[i], "--no-spi-release") == 0) {
			if (runtime) {
				maybe_get (fd, chip_packet, MCP2210_CHIP_GET);
				chip_mod = 1;
				mcp2210_chip_set_no_spi_release (chip_packet, 1);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_no_spi_release (nvram_chip_packet, 1);
			}
		} else if (strcmp (argv[i], "--lock-none") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_access_control (nvram_chip_packet, MCP2210_CHIP_PROTECT_NONE);
			}
		} else if (strcmp (argv[i], "--lock-password") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_access_control (nvram_chip_packet, MCP2210_CHIP_PROTECT_PASSWD);
			}
		} else if (strcmp (argv[i], "--lock-permanent") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_access_control (nvram_chip_packet, MCP2210_CHIP_PROTECT_LOCKED);
			}
		} else if (strcmp (argv[i], "--password") == 0) {
			char string[MCP2210_PASSWORD_LEN];

			get_string (argc, argv, i++, string, MCP2210_PASSWORD_LEN);
			if (nvram) {
				maybe_get_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
				nvram_chip_mod = 1;
				mcp2210_chip_set_access_password (nvram_chip_packet, string);
			}
		} else if (strcmp (argv[i], "--bit-rate") == 0) {
			int rate = get_bitrate (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_bitrate (spi_packet, rate);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_bitrate (nvram_spi_packet, rate);
			}
		} else if (strcmp (argv[i], "--active-cs-on") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_pin_active_cs (spi_packet, pin, 1);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_pin_active_cs (nvram_spi_packet, pin, 1);
			}
		} else if (strcmp (argv[i], "--active-cs-off") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_pin_active_cs (spi_packet, pin, 0);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_pin_active_cs (nvram_spi_packet, pin, 0);
			}
		} else if (strcmp (argv[i], "--active-cs-val") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				putchar (mcp2210_spi_get_pin_active_cs (spi_packet, pin) ? '1' : '0');
			}
			if (runtime && nvram)
				putchar (' ');
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				putchar (mcp2210_spi_get_pin_active_cs (nvram_spi_packet, pin) ? '1' : '0');
			}
			putchar ('\n');
		} else if (strcmp (argv[i], "--idle-cs-on") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_pin_idle_cs (spi_packet, pin, 1);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_pin_idle_cs (nvram_spi_packet, pin, 1);
			}
		} else if (strcmp (argv[i], "--idle-cs-off") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_pin_idle_cs (spi_packet, pin, 0);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_pin_idle_cs (nvram_spi_packet, pin, 0);
			}
		} else if (strcmp (argv[i], "--idle-cs-val") == 0) {
			short pin = get_pin (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				putchar (mcp2210_spi_get_pin_idle_cs (spi_packet, pin) ? '1' : '0');
			}
			if (runtime && nvram)
				putchar (' ');
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				putchar (mcp2210_spi_get_pin_idle_cs (nvram_spi_packet, pin) ? '1' : '0');
			}
			putchar ('\n');
		} else if (strcmp (argv[i], "--cs-to-data-delay") == 0) {
			int delay = get_delay (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_cs_data_delay_100us (spi_packet, delay);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_cs_data_delay_100us (nvram_spi_packet, delay);
			}
		} else if (strcmp (argv[i], "--data-to-cs-delay") == 0) {
			int delay = get_delay (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_data_cs_delay_100us (spi_packet, delay);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_data_cs_delay_100us (nvram_spi_packet, delay);
			}
		} else if (strcmp (argv[i], "--byte-delay") == 0) {
			int delay = get_delay (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_byte_delay_100us (spi_packet, delay);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_byte_delay_100us (nvram_spi_packet, delay);
			}
		} else if (strcmp (argv[i], "--tx-size") == 0) {
			int size = get_tx_size (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_transaction_size (spi_packet, size);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_transaction_size (nvram_spi_packet, size);
			}
		} else if (strcmp (argv[i], "--spi-mode") == 0) {
			short mode = get_spi_mode (argc, argv, i++);

			if (runtime) {
				maybe_get (fd, spi_packet, MCP2210_SPI_GET);
				spi_mod = 1;
				mcp2210_spi_set_mode (spi_packet, mode);
			}
			if (nvram) {
				maybe_get_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
				nvram_spi_mod = 1;
				mcp2210_spi_set_mode (nvram_spi_packet, mode);
			}
		} else if (strcmp (argv[i], "--vendor-id") == 0) {
			unsigned int id = get_usb_id (argc, argv, i++);

			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_vid (nvram_usb_key_packet, id);
			}
		} else if (strcmp (argv[i], "--product-id") == 0) {
			unsigned int id = get_usb_id (argc, argv, i++);

			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_pid (nvram_usb_key_packet, id);
			}
		} else if (strcmp (argv[i], "--host-powered") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_host_powered (nvram_usb_key_packet, 1);
			}
		} else if (strcmp (argv[i], "--no-host-powered") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_host_powered (nvram_usb_key_packet, 0);
			}
		} else if (strcmp (argv[i], "--self-powered") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_self_powered (nvram_usb_key_packet, 1);
			}
		} else if (strcmp (argv[i], "--no-self-powered") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_self_powered (nvram_usb_key_packet, 0);
			}
		} else if (strcmp (argv[i], "--remote-wakeup") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_remote_wakeup (nvram_usb_key_packet, 1);
			}
		} else if (strcmp (argv[i], "--no-remote-wakeup") == 0) {
			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_remote_wakeup (nvram_usb_key_packet, 0);
			}
		} else if (strcmp (argv[i], "--host-current") == 0) {
			int current = get_current (argc, argv, i++);

			if (nvram) {
				maybe_get_nvram (fd, nvram_usb_key_packet, MCP2210_NVRAM_PARAM_USB_KEY);
				nvram_usb_key_mod = 1;
				mcp2210_usb_key_set_current_2ma (nvram_usb_key_packet, current);
			}
		} else if (strcmp (argv[i], "--usb-manufacturer") == 0) {
			char string[MCP2210_USB_STRING];
			int len = get_usb_string (argc, argv, i++, string);

			if (nvram) {
				maybe_get_nvram (fd, nvram_manufact_packet, MCP2210_NVRAM_PARAM_MANUFACT);
				nvram_manufact_mod = 1;
				mcp2210_usb_string_set (nvram_manufact_packet, string, len);
			}
		} else if (strcmp (argv[i], "--usb-product") == 0) {
			char string[MCP2210_USB_STRING];
			int len = get_usb_string (argc, argv, i++, string);

			if (nvram) {
				maybe_get_nvram (fd, nvram_product_packet, MCP2210_NVRAM_PARAM_PRODUCT);
				nvram_product_mod = 1;
				mcp2210_usb_string_set (nvram_product_packet, string, len);
			}
		} else if (strcmp (argv[i], "--unlock") == 0) {
			char string[MCP2210_PASSWORD_LEN];
			mcp2210_packet packet;

			get_string (argc, argv, i++, string, sizeof (string));
			ret = mcp2210_unlock_eeprom (fd, packet, string);
			if (ret < 0) {
				fprintf (stderr, "Error unlocking device: %s\n", mcp2210_strerror (ret));
				return 1;
			}
		} else if (strcmp (argv[i], "--spi-tx") == 0) {
			spi_tx_len = get_string (argc, argv, i++, spi_tx, sizeof (spi_tx));
			if (!spi_tx_len) {
				fprintf (stderr, "Empty SPI transfer not allowed\n");
				return 1;
			}

			maybe_get (fd, spi_packet, MCP2210_SPI_GET);
			spi_mod = 1;
			mcp2210_spi_set_transaction_size (spi_packet, spi_tx_len);
		} else if (strcmp (argv[i], "--spi-cancel") == 0) {
			mcp2210_packet packet = { 0, };

			ret = mcp2210_command (fd, packet, MCP2210_SPI_CANCEL);
			if (ret < 0) {
				fprintf (stderr, "Error cancelling SPI transfer: %s\n", mcp2210_strerror (ret));
				return 1;
			}
		} else {
			fprintf (stderr, "Unknown option: '%s'\n", argv[i]);
			return 1;
		}
	}

	if (gpio_val_mod) {
		ret = mcp2210_command (fd, gpio_val_packet, MCP2210_GPIO_VAL_SET);
		if (ret < 0)
			goto err;
	}

	if (gpio_dir_mod) {
		ret = mcp2210_command (fd, gpio_dir_packet, MCP2210_GPIO_DIR_SET);
		if (ret < 0)
			goto err;
	}

	if (chip_mod) {
		ret = mcp2210_command (fd, chip_packet, MCP2210_CHIP_SET);
		if (ret < 0)
			goto err;
	}

	if (nvram_chip_mod) {
		ret = mcp2210_set_nvram (fd, nvram_chip_packet, MCP2210_NVRAM_PARAM_CHIP);
		if (ret < 0)
			goto err;
	}

	if (spi_mod) {
		ret = mcp2210_command (fd, spi_packet, MCP2210_SPI_SET);
		if (ret < 0)
			goto err;
	}

	if (nvram_spi_mod) {
		ret = mcp2210_set_nvram (fd, nvram_spi_packet, MCP2210_NVRAM_PARAM_SPI);
		if (ret < 0)
			goto err;
	}

	if (nvram_usb_key_mod) {
		mcp2210_packet set = { 0, };
		mcp2210_usb_key_get_to_set (nvram_usb_key_packet, set);
		ret = mcp2210_set_nvram (fd, set, MCP2210_NVRAM_PARAM_USB_KEY);
		if (ret < 0)
			goto err;
	}

	if (nvram_manufact_mod) {
		ret = mcp2210_set_nvram (fd, nvram_manufact_packet, MCP2210_NVRAM_PARAM_MANUFACT);
		if (ret < 0)
			goto err;
	}

	if (nvram_product_mod) {
		ret = mcp2210_set_nvram (fd, nvram_product_packet, MCP2210_NVRAM_PARAM_PRODUCT);
		if (ret < 0)
			goto err;
	}

	if (spi_tx_len) {
		ret = mcp2210_spi_transfer (fd, spi_packet, spi_tx, spi_tx_len);
		if (ret < 0) {
			fprintf (stderr, "SPI transaction error: %s\n", mcp2210_strerror (ret));
			return 1;
		}
		hex_dump (spi_tx, spi_tx_len);
	}

	return 0;

err:
	fprintf (stderr, "Error writing to the device: %s\n", mcp2210_strerror (ret));
	return 1;
}

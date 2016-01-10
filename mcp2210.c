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

#include <errno.h>
#include <string.h>
#include <time.h>

#include "mcp2210.h"

const char *
mcp2210_strerror (int mcp2210_errno)
{
	if (mcp2210_errno >= -1)
		return strerror (errno);

	switch (-mcp2210_errno) {
	case MCP2210_ESPIBUSY:
		return "External master controls the SPI bus";
	case MCP2210_ESPIINPROGRESS:
		return "SPI transfer already in progress";
	case MCP2210_ENOCMD:
		return "No such command";
	case MCP2210_EWRFAIL:
		return "EEPROM write failed";
	case MCP2210_ELOCKED:
		return "EEPROM is locked";
	case MCP2210_ENOACCESS:
		return "Access rejected";
	case MCP2210_ECONDACCESS:
		return "Bad password";
	case MCP2210_EWRSHORT:
		return "Short write";
	case MCP2210_ERDSHORT:
		return "Short read";
	case MCP2210_EBADCMD:
		return "Response command code mismatch";
	case MCP2210_EBADSUBCMD:
		return "Response sub-command code mismatch";
	case MCP2210_EBADADDR:
		return "Response address mismatch";
	case MCP2210_EBADTXSTAT:
		return "Invalid SPI transfer status";
	}

	return "Unknown error";
}

/*
 * Issue a MCP2210 command and read in a response. Fills in the command code,
 * replaces the buffer contents with response and does the error checking.
 * The caller is responsible for supplying the buffer.
 */

int
mcp2210_command (int fd, mcp2210_packet packet, unsigned short command)
{
	packet[0] = command;

	switch (write (fd, packet, MCP2210_PACKET_SIZE)) {
	case MCP2210_PACKET_SIZE:
		break;
	case -1:
		return -1;
	default:
		return -MCP2210_EWRSHORT;
	}

	memset (packet, 0, MCP2210_PACKET_SIZE);

	switch (read (fd, packet, MCP2210_PACKET_SIZE)) {
	case MCP2210_PACKET_SIZE:
		break;
	case -1:
		return -1;
	default:
		return -MCP2210_ERDSHORT;
	}

	if (packet[1] != 0)
		return -packet[1];

	if (packet[0] != command)
		return -MCP2210_EBADCMD;

	return 0;
}

/*
 * Call the a sub-command. Convenience wrapper around mcp2210_command()
 * that does the cleaning if necessary, fills in sub-command and does
 * some sanity checking. Useful with NVRAM access wrappers.
 */

int
mcp2210_subcommand (int fd, mcp2210_packet packet, unsigned short command,
		unsigned short subcommand)
{
	int ret;

	packet[1] = subcommand;

	ret = mcp2210_command (fd, packet, command);
	if (ret < 0)
		return ret;

	if (packet[2] != subcommand)
		return -MCP2210_EBADSUBCMD;

	return 0;
}

/*
 * Internal EEPROM access functions with sanity checking.
 */

int
mcp2210_read_eeprom (int fd, mcp2210_packet packet, unsigned short addr)
{
	int ret;

	memset (packet, 0, MCP2210_PACKET_SIZE);
	packet[1] = addr;

	ret = mcp2210_command (fd, packet, MCP2210_EEPROM_READ);
	if (ret < 0)
		return ret;

	if (packet[2] != addr)
		return -MCP2210_EBADADDR;

	return packet[3];
}

int
mcp2210_write_eeprom (int fd, mcp2210_packet packet, unsigned short addr, unsigned short val)
{
	memset (packet, 0, MCP2210_PACKET_SIZE);
	packet[1] = addr;
	packet[2] = val;

	return mcp2210_command (fd, packet, MCP2210_EEPROM_WRITE);
}

int
mcp2210_unlock_eeprom (int fd, mcp2210_packet packet, const char *passwd)
{
	memset (packet, 0, MCP2210_PACKET_SIZE);
	packet[4] = passwd[0];
	packet[5] = passwd[1];
	packet[6] = passwd[2];
	packet[7] = passwd[3];
	packet[8] = passwd[4];
	packet[9] = passwd[5];
	packet[10] = passwd[6];
	packet[11] = passwd[7];

	return mcp2210_command (fd, packet, MCP2210_SEND_PASSWORD);
}

/*
 * Utility function for reading the GP6 pin counter (section 3.4).
 * Fill in the packet buffer with MCP2210_GP6_COUNT_GET command.
 */

int
mcp2210_gp6_count_get (int fd, mcp2210_packet packet, unsigned short no_reset)
{
	int ret;

	memset (packet, 0, MCP2210_PACKET_SIZE);
	packet[1] = no_reset;
	ret = mcp2210_command (fd, packet, MCP2210_GP6_COUNT_GET);
	if (ret < 0)
		return ret;
	return (packet[5] << 8) | packet[4];
}

int
mcp2210_spi_transfer (int fd, mcp2210_packet spi_packet, char *data, short len)
{
	int rd = 0, wr = 0;
	int bit_rate = mcp2210_spi_get_bitrate (spi_packet);
	int ret;

	while (rd < len) {
		mcp2210_packet packet = { 0, };
		int wr_len = MCP2210_SPI_CHUNK;
		int rd_len = MCP2210_SPI_CHUNK;
		struct timespec delay;

		if (wr + wr_len > len)
			wr_len = len - wr;
		if (rd + rd_len > len)
			rd_len = len - rd;

		delay.tv_sec = rd_len * 8 / bit_rate;
		delay.tv_nsec = (rd_len * 8 % bit_rate) * (1000000000 / bit_rate);
		delay.tv_nsec += rd_len * mcp2210_spi_get_byte_delay_100us (spi_packet) * (100000 + 30000);
		if (wr == 0)
			delay.tv_nsec += mcp2210_spi_get_cs_data_delay_100us (spi_packet) * 100000;
		if (rd + rd_len == len)
			delay.tv_nsec += mcp2210_spi_get_data_cs_delay_100us (spi_packet) * (100000 + 30000);
		delay.tv_sec += delay.tv_nsec / 1000000000;
		delay.tv_nsec %= 1000000000;

retry:
		packet[1] = wr_len;
		memcpy (&packet[2], &data[wr], wr_len);
		ret = mcp2210_command (fd, packet, MCP2210_SPI_TRANSFER);

		nanosleep (&delay, NULL);

		if (ret == -MCP2210_ESPIINPROGRESS) {
			delay.tv_sec = 0;
			delay.tv_nsec = 5000000;
			goto retry;
		} else if (ret < 0) {
			return ret;
		}
		wr += wr_len;

		switch (packet[3]) {
		case MCP2210_SPI_STARTED:
		case MCP2210_SPI_END:
		case MCP2210_SPI_DATA:
			break;
		default:
			return -MCP2210_EBADTXSTAT;
		}

		memcpy (&data[rd], &packet[4], packet[2]);
		rd += packet[2];
	}

	return 0;
}

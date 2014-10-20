/*
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s)
{
	perror(s);
	abort();
}

static const char dev_cs[] = "/dev/spidev0.12"; //SPI_NCSB : PF12  ==> cs
static const char dev_data[] = "/dev/spidev0.3"; //SPI_NCSA : PF3 ==> data
static uint8_t mode;
static uint8_t bits = 8;
static uint32_t speed = 500000;
static uint16_t delay;
static int led[4];
static uint16_t data;

int port;
int sled;
int buf[64];
uint8_t len;
/*

  From hardware-x.y tar ball, cpld dir, README.txt:

  D1 D0 | LED1
  ------|---
  0   0 | off
  0   1 | red
  1   0 | green
  1   1 | off

  An similarly for the other LEDs:

  D[3:2] LED2
  D[5:4] LED3
  D[7:6] LED4

 */
 
//FILE *fd_cs, *fd_data;
int fd_cs, fd_data;

uint8_t pin_cs[1];
uint8_t tx_data[64];
uint8_t rx_data[64];

static int open_csspi( )
{
	int status = -1;
	int ret;
	uint16_t mode;
	
	printf("Open spi cs port %s\n", dev_cs);
	fd_cs = open((char *)dev_cs, O_WRONLY );
	if (fd_cs <= 0) {
		printf("can't open device for chipselect");
	} else  {
		
		
		mode = 0xF800;

		ret = ioctl(fd_cs, SPI_IOC_WR_MODE, &mode);
		if (ret == -1)
			pabort("can't get spi mode");
			
		ret = ioctl(fd_cs, SPI_IOC_RD_MODE, &mode);
		if (ret == -1)
			pabort("can't get spi mode");
		printf("SPI MODE: 0x%04X\n",mode);
	
		status = 0;
	}
	return status;
}

static int open_dataspi( )
{
	int status = -1, ret;
	uint16_t mode;
	printf("Open spi data port %s\n", dev_data);
	fd_data = open((char *)dev_data, O_RDWR);
	if (fd_data <= 0) {
		printf("can't open device for data spi");
	} else {
		
		
		mode = 0xF800;

		ret = ioctl(fd_data, SPI_IOC_WR_MODE, &mode);
		if (ret == -1)
			pabort("can't get spi mode");
	
		ret = ioctl(fd_data, SPI_IOC_RD_MODE, &mode);
		if (ret == -1)
			pabort("can't get spi mode");
		printf("SPI MODE: 0x%04X\n", mode);
		status = 0;
	}
	return status;
}

static int set_led(int port, int state)
{
	int status;
	uint8_t csled = 0x05;
	uint8_t card = 0;
		
	if (port > 4) {
		csled += 0x40;
		card = 1;
	}

	status = write(fd_cs,&csled,1);
	if (status < 0)
		printf("Led select is not working!!!\n");
	else {
		led[card] &= ~(0x3 << ((port-1)*2));
		led[card] |= state << ((port-1)*2); 
		
		status = write(fd_data, led+card,1);
		printf("Led write data[%d] %d, ret is %d!!!\n", card, led[card], status);
		if (status < 0)
			printf("Led write data is not working!!!\n");
	}
	return status;
}

static int enable_spi(int port)
{
	int status;
	
	if (port > 4)
		port += 0x40;
	status = write(fd_cs,&port,1);
	return status;
}

static int write_spi(int port, char *data, uint8_t len)
{
	int i;
	int status = 0;
	
	status = enable_spi(port);
	
	if (status < 0) {
		printf("Enable Spi on Port %d is not working!!!\n", port);
		return (status);
	}
	
	for (i = 0; i < len; i++) {
		status = write(fd_data, (data+i),1);
		if (status < 0) {
			printf("Write Spi Data %d on Port %d is not working!!!\n", i, port);
			return status;
		}
	}
	return i;
}

static int read_spi(int port, char *data, uint8_t len)
{
	int i;
	int status = 0;
	int dummy = 0xff;
	
	status = enable_spi(port);
	
	if (status < 0) {
		printf("Enable Spi on Port %d is not working!!!\n", port);
		return (status);
	}
	write(fd_data,&len,1);
	
	for (i = 0; i < len; i++) {
		write(fd_data, &dummy, 1);
		status = read(fd_data, (data+i),1);
		if (status < 0) {
			return status;
		}
	}
	
	return i;
}

/*
static void transfer(int fd)
{
	int ret;
	uint8_t pin_cs[1];
	uint8_t tx_data[64];
	uint8_t rx_data[64];

	
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = ARRAY_SIZE(tx),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = bits,
	};

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
		pabort("can't send spi message");

	for (ret = 0; ret < ARRAY_SIZE(tx); ret++) {
		if (!(ret % 6))
			puts("");
		printf("%.2X ", rx[ret]);
	}
	puts("");
}
*/

static void print_usage(const char *prog)
{
	printf("Usage: %s [-psrwld]\n", prog);
	puts("  -p --port   port to use where data to be send / read\n"
	     "  -s --led    led state\n"
	     "  -r --read   read length of data from port p\n"
	     "  -w --write   write data to port p\n"
	     "  -l --len   length of data to write\n"
	     "  -d --data   data dec to write\n"
	     );
	exit(1);
}

#define MAX_LEN 64

static int parse_opts(int argc, char *argv[])
{
	int optw = 0;
	int status = -1;;
	char buf[64];
	static const struct option lopts[] = {
			{ "port", 1, NULL, 'p' },
			{ "set",  1, NULL, 's' },
			{ "read", 1, NULL, 'r' },
			{ "write", 1, NULL, 'w' },
			{ "length", 1, NULL, 'l' },
			{ "data", 1, NULL, 'd' },
			{ NULL, 0, 0, 0 }
		};
	int c;
	
	while (1) {	
		printf("Parse prog options\n");
		c = getopt_long(argc, argv, "p:s:r:w:l:d:", lopts, NULL);

		if (c == -1)
			break;
		printf("Parse prog option %c, %s\n", c, optarg);
		
		switch (c) {
		case 'p':
			port = atoi(optarg);
			optw++;
			break;
		case 's':
			sled = atoi(optarg);
			if (port > 0) {
				printf("set led on port %d, state %d\n",port, sled);
				set_led(port, sled);
			} else {
				printf("please set port of the led first\n");
				print_usage(argv[0]);
			}
			break;
		case 'l':
			len = atoi(optarg);
			optw++;
			break;
		case 'd':
			data = atoi(optarg);
			if ((len > 0) && (port > 0)) {
				printf("write data %d on port %d, len %d\n",data, port, len);
				status = write_spi(port, &data, 1);
				if (status < 0)
					printf("Writing to port %d is not working\n", port);
			} else  {
				printf("please specify lenght of data to write\n");
				print_usage(argv[0]);
			}
			break;
		case 'r':
			len = atoi(optarg);
			if (port > 0) {
				printf("read data on port %d, len %d\n",port, len);
				status = read_spi(port, buf, len);
				if (status > 0) {
					buf[status] = 0x0;
					printf ("Read on port %d, len: %d res: %s\n",status, len, buf);
				} else {
					printf("Read on port %d is not working\n", port);
				}
			}	
			break;
		case 'w':
			strcpy(buf, (char *)optarg);
			if ((len > 0) && (port > 0)) {
				buf[len] = 0x0;
				printf("write data %s on port %d, len %d\n", buf, port, len);
				status = write_spi(port, buf, len);
				if (status < 0)
					printf("Writing to port %d is not working\n", port);
			} else  {
				printf("please specify lenght of data to write\n");
				print_usage(argv[0]);
			}
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
	return status;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int i;
	
	printf("run argc %d, argv: ",argc);
	for (i = 0; i < argc-1; i++) 
		printf(" %s, ", argv[i]);
	printf(" %s\n", argv[i]);
	ret = open_csspi();
	
	
		
	if (ret < 0) {
		printf("Open spi cs is not working, err: %d\n", ret);
		return ret;
	}
	
	ret = open_dataspi();
	if (ret < 0) {
		printf("Open spi data is not working, err: %d\n", ret);
		return ret;
	}
	
	ret = parse_opts(argc, argv);
	
	if (fd_data > 0)
		close(fd_data);
	if (fd_cs > 0)
		close(fd_cs);
		
	return ret;
}

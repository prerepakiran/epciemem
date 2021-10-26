/*
 * pcimem.c: Simple program to read/write from/to a pci device from userspace.
 *
 *  Copyright (C) 2021, Satya Pavan Kiran Prerepa (kiran1107@gmail.com)
 *
 *  Copyright (C) 2010, Bill Farrow (bfarrow@beyondelectronics.us)
 *
 *  Based on the devmem2.c code
 *  Copyright (C) 2000, Jan-Derk Bakker (J.D.Bakker@its.tudelft.nl)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
  __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)


#define VERBOSE  1

void usage(char *name)
{
	fprintf(stderr, "\nUsage:\t%s { sysfile } { offset } [ type*count [--fixed/--incr data ] ]\n"
		"\tsys file        : sysfs file for the pci resource to act on\n"
		"\toffset          : offset into pci memory region to act upon\n"
		"\ttype            : access operation type : [b]yte, [h]alfword, [w]ord, [d]ouble-word\n"
		"\t*count          : number of items to read:  w*100 will dump 100 words\n"
		"\t--fixed/--incr  : data pattern to be written\n"
		"\tdata            : data to be written as per pattern\n\n",
		name);
	exit(1);
}

int get_access_width(int access_type)
{
	int type_width;

	switch(access_type) {
		case 'b':
			type_width = 1;
			break;
		case 'h':
			type_width = 2;
			break;
		case 'w':
			type_width = 4;
			break;
		case 'd':
			type_width = 8;
			break;
		default:
			type_width = -1;
			break;
	}
	return type_width;
}

int open_mem_file(char *filename)
{
	int fd;

	fd = open(filename, O_RDWR | O_SYNC);
	if(fd < 0) FATAL;

	return fd;
}

void close_mem_file(int fd)
{
	if (fd < 0) FATAL;
	close(fd);
}

void write_data(void *dst_addr, uint8_t *buffer, int access_width, int count)
{
	int index;
	uint8_t *u8_buf = (uint8_t *)buffer;
	uint16_t *u16_buf = (uint16_t *)buffer;
	uint32_t *u32_buf = (uint32_t *)buffer;
	uint64_t *u64_buf = (uint64_t *)buffer;

	for (index = 0; index < count; index++) {
		if (access_width == 1) {
			((uint8_t *)dst_addr)[index] = u8_buf[index];
		}
		else if (access_width == 2) {
			((uint16_t *)dst_addr)[index] = u16_buf[index];
		}
		else if (access_width == 4) {
			((uint32_t *)dst_addr)[index] = u32_buf[index];
		}
		else if (access_width == 8) {
			((uint64_t *)dst_addr)[index] = u64_buf[index];
		}
	}
}

void read_data(void *src_addr, uint8_t *buffer, int access_width, int count)
{
	int index;
	uint8_t *u8_buf = (uint8_t *)buffer;
	uint16_t *u16_buf = (uint16_t *)buffer;
	uint32_t *u32_buf = (uint32_t *)buffer;
	uint64_t *u64_buf = (uint64_t *)buffer;

	for (index = 0; index < count; index++) {
		if (access_width == 1) {
			u8_buf[index] = ((uint8_t *)src_addr)[index];
		}
		else if (access_width == 2) {
			u16_buf[index] = ((uint16_t *)src_addr)[index];
		}
		else if (access_width == 4) {
			u32_buf[index] = ((uint32_t *)src_addr)[index];
		}
		else if (access_width == 8) {
			u64_buf[index] = ((uint64_t *)src_addr)[index];
		}
	}
}

void *mmap_file(int fd, int map_size, off_t target_base)
{
	void *map_base;

	map_base = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target_base);
	if(map_base == (void *) -1) FATAL;

	return map_base;
}

void dump_data(uint8_t *buf, loff_t offset, int count, int access_width)
{
#if VERBOSE == 1
	int index;

	for (index = 0; index < count; index++)
	{
		if (((index * access_width) % 16) == 0) {
			printf("\n0x%08lX: ", (offset + (index * access_width)));
		}
		if (access_width == 1) {
			printf("0x%02X  ", *(uint8_t *)buf);
		}
		else if (access_width == 2) {
			printf("0x%04X  ", *(uint16_t *)buf);
		}
		else if (access_width == 4) {
			printf("0x%08X  ", *(uint32_t *)buf);
		}
		else if (access_width == 8) {
			printf("0x%016lX  ", *(uint64_t *)buf);
		}

		buf = (uint8_t *)buf + access_width;
	}
	printf("\n");
#endif
}

enum write_type {
	FIXED = 0,
	INCR,
};

void fill_data(uint8_t *buf, enum write_type wr_type,
	unsigned long writeval, int access_width, int count)
{
	int index;

	if (wr_type == FIXED) {
		for (index = 0; index < count; index++) {
			memcpy(buf, &writeval, access_width);
			buf = (uint8_t *)buf + access_width;
		}
	}
	else if (wr_type == INCR) {
		for (index = 0; index < count; index++) {
			memcpy(buf, &writeval, access_width);
			writeval = writeval + 1;
			buf = (uint8_t *)buf + access_width;
		}
	}
}

int main(int argc, char **argv)
{
	char *filename;
	off_t offset, offset_base;
	int access_type = 'w';
	int access_width;
	int count = 1;
	int len;
	unsigned long writeval;
	bool is_write = false;
	int fd;
	void *rd_buf, *wr_buf;
	int map_size = 4096UL;
	void *map_base, *virt_addr;
	enum write_type wr_type;
	int index;
	int rv;


	switch(argc)
	{
		case 6:
			if (strcmp(argv[4], "--fixed") == 0) {
				wr_type = FIXED;
			}
			else if (strcmp(argv[4], "--incr") == 0) {
				wr_type = INCR;
			}
			writeval = strtoull(argv[5], 0, 0);
			is_write = true;
		case 4:
			access_type = tolower(argv[3][0]);
			if (argv[3][1] == '*') {
				count = strtoul(argv[3]+2, 0, 0);
			}
		case 3:
			offset = strtoul(argv[2], 0, 0);
			filename = argv[1];
			break;
		default:
			usage(basename(argv[0]));
	}

	//printf("Args : offset : 0x%lX, access_type : %c, count : %d\n", offset, access_type, count);

	access_width = get_access_width(access_type);
	len = access_width * count;

	fd = open_mem_file(filename);

	offset_base = offset & ~(sysconf(_SC_PAGE_SIZE)-1);
	if ((offset + (count * access_width) - offset_base) > map_size) {
		map_size = offset + (count * access_width) - offset_base;
	}

	map_base = mmap_file(fd, map_size, offset_base);

	virt_addr = map_base + (offset - offset_base);

	rd_buf = malloc(len);
	if (rd_buf == NULL) FATAL;

#if VERBOSE == 1
	if (is_write) {
		printf("\n------------------------------------------\n");
		printf("Reading the original contents before write\n");
		printf("------------------------------------------\n");
	}
	else {
		printf("\n------------------------------------------\n");
		printf("Reading the contents\n");
		printf("------------------------------------------\n");
	}
#endif
	memset(rd_buf, 0, len);
	read_data(virt_addr, rd_buf, access_width, count);
	dump_data(rd_buf, offset, count, access_width);
	if (is_write) {
		wr_buf = malloc(len);
		if (wr_buf == NULL) FATAL;
		fill_data(wr_buf, wr_type, writeval, access_width, count);
		write_data(virt_addr, wr_buf, access_width, count);

		//printf("Data Written : Pattern : %s, Data : 0x%lX, count : %d\n", (wr_type == FIXED) ? "--fixed" : "--incr", writeval, count);

#if VERBOSE == 1
		if (is_write) {
			printf("\n------------------------------------------\n");
			printf("Read back the data after write\n");
			printf("------------------------------------------\n");
		}
#endif

		memset(rd_buf, 0, len);
		read_data(virt_addr, rd_buf, access_width, count);
		dump_data(rd_buf, offset, count, access_width);

		rv = memcmp(wr_buf, rd_buf, len);
		if (rv != 0) {
			printf("------------------------------------------\n");
			printf("Data Mismatch\n");
			printf("------------------------------------------\n");
		}
		else {
			printf("------------------------------------------\n");
			printf("Successful Data match\n");
			printf("------------------------------------------\n");
		}
		free(wr_buf);
	}

	free(rd_buf);

	close_mem_file(fd);

	return 0;
}
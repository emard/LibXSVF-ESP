/*
 *  Lib(X)SVF  -  A library for implementing SVF and XSVF JTAG players
 *
 *  Copyright (C) 2009  RIEGL Research ForschungsGmbH
 *  Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>
 *  
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <Arduino.h>

#include "libxsvf.h"


// arduino C++ constants "INPUT", "OUTPUT" are not passed to C
// so we (re)define them here. Those constants are
// different for ESP8266 and ESP32

#if ESP8266
#define TCK 14 // NodeMCU D5
#define TMS  5 // NodeMCU D1
#define TDI 13 // NodeMCU D7
#define TDO 12 // NodeMCU D6
//#define INPUT 0
//#define OUTPUT 1
static volatile uint32_t *outport, *inport; // assume one port contains all out pins
static uint32_t tckpinmask, tmspinmask, tdipinmask, tdopinmask;
#endif

#if ESP32
#define TCK 18
#define TMS 21
#define TDI 23
#define TDO 19
//#define INPUT 1
//#define OUTPUT 2
static volatile uint32_t *outport, *inport; // assume one port contains all out pins
static uint32_t tckpinmask, tmspinmask, tdipinmask, tdopinmask;
#define PORT_DIRECT 1
#endif

/** BEGIN: Low-Level I/O Implementation **/

static void io_setup(void)
{
  pinMode(TCK, OUTPUT);
  pinMode(TMS, OUTPUT);
  pinMode(TDI, OUTPUT);
  pinMode(TDO, INPUT);
  outport = portOutputRegister(digitalPinToPort(TCK));
  tckpinmask = digitalPinToBitMask(TCK);
  tmspinmask = digitalPinToBitMask(TMS);
  tdipinmask = digitalPinToBitMask(TDI);
  inport = portInputRegister(digitalPinToPort(TDO));
  tdopinmask = digitalPinToBitMask(TDO);
}

static void io_shutdown(void)
{
  pinMode(TCK, INPUT);
  pinMode(TMS, INPUT);
  pinMode(TDO, INPUT);
  pinMode(TDI, INPUT);
}

static inline void io_tms(int val)
{
  #if PORT_DIRECT
  if(val)
    *outport |= tmspinmask;
  else
    *outport &= ~tmspinmask;
  #else
  digitalWrite(TMS, val);
  #endif
}

static inline void io_tdi(int val)
{
  #if PORT_DIRECT
  if(val)
    *outport |= tdipinmask;
  else
    *outport &= ~tdipinmask;
  #else
  digitalWrite(TDI, val);
  #endif
}

static inline void io_tck(int val)
{
  #if PORT_DIRECT
  if(val)
    *outport |= tckpinmask;
  else
    *outport &= ~tckpinmask;
  #else
  digitalWrite(TCK, val);
  #endif
}

static inline void io_sck(int val)
{
}

static inline void io_trst(int val)
{
}

static inline int io_tdo()
{
  #if PORT_DIRECT
  return tdopinmask & *inport ? 1 : 0;
  #else
  return digitalRead(TDO);
  #endif
}

/** END: Low-Level I/O Implementation **/

struct udata_s {
	int (*file_getbyte)();
	int line;
	int verbose;
	int clockcount;
	int bitcount_tdi;
	int bitcount_tdo;
	int retval_i;
	int retval[256];
	char report[256];
	uint32_t idcode;
};

static int h_setup(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 2) {
		printf("[SETUP]\n");
	}
	io_setup();
	return 0;
}

static int h_shutdown(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 2) {
		printf("[SHUTDOWN]\n");
	}
	io_shutdown();
	return 0;
}

static void h_udelay(struct libxsvf_host *h, long usecs, int tms, long num_tck)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 3) {
		printf("[DELAY:%ld, TMS:%d, NUM_TCK:%ld]\n", usecs, tms, num_tck);
	}
	if (num_tck > 0) {
		struct timeval tv1, tv2;
		gettimeofday(&tv1, NULL);
		io_tms(tms);
		while (num_tck > 0) {
			io_tck(0);
			io_tck(1);
			num_tck--;
		}
		gettimeofday(&tv2, NULL);
		if (tv2.tv_sec > tv1.tv_sec) {
			usecs -= (1000000 - tv1.tv_usec) + (tv2.tv_sec - tv1.tv_sec - 1) * 1000000;
			tv1.tv_usec = 0;
		}
		usecs -= tv2.tv_usec - tv1.tv_usec;
		if (u->verbose >= 3) {
			printf("[DELAY_AFTER_TCK:%ld]\n", usecs > 0 ? usecs : 0);
		}
	}
	if (usecs > 0) {
		delayMicroseconds(usecs);
	}
}

static int h_getbyte(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	int retval = u->file_getbyte();
	if(retval == '\n')
	  u->line++;
	return retval; // returns same as fgetc()
}

static int h_pulse_tck(struct libxsvf_host *h, int tms, int tdi, int tdo, int rmask, int sync)
{
	struct udata_s *u = h->user_data;

	io_tms(tms);

	if (tdi >= 0) {
		u->bitcount_tdi++;
		io_tdi(tdi);
	}

	io_tck(0);
	io_tck(1);

	int line_tdo = io_tdo();
	int rc = line_tdo >= 0 ? line_tdo : 0;

	if (rmask == 1 && u->retval_i < 256)
		u->retval[u->retval_i++] = line_tdo;

	if (tdo >= 0 && line_tdo >= 0) {
		u->bitcount_tdo++;
		if (tdo != line_tdo)
			rc = -1;
	}

	if (u->verbose >= 4) {
		printf("[TMS:%d, TDI:%d, TDO_ARG:%d, TDO_LINE:%d, RMASK:%d, RC:%d]\n", tms, tdi, tdo, line_tdo, rmask, rc);
	}

	u->clockcount++;
	return rc;
}

static void h_pulse_sck(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 4) {
		printf("[SCK]\n");
	}
	io_sck(0);
	io_sck(1);
}

static void h_set_trst(struct libxsvf_host *h, int v)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 4) {
		printf("[TRST:%d]\n", v);
	}
	io_trst(v);
}

static int h_set_frequency(struct libxsvf_host *h, int v)
{
	printf("WARNING: Setting JTAG clock frequency to %d ignored!\n", v);
	return 0;
}

static void h_report_tapstate(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 3) {
		printf("[%s]\n", libxsvf_state2str(h->tap_state));
	}
}

static void h_report_device(struct libxsvf_host *h, unsigned long idcode)
{
	struct udata_s *u = h->user_data;
	u->idcode = idcode;
	printf("idcode=0x%08lx, revision=0x%01lx, part=0x%04lx, manufactor=0x%03lx\n", idcode,
			(idcode >> 28) & 0xf, (idcode >> 12) & 0xffff, (idcode >> 1) & 0x7ff);
}

static void h_report_status(struct libxsvf_host *h, const char *message)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 2) {
		printf("[STATUS] %s\n", message);
	}
}

static void h_report_error(struct libxsvf_host *h, const char *file, int line, const char *message)
{
	struct udata_s *u = h->user_data;
	// snprintf(u->report, 256, "[%s:%d] %s", file, line, message);
	snprintf(u->report, 256, "line %d: %s", u->line, message);
	puts(u->report);
	// printf("[%s:%d] %s\n", file, line, message);
}

static int realloc_maxsize[LIBXSVF_MEM_NUM];

static void *h_realloc(struct libxsvf_host *h, void *ptr, int size, enum libxsvf_mem which)
{
	struct udata_s *u = h->user_data;
	if (size > realloc_maxsize[which])
		realloc_maxsize[which] = size;
	if (u->verbose >= 3) {
		printf("[REALLOC:%s:%d]\n", libxsvf_mem2str(which), size);
	}
	return realloc(ptr, size);
}

static struct udata_s u;

static struct libxsvf_host h = {
	.udelay = h_udelay,
	.setup = h_setup,
	.shutdown = h_shutdown,
	.getbyte = h_getbyte,
	.pulse_tck = h_pulse_tck,
	.pulse_sck = h_pulse_sck,
	.set_trst = h_set_trst,
	.set_frequency = h_set_frequency,
	.report_tapstate = h_report_tapstate,
	.report_device = h_report_device,
	.report_status = h_report_status,
	.report_error = h_report_error,
	.realloc = h_realloc,
	.user_data = &u
};


#if 0
int main(int argc, char **argv)
{
	int rc = 0;
	int gotaction = 0;
	int hex_mode = 0;
	const char *realloc_name = NULL;
	int opt, i, j;

	progname = argc >= 1 ? argv[0] : "xvsftool";
	while ((opt = getopt(argc, argv, "r:vLBx:s:c")) != -1)
	{
		switch (opt)
		{
		case 'r':
			realloc_name = optarg;
			break;
		case 'v':
			copyleft();
			u.verbose++;
			break;
		case 'x':
		case 's':
			gotaction = 1;
			if (u.verbose)
				fprintf(stderr, "Playing %s file `%s'.\n", opt == 's' ? "SVF" : "XSVF", optarg);
			if (!strcmp(optarg, "-"))
				u.f = stdin;
			else
				u.f = fopen(optarg, "rb");
			if (u.f == NULL) {
				fprintf(stderr, "Can't open %s file `%s': %s\n", opt == 's' ? "SVF" : "XSVF", optarg, strerror(errno));
				rc = 1;
				break;
			}
			if (libxsvf_play(&h, opt == 's' ? LIBXSVF_MODE_SVF : LIBXSVF_MODE_XSVF) < 0) {
				fprintf(stderr, "Error while playing %s file `%s'.\n", opt == 's' ? "SVF" : "XSVF", optarg);
				rc = 1;
			}
			if (strcmp(optarg, "-"))
				fclose(u.f);
			break;
		case 'c':
			gotaction = 1;
			if (libxsvf_play(&h, LIBXSVF_MODE_SCAN) < 0) {
				fprintf(stderr, "Error while scanning JTAG chain.\n");
				rc = 1;
			}
			break;
		case 'L':
			hex_mode = 1;
			break;
		case 'B':
			hex_mode = 2;
			break;
		default:
			help();
			break;
		}
	}

	if (!gotaction)
		help();

	if (u.verbose) {
		fprintf(stderr, "Total number of clock cycles: %d\n", u.clockcount);
		fprintf(stderr, "Number of significant TDI bits: %d\n", u.bitcount_tdi);
		fprintf(stderr, "Number of significant TDO bits: %d\n", u.bitcount_tdo);
		if (rc == 0) {
			fprintf(stderr, "Finished without errors.\n");
		} else {
			fprintf(stderr, "Finished with errors!\n");
		}
	}

	if (u.retval_i) {
		if (hex_mode) {
			printf("0x");
			for (i=0; i < u.retval_i; i+=4) {
				int val = 0;
				for (j=i; j<i+4; j++)
					val = val << 1 | u.retval[hex_mode > 1 ? j : u.retval_i - j - 1];
				printf("%x", val);
			}
		} else {
			printf("%d rmask bits:", u.retval_i);
			for (i=0; i < u.retval_i; i++)
				printf(" %d", u.retval[i]);
		}
		printf("\n");
	}

	if (realloc_name) {
		int num = 0;
		for (i = 0; i < LIBXSVF_MEM_NUM; i++) {
			if (realloc_maxsize[i] > 0)
				num = i+1;
		}
		printf("void *%s(void *h, void *ptr, int size, int which) {\n", realloc_name);
		for (i = 0; i < num; i++) {
			if (realloc_maxsize[i] > 0)
				printf("\tstatic unsigned char buf_%s[%d];\n", libxsvf_mem2str(i), realloc_maxsize[i]);
		}
		printf("\tstatic unsigned char *buflist[%d] = {", num);
		for (i = 0; i < num; i++) {
			if (realloc_maxsize[i] > 0)
				printf("%sbuf_%s", i ? ", " : " ", libxsvf_mem2str(i));
			else
				printf("%s(void*)0", i ? ", " : " ");
		}
		printf(" };\n\tstatic int sizelist[%d] = {", num);
		for (i = 0; i < num; i++) {
			if (realloc_maxsize[i] > 0)
				printf("%ssizeof(buf_%s)", i ? ", " : " ", libxsvf_mem2str(i));
			else
				printf("%s0", i ? ", " : " ");
		}
		printf(" };\n");
		printf("\treturn which < %d && size <= sizelist[which] ? buflist[which] : (void*)0;\n", num);
		printf("};\n");
	}

	return rc;
}
#endif

int xsvftool_esp_scan(void)
{
  u.idcode = 0; // clear previous scan result
  return libxsvf_play(&h, LIBXSVF_MODE_SCAN);
}

// return scan result (idcode)
uint32_t xsvftool_esp_id(void)
{
  return u.idcode;
}

int xsvftool_esp_program(int (*file_getbyte)(), int x)
{
  u.file_getbyte = file_getbyte;
  if(u.file_getbyte)
    return libxsvf_play(&h, x ? LIBXSVF_MODE_XSVF : LIBXSVF_MODE_SVF);
  return -1; // NULL file_getbyte pointer supplied
}

int xsvftool_esp_svf_packet(int (*packet_getbyte)(), int index, int final, char *report)
{
  u.verbose = 0;
  u.file_getbyte = packet_getbyte;
  if(u.file_getbyte)
  {
    if(index == 0)
      u.line = 1;
    int retval = libxsvf_svf_packet(&h, index, final);
    strcpy(report, u.report);
    return retval;
  }
  return -1; // NULL file_getbyte pointer supplied
}

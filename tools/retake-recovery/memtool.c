// SPDX-License-Identifier: GPL-2.0-only
/* memtool - raw MB86H47A chip-memory read/write for the CECH-ZD1 J,
 * built on the torne driver core (libusb transport, like torne_user).
 *
 *   memtool read32 <addr>            print one word
 *   memtool read <addr> <count>      dump count words, "ADDR: VALUE" per line
 *   memtool write32 <addr> <value>   write one word, then read it back
 * Addresses and values are hex (0x...) or decimal.
 */
#include <libusb-1.0/libusb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "torne_core.h"

static void do_msleep(void *priv, unsigned int ms)
{
	(void)priv;
	usleep(ms * 1000);
}

static u64 mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (u64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static u64 do_now_ms(void *priv)
{
	(void)priv;
	return mono_ms();
}

static void do_log(void *priv, int level, const char *fmt, ...)
{
	va_list ap;
	(void)priv;
	(void)level;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

struct usb_ctx {
	libusb_context *ctx;
	libusb_device_handle *h;
	unsigned long requests;
};

static int usb_control(void *priv, u8 request_type, u16 value, u16 index, void *buf, u16 len)
{
	struct usb_ctx *u = priv;

	u->requests++;
	return libusb_control_transfer(u->h, request_type, 0xbc, value, index, buf, len, 1000);
}

static int usb_bulk(void *priv, u8 endpoint, void *buf, int len, unsigned int timeout_ms)
{
	struct usb_ctx *u = priv;
	int got = 0, r;

	r = libusb_bulk_transfer(u->h, endpoint, buf, len, &got, timeout_ms);
	if (r == LIBUSB_ERROR_TIMEOUT)
		return got > 0 ? got : -ETIMEDOUT;
	return r < 0 ? -EIO : got;
}

static const struct torne_ops usb_ops = {
	.control = usb_control, .bulk = usb_bulk, .msleep = do_msleep, .now_ms = do_now_ms, .log = do_log,
};

static int usb_open(struct usb_ctx *u)
{
	int r = libusb_init(&u->ctx);

	if (r)
		return r;
	u->h = libusb_open_device_with_vid_pid(u->ctx, TORNE_USB_VENDOR, TORNE_USB_PRODUCT);
	if (!u->h) {
		fprintf(stderr, "CECH-ZD1 J not found\n");
		return -1;
	}
	if (libusb_kernel_driver_active(u->h, 0) == 1)
		libusb_detach_kernel_driver(u->h, 0);
	r = libusb_claim_interface(u->h, 0);
	if (r) {
		fprintf(stderr, "claim interface: %s\n", libusb_error_name(r));
		return r;
	}
	libusb_set_interface_alt_setting(u->h, 0, 0);
	return 0;
}

static unsigned long parse_num(const char *s)
{
	return strtoul(s, NULL, 0);
}

int main(int argc, char **argv)
{
	struct usb_ctx u;
	struct torne_bridge b;
	int r;

	if (argc < 2) {
		fprintf(stderr, "usage: %s read32 ADDR | read ADDR COUNT | write32 ADDR VALUE\n", argv[0]);
		return 2;
	}
	memset(&u, 0, sizeof(u));
	r = usb_open(&u);
	if (r)
		return 1;
	torne_bridge_init(&b, &usb_ops, &u);

	if (!strcmp(argv[1], "read32") && argc == 3) {
		u32 addr = (u32)parse_num(argv[2]);
		u32 v = torne_mem_read32(&b, addr);
		if (b.err) {
			fprintf(stderr, "read32 %#x: bridge err %d\n", addr, b.err);
			return 1;
		}
		printf("0x%08x: 0x%08x\n", addr, v);
	} else if (!strcmp(argv[1], "read") && argc == 4) {
		u32 addr = (u32)parse_num(argv[2]);
		unsigned long i, count = parse_num(argv[3]);
		for (i = 0; i < count; i++) {
			u32 v = torne_mem_read32(&b, addr + (u32)i * 4);
			if (b.err) {
				fprintf(stderr, "read %#x: bridge err %d\n", addr + (u32)i * 4, b.err);
				return 1;
			}
			printf("0x%08x: 0x%08x\n", addr + (u32)i * 4, v);
		}
	} else if (!strcmp(argv[1], "write32") && argc == 4) {
		u32 addr = (u32)parse_num(argv[2]);
		u32 val = (u32)parse_num(argv[3]);
		u32 back;
		torne_mem_write32(&b, addr, val);
		if (b.err) {
			fprintf(stderr, "write32 %#x: bridge err %d\n", addr, b.err);
			return 1;
		}
		back = torne_mem_read32(&b, addr);
		if (b.err) {
			fprintf(stderr, "write32 %#x readback: bridge err %d\n", addr, b.err);
			return 1;
		}
		printf("0x%08x: wrote 0x%08x, read back 0x%08x\n", addr, val, back);
	} else {
		fprintf(stderr, "usage: %s read32 ADDR | read ADDR COUNT | write32 ADDR VALUE\n", argv[0]);
		return 2;
	}
	return 0;
}

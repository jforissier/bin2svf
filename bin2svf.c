/*
 * bin2svf.c - HiSilicon D02 BIOS binary to SVF conversion tool
 */

#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Flags for generate_svf() */
#define MODE_ERASE	0x1
#define MODE_WRITE	0x2
#define MODE_VERIFY	0x4

/*
 * A pattern of bits sent in a SDR command to program or verify a page of the
 * SPI flash.
 */
struct page_pattern {
	uint8_t data[256];
	uint32_t addr_and_op;
};

/*
 * Type of operation the page pattern is intended for.
 */
#define PAGE_OP_PROGRAM_TDI	0x40
#define PAGE_OP_VERIFY_TDI	0xc0
#define PAGE_OP_VERIFY_TDO	0x00
#define PAGE_OP_VERIFY_MASK	0x00

enum chip_size {
	SIZE_4MB,
	SIZE_8MB,
	SIZE_16MB,
};

/*
 * Chip size only affects the time we wait when erasing the chip
 * (for the supported sizes at least).
 */
static const int chip_size = SIZE_16MB;

static int erase_time(enum chip_size size)
{
	switch (size) {
	case SIZE_4MB:
		return 80000;
	case SIZE_8MB:
		return 160000;
	case SIZE_16MB:
		return 250000;
	default:
		printf("Unsupported chip size\n");
		exit(1);
	}
}

static void usage()
{
	printf("Usage: bin2svf [INFILE] >OUTFILE\n\n");
	printf("Converts a Hisilicon D02 BIOS binary to SVF format.\n");
	printf("If INFILE is not supplied, reads from standard input.\n");
}

/* Reverse bits (MSB becomes LSB) */
static uint8_t reverse_bits_8(uint8_t v)
{
	uint8_t res = 0;
	int i;

	for (i = 0; i < 8; i++)
		res |= ((v >> i) & 1) << (7 - i);

	return res;
}

static uint32_t reverse_bits_32(uint32_t v)
{
	uint32_t res = 0;
	int i;

	for (i = 0; i < 32; i++)
		res |= ((v >> i) & 1) << (31 - i);

	return res;
}

static void hexdump(uint8_t *buf, size_t len, bool uppercase)
{
	size_t i;

	for (i = 0; i < len; i++)
		printf("%02x", buf[i]);
}

/*
 * Fill a bit pattern to be used in a SDR command to program or verify a page
 * @pattern:   the bit pattern to send for writing, for comparing against,
 *             or as a mask
 * @buf:       the data to send
 * @len:       the data length in bytes
 * @page_addr: the start address of the page in the flash memory (must be a
 *             multiple of 256 less than 16 MiB)
 * @op:        the operation this page will be used with
 */
static int create_page_pattern(struct page_pattern *pattern, uint8_t *buf,
			       size_t len, uint32_t page_addr, uint8_t op)
{
	size_t i;
	uint32_t addr;

	if (!buf)
		len = 0;
	for (i = 0; i < len; i++)
		pattern->data[255-i] = reverse_bits_8(buf[i]);
	for (i = len; i < 256; i++)
		pattern->data[255-i] = 0xff;
	if (op == PAGE_OP_VERIFY_TDO || op == PAGE_OP_VERIFY_MASK)
		addr = 0;
	else
		addr = htonl(reverse_bits_32(page_addr));
	pattern->addr_and_op = addr | htonl(op);

	return 0;
}

static void trst(int on)
{
	printf("TRST %s;\n\n", (on ? "ON" : "OFF"));
}

static void set_frequency()
{
	printf("FREQUENCY 5.00e+006 HZ;\n\n");
}

static void write_enable()
{
	printf("! Write enable\n");
	printf("SDR 8 TDI(60);\n\n");
}

static void write_disable()
{
	printf("! Write disable\n");
	printf("SDR 8 TDI(20);\n\n");
}

static void wait(int ms)
{
	printf("RUNTEST IDLE %G SEC ENDSTATE IDLE;\n\n", (float)ms/1000);
}

static void clear_software_protect()
{
	printf("! Clear software protect\n");
	printf("SDR 16 TDI(0080);\n\n");
}

static void check_no_software_protect()
{
	printf("! Check no software protect\n");
	printf("SDR 16 TDI(ffa0) TDO(c6ff) MASK(3900);\n\n");
}

static void check_status()
{
	printf("! Check status\n");
	printf("SDR 16 TDI(ffa0) TDO(0000) MASK(8000);\n\n");
}

static void bulk_erase()
{
	printf("! Bulk erase\n");
	printf("SDR 8 TDI(e3);\n\n");
}

static int send_page_program(uint8_t *buf, size_t len, uint32_t addr)
{
	struct page_pattern pattern;
	int rc;

	rc = create_page_pattern(&pattern, buf, len, addr,
				 PAGE_OP_PROGRAM_TDI);
	if (rc < 0)
		return rc;

	write_enable();

	printf("! Program page: 0x%08x\n", addr);
	printf("SDR 2080 TDI (");
	hexdump((uint8_t *)&pattern, sizeof(pattern), false);
	printf(");\n\n");

	write_disable();
	wait(2);
	
	return 0;
}

static int send_page_verify(uint8_t *buf, size_t len, uint32_t addr)
{
	struct page_pattern pattern;
	int rc;

	rc = create_page_pattern(&pattern, NULL, 0, addr,
				 PAGE_OP_VERIFY_TDI);
	if (rc < 0)
		return rc;

	printf("! Verify page: 0x%08x\n", addr);
	printf("SDR 2080 TDI (");
	hexdump((uint8_t *)&pattern, sizeof(pattern), true);
	printf(")\n");
	
	rc = create_page_pattern(&pattern, buf, len, addr,
				 PAGE_OP_VERIFY_TDO);
	if (rc < 0)
		return rc;

	printf("TDO (");
	hexdump((uint8_t *)&pattern, sizeof(pattern), false);
	printf(")\n");
	
	rc = create_page_pattern(&pattern, NULL, 0, addr,
				 PAGE_OP_VERIFY_MASK);
	if (rc < 0)
		return rc;

	printf("MASK (");
	hexdump((uint8_t *)&pattern, sizeof(pattern), true);
	printf(");\n");
	
	return 0;
}

static int generate_svf(uint8_t *buf, size_t len, uint32_t addr, uint32_t mode)
{
	int i;
	int rc;
	size_t blen;
	int all_ones;
	int written;

	trst(0);
	set_frequency();
	write_enable();
	clear_software_protect();
	write_disable();
	wait(100);

	check_no_software_protect();

	if (mode & MODE_ERASE) {
		write_enable();
		bulk_erase();
		write_disable();
		wait(erase_time(chip_size));
		check_status();
	}

	do {
		blen = len < 256 ? len : 256;
		all_ones = 1;
		written = 0;
		
		for (int i = 0; i < blen; i++) {
			if (buf[i] != 0xFF) {
				all_ones = 0;
				break;
			}
		}

		if (mode & MODE_WRITE) {
			/*
			 * No need to write a page full of ones if the
			 * flash was cleared previously.
			 */
			if (!(mode & MODE_ERASE) || !all_ones) {
				rc = send_page_program(buf, blen, addr);
				if (rc < 0)
					goto out;
				written = 1;
			}
		}
		/* Verify if asked to. Always verify after write. */
		if ((mode & MODE_VERIFY) || ((mode & MODE_WRITE) && written)) {
			rc = send_page_verify(buf, blen, addr);
			if (rc < 0)
				goto out;
		}

		len -= blen;
		buf += blen;
		addr += blen;
	} while (len);

	trst(1);
out:
	return rc;
}


int main(int argc, char *argv[])
{
	int fd = 0;
	uint8_t *buf;
	uint8_t *ptr;
	size_t n;
	size_t left = 32 * 1024 * 1024; /* Yeah... */

	if (argc == 2) {
		if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
			usage();
			return 0;
		}
		fd = open(argv[1], O_RDONLY);
		if (fd < 0) {
			perror("open");
			return 1;
		}
	}

	buf = malloc(left);
	if (!buf) {
		printf("bin2svf: out of memory\n");
		return 1;
	}

	ptr = buf;
	do {
		n = read(fd, ptr, left);
		if (!n) {
			if (errno == EINTR)
				continue;
			/* EOF */
			break;
		}
		left -= n;
		ptr += n;
		if (!left) {
			char c;

			do {
				n = read(fd, &c, 1);
			} while (!n && errno == EINTR);

			if (n) {
				printf("%s: input file to big\n", argv[0]);
				return 1;
			}
			break;
		}
	} while (left);

	if (ptr != buf)
		generate_svf(buf, ptr - buf, 0x800000,
			     MODE_ERASE | MODE_WRITE);

	return 0;	
}


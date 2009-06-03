/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2005-2007 coresystems GmbH <stepan@coresystems.de>
 * Copyright (C) 2006 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2007-2009 Luc Verhaegen <libv@skynet.be>
 * Copyright (C) 2007 Carl-Daniel Hailfinger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/*
 * Contains the board specific flash enables.
 */

#include <string.h>
#include <fcntl.h>
#include "flash.h"

/*
 * Helper functions for many Winbond Super I/Os of the W836xx range.
 */
/* Enter extended functions */
void w836xx_ext_enter(uint16_t port)
{
	OUTB(0x87, port);
	OUTB(0x87, port);
}

/* Leave extended functions */
void w836xx_ext_leave(uint16_t port)
{
	OUTB(0xAA, port);
}

/* Generic Super I/O helper functions */
uint8_t sio_read(uint16_t port, uint8_t reg)
{
	OUTB(reg, port);
	return INB(port + 1);
}

void sio_write(uint16_t port, uint8_t reg, uint8_t data)
{
	OUTB(reg, port);
	OUTB(data, port + 1);
}

void sio_mask(uint16_t port, uint8_t reg, uint8_t data, uint8_t mask)
{
	uint8_t tmp;

	OUTB(reg, port);
	tmp = INB(port + 1) & ~mask;
	OUTB(tmp | (data & mask), port + 1);
}

/**
 * Winbond W83627HF: Raise GPIO24.
 *
 * Suited for:
 *  - Agami Aruma
 *  - IWILL DK8-HTX
 */
static int w83627hf_gpio24_raise(uint16_t port, const char *name)
{
	w836xx_ext_enter(port);

	/* Is this the W83627HF? */
	if (sio_read(port, 0x20) != 0x52) {	/* Super I/O device ID reg. */
		fprintf(stderr, "\nERROR: %s: W83627HF: Wrong ID: 0x%02X.\n",
			name, sio_read(port, 0x20));
		w836xx_ext_leave(port);
		return -1;
	}

	/* PIN89S: WDTO/GP24 multiplex -> GPIO24 */
	sio_mask(port, 0x2B, 0x10, 0x10);

	/* Select logical device 8: GPIO port 2 */
	sio_write(port, 0x07, 0x08);

	sio_mask(port, 0x30, 0x01, 0x01);	/* Activate logical device. */
	sio_mask(port, 0xF0, 0x00, 0x10);	/* GPIO24 -> output */
	sio_mask(port, 0xF2, 0x00, 0x10);	/* Clear GPIO24 inversion */
	sio_mask(port, 0xF1, 0x10, 0x10);	/* Raise GPIO24 */

	w836xx_ext_leave(port);

	return 0;
}

static int w83627hf_gpio24_raise_2e(const char *name)
{
	return w83627hf_gpio24_raise(0x2e, name);
}

/**
 * Winbond W83627THF: GPIO 4, bit 4
 *
 * Suited for:
 *  - MSI K8T Neo2-F
 *  - MSI K8N-NEO3
 */
static int w83627thf_gpio4_4_raise(uint16_t port, const char *name)
{
	w836xx_ext_enter(port);

	/* Is this the W83627THF? */
	if (sio_read(port, 0x20) != 0x82) {	/* Super I/O device ID reg. */
		fprintf(stderr, "\nERROR: %s: W83627THF: Wrong ID: 0x%02X.\n",
			name, sio_read(port, 0x20));
		w836xx_ext_leave(port);
		return -1;
	}

	/* PINxxxxS: GPIO4/bit 4 multiplex -> GPIOXXX */

	sio_write(port, 0x07, 0x09);      /* Select LDN 9: GPIO port 4 */
	sio_mask(port, 0x30, 0x02, 0x02); /* Activate logical device. */
	sio_mask(port, 0xF4, 0x00, 0x10); /* GPIO4 bit 4 -> output */
	sio_mask(port, 0xF6, 0x00, 0x10); /* Clear GPIO4 bit 4 inversion */
	sio_mask(port, 0xF5, 0x10, 0x10); /* Raise GPIO4 bit 4 */

	w836xx_ext_leave(port);

	return 0;
}

static int w83627thf_gpio4_4_raise_2e(const char *name)
{
	return w83627thf_gpio4_4_raise(0x2e, name);
}

static int w83627thf_gpio4_4_raise_4e(const char *name)
{
	return w83627thf_gpio4_4_raise(0x4e, name);
}

/**
 * w83627: Enable MEMW# and set ROM size to max.
 */
static void w836xx_memw_enable(uint16_t port)
{
	w836xx_ext_enter(port);
	if (!(sio_read(port, 0x24) & 0x02)) {	/* Flash ROM enabled? */
		/* Enable MEMW# and set ROM size select to max. (4M). */
		sio_mask(port, 0x24, 0x28, 0x28);
	}
	w836xx_ext_leave(port);
}

/**
 * Common routine for several VT823x based boards.
 */
static void vt823x_set_all_writes_to_lpc(struct pci_dev *dev)
{
	uint8_t val;

	/* All memory cycles, not just ROM ones, go to LPC. */
	val = pci_read_byte(dev, 0x59);
	val &= ~0x80;
	pci_write_byte(dev, 0x59, val);
}

/**
 * VT823x: Set one of the GPIO pins.
 */
static void vt823x_gpio_set(struct pci_dev *dev, uint8_t gpio, int raise)
{
	uint16_t base;
	uint8_t val, bit;

	if ((gpio < 12) || (gpio > 15)) {
		fprintf(stderr, "\nERROR: "
			"VT823x GPIO%02d is not implemented.\n", gpio);
		return;
	}

	/* GPIO12-15 -> output */
	val = pci_read_byte(dev, 0xE4);
	val |= 0x10;
	pci_write_byte(dev, 0xE4, val);

	/* Now raise/drop the GPIO line itself. */
	bit = 0x01 << (gpio - 8);

	/* We need the I/O Base Address for this board's flash enable. */
	base = pci_read_word(dev, 0x88) & 0xff80;

	val = INB(base + 0x4D);
	if (raise)
		val |= bit;
	else
		val &= ~bit;
	OUTB(val, base + 0x4D);
}

/**
 * Suited for VIAs EPIA M and MII, and maybe other CLE266 based EPIAs.
 *
 * We don't need to do this when using coreboot, GPIO15 is never lowered there.
 */
static int board_via_epia_m(const char *name)
{
	struct pci_dev *dev;

	dev = pci_dev_find(0x1106, 0x3177);	/* VT8235 ISA bridge */
	if (!dev) {
		fprintf(stderr, "\nERROR: VT8235 ISA bridge not found.\n");
		return -1;
	}

	/* GPIO15 is connected to write protect. */
	vt823x_gpio_set(dev, 15, 1);

	return 0;
}

/**
 * Suited for:
 *   - ASUS A7V8X-MX SE and A7V400-MX: AMD K7 + VIA KM400A + VT8235
 *   - Tyan S2498 (Tomcat K7M): AMD Geode NX + VIA KM400 + VT8237.
 */
static int board_asus_a7v8x_mx(const char *name)
{
	struct pci_dev *dev;

	dev = pci_dev_find(0x1106, 0x3177);	/* VT8235 ISA bridge */
	if (!dev)
		dev = pci_dev_find(0x1106, 0x3227);	/* VT8237 ISA bridge */
	if (!dev) {
		fprintf(stderr, "\nERROR: VT823x ISA bridge not found.\n");
		return -1;
	}

	vt823x_set_all_writes_to_lpc(dev);
	w836xx_memw_enable(0x2E);

	return 0;
}

/**
 * Suited for VIAs EPIA SP and EPIA CN.
 */
static int board_via_epia_sp(const char *name)
{
	struct pci_dev *dev;

	dev = pci_dev_find(0x1106, 0x3227);	/* VT8237R ISA bridge */
	if (!dev) {
		fprintf(stderr, "\nERROR: VT8237R ISA bridge not found.\n");
		return -1;
	}

	vt823x_set_all_writes_to_lpc(dev);

	return 0;
}

/**
 * Suited for EPoX EP-8K5A2.
 */
static int board_epox_ep_8k5a2(const char *name)
{
	struct pci_dev *dev;

	dev = pci_dev_find(0x1106, 0x3177);	/* VT8235 ISA bridge */
	if (!dev) {
		fprintf(stderr, "\nERROR: VT8235 ISA bridge not found.\n");
		return -1;
	}

	w836xx_memw_enable(0x2E);

	return 0;
}

/**
 * Suited for ASUS P5A.
 *
 * This is rather nasty code, but there's no way to do this cleanly.
 * We're basically talking to some unknown device on SMBus, my guess
 * is that it is the Winbond W83781D that lives near the DIP BIOS.
 */
static int board_asus_p5a(const char *name)
{
	uint8_t tmp;
	int i;

#define ASUSP5A_LOOP 5000

	OUTB(0x00, 0xE807);
	OUTB(0xEF, 0xE803);

	OUTB(0xFF, 0xE800);

	for (i = 0; i < ASUSP5A_LOOP; i++) {
		OUTB(0xE1, 0xFF);
		if (INB(0xE800) & 0x04)
			break;
	}

	if (i == ASUSP5A_LOOP) {
		printf("%s: Unable to contact device.\n", name);
		return -1;
	}

	OUTB(0x20, 0xE801);
	OUTB(0x20, 0xE1);

	OUTB(0xFF, 0xE802);

	for (i = 0; i < ASUSP5A_LOOP; i++) {
		tmp = INB(0xE800);
		if (tmp & 0x70)
			break;
	}

	if ((i == ASUSP5A_LOOP) || !(tmp & 0x10)) {
		printf("%s: failed to read device.\n", name);
		return -1;
	}

	tmp = INB(0xE804);
	tmp &= ~0x02;

	OUTB(0x00, 0xE807);
	OUTB(0xEE, 0xE803);

	OUTB(tmp, 0xE804);

	OUTB(0xFF, 0xE800);
	OUTB(0xE1, 0xFF);

	OUTB(0x20, 0xE801);
	OUTB(0x20, 0xE1);

	OUTB(0xFF, 0xE802);

	for (i = 0; i < ASUSP5A_LOOP; i++) {
		tmp = INB(0xE800);
		if (tmp & 0x70)
			break;
	}

	if ((i == ASUSP5A_LOOP) || !(tmp & 0x10)) {
		printf("%s: failed to write to device.\n", name);
		return -1;
	}

	return 0;
}

static int board_ibm_x3455(const char *name)
{
	/* Set GPIO lines in the Broadcom HT-1000 southbridge. */
	/* It's not a superio but it uses the same index/data port method. */
	sio_mask(0xcd6, 0x45, 0x20, 0x20);

	return 0;
}

/**
 * Suited for the Gigabyte GA-K8N-SLI: CK804 southbridge.
 */
static int board_ga_k8n_sli(const char *name)
{
	struct pci_dev *dev;
	uint32_t base;
	uint8_t tmp;

	dev = pci_dev_find(0x10DE, 0x0050);	/* NVIDIA CK804 LPC */
	if (!dev) {
		fprintf(stderr, "\nERROR: NVIDIA LPC bridge not found.\n");
		return -1;
	}

	base = pci_read_long(dev, 0x64) & 0x0000FF00; /* System control area */

	/* if anyone knows more about nvidia lpcs, feel free to explain this */
	tmp = INB(base + 0xE1);
	tmp |= 0x05;
	OUTB(tmp, base + 0xE1);

	return 0;
}

static int board_hp_dl145_g3_enable(const char *name)
{
	/* Set GPIO lines in the Broadcom HT-1000 southbridge. */
	/* GPIO 0 reg from PM regs */
	/* Set GPIO 2 and 5 high, connected to flash WP# and TBL# pins. */
	/* It's not a superio but it uses the same index/data port method. */
	sio_mask(0xcd6, 0x44, 0x24, 0x24);

	return 0;
}

/**
 * Suited for EPoX EP-BX3, and maybe some other Intel 440BX based boards.
 */
static int board_epox_ep_bx3(const char *name)
{
	uint8_t tmp;

	/* Raise GPIO22. */
	tmp = INB(0x4036);
	OUTB(tmp, 0xEB);

	tmp |= 0x40;

	OUTB(tmp, 0x4036);
	OUTB(tmp, 0xEB);

	return 0;
}

/**
 * Suited for Acorp 6A815EPD.
 */
static int board_acorp_6a815epd(const char *name)
{
	struct pci_dev *dev;
	uint16_t port;
	uint8_t val;

	dev = pci_dev_find(0x8086, 0x2440);	/* Intel ICH2 LPC */
	if (!dev) {
		fprintf(stderr, "\nERROR: ICH2 LPC bridge not found.\n");
		return -1;
	}

	/* Use GPIOBASE register to find where the GPIO is mapped. */
	port = (pci_read_word(dev, 0x58) & 0xFFC0) + 0xE;

	val = INB(port);
	val |= 0x80;		/* Top Block Lock -- pin 8 of PLCC32 */
	val |= 0x40;		/* Lower Blocks Lock -- pin 7 of PLCC32 */
	OUTB(val, port);

	return 0;
}

/**
 * Suited for Artec Group DBE61 and DBE62.
 */
static int board_artecgroup_dbe6x(const char *name)
{
#define DBE6x_MSR_DIVIL_BALL_OPTS	0x51400015
#define DBE6x_PRI_BOOT_LOC_SHIFT	(2)
#define DBE6x_BOOT_OP_LATCHED_SHIFT	(8)
#define DBE6x_SEC_BOOT_LOC_SHIFT	(10)
#define DBE6x_PRI_BOOT_LOC		(3 << DBE6x_PRI_BOOT_LOC_SHIFT)
#define DBE6x_BOOT_OP_LATCHED		(3 << DBE6x_BOOT_OP_LATCHED_SHIFT)
#define DBE6x_SEC_BOOT_LOC		(3 << DBE6x_SEC_BOOT_LOC_SHIFT)
#define DBE6x_BOOT_LOC_FLASH		(2)
#define DBE6x_BOOT_LOC_FWHUB		(3)

	unsigned long msr[2];
	int msr_fd;
	unsigned long boot_loc;

	msr_fd = open("/dev/cpu/0/msr", O_RDWR);
	if (msr_fd == -1) {
		perror("open /dev/cpu/0/msr");
		return -1;
	}

	if (lseek(msr_fd, DBE6x_MSR_DIVIL_BALL_OPTS, SEEK_SET) == -1) {
		perror("lseek");
		close(msr_fd);
		return -1;
	}

	if (read(msr_fd, (void *)msr, 8) != 8) {
		perror("read");
		close(msr_fd);
		return -1;
	}

	if ((msr[0] & (DBE6x_BOOT_OP_LATCHED)) ==
	    (DBE6x_BOOT_LOC_FWHUB << DBE6x_BOOT_OP_LATCHED_SHIFT))
		boot_loc = DBE6x_BOOT_LOC_FWHUB;
	else
		boot_loc = DBE6x_BOOT_LOC_FLASH;

	msr[0] &= ~(DBE6x_PRI_BOOT_LOC | DBE6x_SEC_BOOT_LOC);
	msr[0] |= ((boot_loc << DBE6x_PRI_BOOT_LOC_SHIFT) |
		   (boot_loc << DBE6x_SEC_BOOT_LOC_SHIFT));

	if (lseek(msr_fd, DBE6x_MSR_DIVIL_BALL_OPTS, SEEK_SET) == -1) {
		perror("lseek");
		close(msr_fd);
		return -1;
	}

	if (write(msr_fd, (void *)msr, 8) != 8) {
		perror("write");
		close(msr_fd);
		return -1;
	}

	close(msr_fd);
	return 0;
}

/**
 * Set the specified GPIO on the specified ICHx southbridge to high.
 *
 * @param name The name of this board.
 * @param ich_vendor PCI vendor ID of the specified ICHx southbridge.
 * @param ich_device PCI device ID of the specified ICHx southbridge.
 * @param gpiobase_reg GPIOBASE register offset in the LPC bridge.
 * @param gp_lvl Offset of GP_LVL register in I/O space, relative to GPIOBASE.
 * @param gp_lvl_bitmask GP_LVL bitmask (set GPIO bits to 1, all others to 0).
 * @param gpio_bit The bit (GPIO) which shall be set to high.
 * @return If the write-enable was successful return 0, otherwise return -1.
 */
static int ich_gpio_raise(const char *name, uint16_t ich_vendor,
			  uint16_t ich_device, uint8_t gpiobase_reg,
			  uint8_t gp_lvl, uint32_t gp_lvl_bitmask,
			  unsigned int gpio_bit)
{
	struct pci_dev *dev;
	uint16_t gpiobar;
	uint32_t reg32;

	dev = pci_dev_find(ich_vendor, ich_device);	/* Intel ICHx LPC */
	if (!dev) {
		fprintf(stderr, "\nERROR: ICHx LPC dev %4x:%4x not found.\n",
			ich_vendor, ich_device);
		return -1;
	}

	/* Use GPIOBASE register to find the I/O space for GPIO. */
	gpiobar = pci_read_word(dev, gpiobase_reg) & gp_lvl_bitmask;

	/* Set specified GPIO to high. */
	reg32 = INL(gpiobar + gp_lvl);
	reg32 |= (1 << gpio_bit);
	OUTL(reg32, gpiobar + gp_lvl);

	return 0;
}

/**
 * Suited for ASUS P4B266.
 */
static int ich2_gpio22_raise(const char *name)
{
	return ich_gpio_raise(name, 0x8086, 0x2440, 0x58, 0x0c, 0xffc0, 22);
}

/**
 * Suited for MSI MS-7046.
 */
static int ich6_gpio19_raise(const char *name)
{
	return ich_gpio_raise(name, 0x8086, 0x2640, 0x48, 0x0c, 0xffc0, 19);
}

static int board_kontron_986lcd_m(const char *name)
{
	struct pci_dev *dev;
	uint16_t gpiobar;
	uint32_t val;

#define ICH7_GPIO_LVL2 0x38

	dev = pci_dev_find(0x8086, 0x27b8);	/* Intel ICH7 LPC */
	if (!dev) {
		// This will never happen on this board
		fprintf(stderr, "\nERROR: ICH7 LPC bridge not found.\n");
		return -1;
	}

	/* Use GPIOBASE register to find where the GPIO is mapped. */
	gpiobar = pci_read_word(dev, 0x48) & 0xfffc;

	val = INL(gpiobar + ICH7_GPIO_LVL2);	/* GP_LVL2 */
	printf_debug("\nGPIOBAR=0x%04x GP_LVL: 0x%08x\n", gpiobar, val);

	/* bit 2 (0x04) = 0 #TBL --> bootblock locking = 1
	 * bit 2 (0x04) = 1 #TBL --> bootblock locking = 0
	 * bit 3 (0x08) = 0 #WP --> block locking = 1
	 * bit 3 (0x08) = 1 #WP --> block locking = 0
	 *
	 * To enable full block locking, you would do:
	 *     val &= ~ ((1 << 2) | (1 << 3));
	 */
	val |= (1 << 2) | (1 << 3);

	OUTL(val, gpiobar + ICH7_GPIO_LVL2);

	return 0;
}

/**
 * Suited for:
 *   - BioStar P4M80-M4: Intel P4 + VIA P4M800 + VT8237
 *   - GIGABYTE GA-7VT600: AMD K7 + VIA KT600 + VT8237
 */
static int board_biostar_p4m80_m4(const char *name)
{
	/* enter IT87xx conf mode */
	enter_conf_mode_ite(0x2e);

	/* select right flash chip */
	sio_mask(0x2e, 0x22, 0x80, 0x80);

	/* bit 3: flash chip write enable
	 * bit 7: map flash chip at 1MB-128K (why though? ignoring this.)
	 */
	sio_mask(0x2e, 0x24, 0x04, 0x04);

	/* exit IT87xx conf mode */
	exit_conf_mode_ite(0x2e);

	return 0;
}

/**
 * Winbond W83697HF Super I/O + VIA VT8235 southbridge
 *
 * Suited for:
 *   - MSI KT4V and KT4V-L: AMD K7 + VIA KT400 + VT8235
 *   - MSI KT4 Ultra: AMD K7 + VIA KT400 + VT8235
 *   - MSI KT3 Ultra2: AMD K7 + VIA KT333 + VT8235
 */
static int board_msi_kt4v(const char *name)
{
	struct pci_dev *dev;
	uint8_t val;

	dev = pci_dev_find(0x1106, 0x3177);	/* VT8235 ISA bridge */
	if (!dev) {
		fprintf(stderr, "\nERROR: VT823x ISA bridge not found.\n");
		return -1;
	}

	val = pci_read_byte(dev, 0x59);
	val &= 0x0c;
	pci_write_byte(dev, 0x59, val);

	vt823x_gpio_set(dev, 12, 1);
	w836xx_memw_enable(0x2E);

	return 0;
}

/**
 * We use 2 sets of IDs here, you're free to choose which is which. This
 * is to provide a very high degree of certainty when matching a board on
 * the basis of subsystem/card IDs. As not every vendor handles
 * subsystem/card IDs in a sane manner.
 *
 * Keep the second set NULLed if it should be ignored. Keep the subsystem IDs
 * NULLed if they don't identify the board fully. But please take care to
 * provide an as complete set of pci ids as possible; autodetection is the
 * preferred behaviour and we would like to make sure that matches are unique.
 *
 * The coreboot ids are used two fold. When running with a coreboot firmware,
 * the ids uniquely matches the coreboot board identification string. When a
 * legacy bios is installed and when autodetection is not possible, these ids
 * can be used to identify the board through the -m command line argument.
 *
 * When a board is identified through its coreboot ids (in both cases), the
 * main pci ids are still required to match, as a safeguard.
 */

/* Please keep this list alphabetically ordered by vendor/board name. */
struct board_pciid_enable board_pciid_enables[] = {
	/* first pci-id set [4],          second pci-id set [4],          coreboot id [2],             vendor name    board name            flash enable */
	{0x8086, 0x1130,      0,      0,  0x105a, 0x0d30, 0x105a, 0x4d33, "acorp",      "6a815epd",    "Acorp",       "6A815EPD",           board_acorp_6a815epd},
	{0x1022, 0x746B, 0x1022, 0x36C0,       0,      0,      0,      0, "AGAMI",      "ARUMA",       "agami",       "Aruma",              w83627hf_gpio24_raise_2e},
	{0x1022, 0x2090,      0,      0,  0x1022, 0x2080,      0,      0, "artecgroup", "dbe61",       "Artec Group", "DBE61",              board_artecgroup_dbe6x},
	{0x1022, 0x2090,      0,      0,  0x1022, 0x2080,      0,      0, "artecgroup", "dbe62",       "Artec Group", "DBE62",              board_artecgroup_dbe6x},
	{0x1106, 0x3177, 0x1043, 0x80A1,  0x1106, 0x3205, 0x1043, 0x8118, NULL,         NULL,          "ASUS",        "A7V8-MX SE",         board_asus_a7v8x_mx},
	{0x8086, 0x1a30, 0x1043, 0x8070,  0x8086, 0x244b, 0x1043, 0x8028, NULL,         NULL,          "ASUS",        "P4B266",             ich2_gpio22_raise},
	{0x10B9, 0x1541,      0,      0,  0x10B9, 0x1533,      0,      0, "asus",       "p5a",         "ASUS",        "P5A",                board_asus_p5a},
	{0x1106, 0x3149, 0x1565, 0x3206,  0x1106, 0x3344, 0x1565, 0x1202, NULL,         NULL,          "BioStar",     "P4M80-M4",           board_biostar_p4m80_m4},
	{0x1106, 0x3177, 0x1106, 0x3177,  0x1106, 0x3059, 0x1695, 0x3005, NULL,         NULL,          "EPoX",        "EP-8K5A2",           board_epox_ep_8k5a2},
	{0x8086, 0x7110,      0,      0,  0x8086, 0x7190,      0,      0, "epox",       "ep-bx3",      "EPoX",        "EP-BX3",             board_epox_ep_bx3},
	{0x1039, 0x0761,      0,      0,       0,      0,      0,      0, "gigabyte",   "2761gxdk",    "GIGABYTE",    "GA-2761GXDK",        it87xx_probe_spi_flash},
	{0x1106, 0x3227, 0x1458, 0x5001,  0x10ec, 0x8139, 0x1458, 0xe000, NULL,         NULL,          "GIGABYTE",    "GA-7VT600",          board_biostar_p4m80_m4},
	{0x10DE, 0x0050, 0x1458, 0x0C11,  0x10DE, 0x005e, 0x1458, 0x5000, NULL,         NULL,          "GIGABYTE",    "GA-K8N-SLI",         board_ga_k8n_sli},
	{0x10de, 0x0360,      0,      0,       0,      0,      0,      0, "gigabyte",   "m57sli",      "GIGABYTE",    "GA-M57SLI-S4",       it87xx_probe_spi_flash},
	{0x10de, 0x03e0,      0,      0,       0,      0,      0,      0, "gigabyte",   "m61p",        "GIGABYTE",    "GA-M61P-S3",         it87xx_probe_spi_flash},
	{0x1002, 0x4398, 0x1458, 0x5004,  0x1002, 0x4391, 0x1458, 0xb000, NULL,         NULL,          "GIGABYTE",    "GA-MA78G-DS3H",      it87xx_probe_spi_flash},
	{0x1002, 0x4398, 0x1458, 0x5004,  0x1002, 0x4391, 0x1458, 0xb002, NULL,         NULL,          "GIGABYTE",    "GA-MA78GM-S2H",      it87xx_probe_spi_flash},
	/* SB600 LPC, RD790 North. Neither are specific to the GA-MA790FX-DQ6. The coreboot ID is here to be able to trigger the board enable more easily. */
	{0x1002, 0x438d, 0x1458, 0x5001,  0x1002, 0x5956, 0x1002, 0x5956, "gigabyte",   "ma790fx-dq6", "GIGABYTE",    "GA-MA790FX-DQ6",     it87xx_probe_spi_flash},
	{0x1166, 0x0223, 0x103c, 0x320d,  0x102b, 0x0522, 0x103c, 0x31fa, "hp",         "dl145_g3",    "HP",          "DL145 G3",           board_hp_dl145_g3_enable},
	{0x1166, 0x0205, 0x1014, 0x0347,       0,      0,      0,      0, "ibm",        "x3455",       "IBM",         "x3455",              board_ibm_x3455},
	{0x1039, 0x5513, 0x8086, 0xd61f,  0x1039, 0x6330, 0x8086, 0xd61f, NULL,         NULL,          "Intel",       "D201GLY",            wbsio_check_for_spi},
	{0x1022, 0x7468,      0,      0,       0,      0,      0,      0, "iwill",      "dk8_htx",     "IWILL",       "DK8-HTX",            w83627hf_gpio24_raise_2e},
	/* Note: There are >= 2 version of the Kontron 986LCD-M/mITX! */
	{0x8086, 0x27b8,      0,      0,       0,      0,      0,      0, "kontron",    "986lcd-m",    "Kontron",     "986LCD-M",           board_kontron_986lcd_m},
	{0x10ec, 0x8168, 0x10ec, 0x8168,  0x104c, 0x8023, 0x104c, 0x8019, "kontron",    "986lcd-m",    "Kontron",     "986LCD-M",           board_kontron_986lcd_m},
	{0x10de, 0x005e,      0,      0,       0,      0,      0,      0, "msi",        "k8n-neo3",    "MSI",         "MS-7135 (K8N Neo3)", w83627thf_gpio4_4_raise_4e},
	{0x1106, 0x3149, 0x1462, 0x7094,  0x10ec, 0x8167, 0x1462, 0x094c, NULL,         NULL,          "MSI",         "MS-6702E (K8T Neo2-F)",w83627thf_gpio4_4_raise_2e},
	{0x1106, 0x0571, 0x1462, 0x7120,       0,      0,      0,      0, "msi",        "kt4v",        "MSI",         "MS-6712 (KT4V)",     board_msi_kt4v},
	{0x13f6, 0x0111, 0x1462, 0x5900,  0x1106, 0x3177, 0x1106,      0, "msi",        "kt4ultra",    "MSI",         "MS-6590 (KT4 Ultra)",board_msi_kt4v},
	{0x8086, 0x2658, 0x1462, 0x7046,  0x1106, 0x3044, 0x1462, 0x046d, NULL,         NULL,          "MSI",         "MS-7046",            ich6_gpio19_raise},
	{0x8086, 0x1076, 0x8086, 0x1176,  0x1106, 0x3059, 0x10f1, 0x2498, NULL,         NULL,          "Tyan",        "S2498 (Tomcat K7M)", board_asus_a7v8x_mx},
	{0x1106, 0x0314, 0x1106, 0xaa08,  0x1106, 0x3227, 0x1106, 0xAA08, NULL,         NULL,          "VIA",         "EPIA-CN",            board_via_epia_sp},
	{0x1106, 0x3177, 0x1106, 0xAA01,  0x1106, 0x3123, 0x1106, 0xAA01, NULL,         NULL,          "VIA",         "EPIA M/MII/...",     board_via_epia_m},
	{0x1106, 0x3227, 0x1106, 0xAA01,  0x1106, 0x0259, 0x1106, 0xAA01, NULL,         NULL,          "VIA",         "EPIA SP",            board_via_epia_sp},
	{0x1106, 0x5337, 0x1458, 0xb003,  0x1106, 0x287e, 0x1106, 0x337e, "via",        "pc3500g",     "VIA",         "PC3500G",            it87xx_probe_spi_flash},
	{     0,      0,      0,      0,       0,      0,      0,      0, NULL,         NULL,          NULL,          NULL,                 NULL}, /* end marker */
};

/* Please keep this list alphabetically ordered by vendor/board. */
const struct board_info boards_ok[] = {
	/* Verified working boards that don't need write-enables. */
	{ "Abit",		"AX8", },
	{ "Advantech",		"PCM-5820", },
	{ "ASI",		"MB-5BLMP", },
	{ "ASUS",		"A8N-E", },
	{ "ASUS",		"A8NE-FM/S", },
	{ "ASUS",		"A8N-SLI", },
	{ "ASUS",		"A8N-SLI Premium", },
	{ "ASUS",		"A8V-E Deluxe", },
	{ "ASUS",		"M2A-VM", },
	{ "ASUS",		"M2N-E", },
	{ "ASUS",		"M2V", },
	{ "ASUS",		"P2B", },
	{ "ASUS",		"P2B-F", },
	{ "ASUS",		"P2B-D", },
	{ "ASUS",		"P2B-DS", },
	{ "ASUS",		"A7N8X Deluxe", },
	{ "ASUS",		"A7V400-MX", },
	{ "ASUS",		"A7V8X-MX", },
	{ "ASUS",		"A8V-E SE", },
	{ "ASUS",		"P2L97-S", },
	{ "ASUS",		"M2A-MX", },
	{ "ASUS",		"P5B-Deluxe", },
	{ "ASUS",		"P6T Deluxe V2", },
	{ "A-Trend",		"ATC-6220", },
	{ "BCOM",		"WinNET100", },
	{ "GIGABYTE",		"GA-6BXC", },
	{ "GIGABYTE",		"GA-6BXDU", },
	{ "GIGABYTE",		"GA-6ZMA", },
	{ "Intel",		"EP80759", },
	{ "Jetway",		"J7F4K1G5D-PB", },
	{ "MSI",		"MS-6570 (K7N2)", },
	{ "MSI",		"MS-7065", },
	{ "MSI",		"MS-7236 (945PL Neo3)", },
	{ "MSI",		"MS-7345 (P35 Neo2-FIR)", },
	{ "MSI",		"MS-7168 (Orion)", },
	{ "MSI",		"MS-7255 (P4M890M)", },
	{ "NEC",		"PowerMate 2000", },
	{ "PC Engines",		"Alix.1c", },
	{ "PC Engines",		"Alix.2c2", },
	{ "PC Engines",		"Alix.2c3", },
	{ "PC Engines",		"Alix.3c3", },
	{ "PC Engines",		"Alix.3d3", },
	{ "RCA",		"RM4100", },
	{ "Supermicro",		"H8QC8", },
	{ "Sun",		"Blade x6250", },
	{ "Thomson",		"IP1000", },
	{ "T-Online",		"S-100", },
	{ "Tyan",		"S1846", },
	{ "Tyan",		"S2881", },
	{ "Tyan",		"S2882", },
	{ "Tyan",		"S2882-D", },
	{ "Tyan",		"S2891", },
	{ "Tyan",		"S2892", },
	{ "Tyan",		"S2895", },
	{ "Tyan",		"S3095", },
	{ "Tyan",		"S5180", },
	{ "Tyan",		"S5191", },
	{ "Tyan",		"S5197", },
	{ "Tyan",		"S5211", },
	{ "Tyan",		"S5211-1U", },
	{ "Tyan",		"S5220", },
	{ "Tyan",		"S5375", },
	{ "Tyan",		"iS5375-1U", },
	{ "Tyan",		"S5376G2NR/S5376WAG2NR", },
	{ "Tyan",		"S5377", },
	{ "Tyan",		"S5397", },
	{ "VIA",		"EPIA-LN", },
	{ "VIA",		"VB700X", },
	{ "VIA",		"NAB74X0", },
	{ "VIA",		"pc2500e", },

	{},
};

/* Please keep this list alphabetically ordered by vendor/board. */
const struct board_info boards_bad[] = {
	/* Verified non-working boards (for now). */
	{ "Abit",		"IS-10", },
	{ "ASUS",		"M3N78 Pro", },
	{ "ASUS",		"MEW-AM", },
	{ "ASUS",		"MEW-VM", },
	{ "ASUS",		"P3B-F", },
	{ "ASUS",		"P5B", },
	{ "ASUS",		"P5BV-M", },
	{ "Biostar",		"M6TBA", },
	{ "Boser",		"HS-6637", },
	{ "DFI",		"855GME-MGF", },
	{ "FIC",		"VA-502", },
	{ "MSI",		"MS-6178", },
	{ "MSI",		"MS-7260 (K9N Neo)", },
	{ "PCCHIPS",		"M537DMA33", },
	{ "Soyo",		"SY-5VD", },
	{ "Sun",		"Fire x4540", },
	{ "Sun",		"Fire x4150", },
	{ "Sun",		"Fire x4200", },
	{ "Sun",		"Fire x4600", },

	{},
};

void print_supported_boards_helper(const struct board_info *b)
{
	int i, j;

	for (i = 0; b[i].vendor != NULL; i++) {
		printf("%s", b[i].vendor);
		for (j = 0; j < 25 - strlen(b[i].vendor); j++)
			printf(" ");
		printf("%s", b[i].name);
		for (j = 0; j < 23 - strlen(b[i].name); j++)
			printf(" ");
		printf("\n");
	}
}

void print_supported_boards(void)
{
	int i, j;
	struct board_pciid_enable *b = board_pciid_enables;

	printf("\nSupported boards which need write-enable code:\n\nVendor:   "
	       "               Board:                   Required option:\n\n");

	for (i = 0; b[i].vendor_name != NULL; i++) {
		printf("%s", b[i].vendor_name);
		for (j = 0; j < 25 - strlen(b[i].vendor_name); j++)
			printf(" ");
		printf("%s", b[i].board_name);
		for (j = 0; j < 25 - strlen(b[i].board_name); j++)
			printf(" ");
		if (b[i].lb_vendor != NULL)
			printf("-m %s:%s\n", b[i].lb_vendor, b[i].lb_part);
		else
			printf("(none, board is autodetected)\n");
	}

	printf("\nSupported boards which don't need write-enable code:\n\n");
	print_supported_boards_helper(boards_ok);

	printf("\nBoards which have been verified to NOT work (yet):\n\n");
	print_supported_boards_helper(boards_bad);
}

/**
 * Match boards on coreboot table gathered vendor and part name.
 * Require main PCI IDs to match too as extra safety.
 */
static struct board_pciid_enable *board_match_coreboot_name(const char *vendor,
							    const char *part)
{
	struct board_pciid_enable *board = board_pciid_enables;
	struct board_pciid_enable *partmatch = NULL;

	for (; board->vendor_name; board++) {
		if (vendor && (!board->lb_vendor
			       || strcasecmp(board->lb_vendor, vendor)))
			continue;

		if (!board->lb_part || strcasecmp(board->lb_part, part))
			continue;

		if (!pci_dev_find(board->first_vendor, board->first_device))
			continue;

		if (board->second_vendor &&
		    !pci_dev_find(board->second_vendor, board->second_device))
			continue;

		if (vendor)
			return board;

		if (partmatch) {
			/* a second entry has a matching part name */
			printf("AMBIGUOUS BOARD NAME: %s\n", part);
			printf("At least vendors '%s' and '%s' match.\n",
			       partmatch->lb_vendor, board->lb_vendor);
			printf("Please use the full -m vendor:part syntax.\n");
			return NULL;
		}
		partmatch = board;
	}

	if (partmatch)
		return partmatch;

	printf("\nUnknown vendor:board from coreboot table or -m option: %s:%s\n\n", vendor, part);
	return NULL;
}

/**
 * Match boards on PCI IDs and subsystem IDs.
 * Second set of IDs can be main only or missing completely.
 */
static struct board_pciid_enable *board_match_pci_card_ids(void)
{
	struct board_pciid_enable *board = board_pciid_enables;

	for (; board->vendor_name; board++) {
		if (!board->first_card_vendor || !board->first_card_device)
			continue;

		if (!pci_card_find(board->first_vendor, board->first_device,
				   board->first_card_vendor,
				   board->first_card_device))
			continue;

		if (board->second_vendor) {
			if (board->second_card_vendor) {
				if (!pci_card_find(board->second_vendor,
						   board->second_device,
						   board->second_card_vendor,
						   board->second_card_device))
					continue;
			} else {
				if (!pci_dev_find(board->second_vendor,
						  board->second_device))
					continue;
			}
		}

		return board;
	}

	return NULL;
}

int board_flash_enable(const char *vendor, const char *part)
{
	struct board_pciid_enable *board = NULL;
	int ret = 0;

	if (part)
		board = board_match_coreboot_name(vendor, part);

	if (!board)
		board = board_match_pci_card_ids();

	if (board) {
		printf("Found board \"%s %s\", enabling flash write... ",
		       board->vendor_name, board->board_name);

		ret = board->enable(board->vendor_name);
		if (ret)
			printf("FAILED!\n");
		else
			printf("OK.\n");
	}

	return ret;
}

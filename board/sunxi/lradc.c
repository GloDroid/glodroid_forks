#include <common.h>
#include <asm/io.h>
#include "lradc.h"

#define LRADC_BASE		0x1c21800

#define LRADC_CTRL		(LRADC_BASE + 0x00)
#define LRADC_INTC		(LRADC_BASE + 0x04)
#define LRADC_INTS		(LRADC_BASE + 0x08)
#define LRADC_DATA0		(LRADC_BASE + 0x0c)
#define LRADC_DATA1		(LRADC_BASE + 0x10)

/* LRADC_CTRL bits */
#define FIRST_CONVERT_DLY(x)	((x) << 24) /* 8 bits */
#define CHAN_SELECT(x)		((x) << 22) /* 2 bits */
#define CONTINUE_TIME_SEL(x)	((x) << 16) /* 4 bits */
#define KEY_MODE_SEL(x)		((x) << 12) /* 2 bits */
#define LEVELA_B_CNT(x)		((x) << 8)  /* 4 bits */
#define HOLD_KEY_EN(x)		((x) << 7)
#define HOLD_EN(x)		((x) << 6)
#define LEVELB_VOL(x)		((x) << 4)  /* 2 bits */
#define SAMPLE_RATE(x)		((x) << 2)  /* 2 bits */
#define ENABLE(x)		((x) << 0)

/* LRADC_INTC and LRADC_INTS bits */
#define CHAN1_KEYUP_IRQ		BIT(12)
#define CHAN1_ALRDY_HOLD_IRQ	BIT(11)
#define CHAN1_HOLD_IRQ		BIT(10)
#define	CHAN1_KEYDOWN_IRQ	BIT(9)
#define CHAN1_DATA_IRQ		BIT(8)
#define CHAN0_KEYUP_IRQ		BIT(4)
#define CHAN0_ALRDY_HOLD_IRQ	BIT(3)
#define CHAN0_HOLD_IRQ		BIT(2)
#define	CHAN0_KEYDOWN_IRQ	BIT(1)
#define CHAN0_DATA_IRQ		BIT(0)

// this is for PinePhone only

int lradc_get_pressed_key(void)
{
	uint32_t val;
	uint32_t vref = 3000000 * 2 / 3;

	val = readl(LRADC_DATA0) & 0x3f;
	val = val * vref / 63;

//	printf("lradc=%u\n", val);

	if (val < 200000) // 158730
		return KEY_VOLUMEUP;
	else if (val < 400000) // 349206
		return KEY_VOLUMEDOWN;

	return 0;
}

void lradc_enable(void)
{
	// aldo3 is always on and defaults to 3V

	writel(0xffffffff, LRADC_INTS);
	writel(0, LRADC_INTC);

	/*
	 * Set sample time to 4 ms / 250 Hz. Wait 2 * 4 ms for key to
	 * stabilize on press, wait (1 + 1) * 4 ms for key release
	 */
	writel(FIRST_CONVERT_DLY(0) | LEVELA_B_CNT(0) | HOLD_EN(0) |
		SAMPLE_RATE(0) | ENABLE(1), LRADC_CTRL);

}

void lradc_disable(void)
{
	writel(0xffffffff, LRADC_INTS);
	writel(0, LRADC_INTC);

	/* Disable lradc, leave other settings unchanged */
	writel(FIRST_CONVERT_DLY(2) | LEVELA_B_CNT(1) | HOLD_EN(1) |
		SAMPLE_RATE(2), LRADC_CTRL);
}

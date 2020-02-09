// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This driver supports the digital controls for the internal codec
 * found in Allwinner's A33 SoCs.
 *
 * (C) Copyright 2010-2016
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * huangxin <huangxin@Reuuimllatech.com>
 * Mylène Josserand <mylene.josserand@free-electrons.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/log2.h>
#include <linux/mfd/ac100.h>

#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <sound/tlv.h>

#include "sun8i-codec.h"

#define SUN8I_SYSCLK_CTL				0x00c
#define SUN8I_SYSCLK_CTL_AIF1CLK_ENA			11
#define SUN8I_SYSCLK_CTL_AIF1CLK_SRC_PLL		(0x3 << 8)
#define SUN8I_SYSCLK_CTL_AIF2CLK_ENA			7
#define SUN8I_SYSCLK_CTL_AIF2CLK_SRC_PLL		(0x3 << 4)
#define SUN8I_SYSCLK_CTL_SYSCLK_ENA			3
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC			0
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF1CLK		(0x0 << 0)
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF2CLK		(0x1 << 0)
#define SUN8I_MOD_CLK_ENA				0x010
#define SUN8I_MOD_CLK_ENA_AIF1				15
#define SUN8I_MOD_CLK_ENA_AIF2				14
#define SUN8I_MOD_CLK_ENA_AIF3				13
#define SUN8I_MOD_CLK_ENA_ADC				3
#define SUN8I_MOD_CLK_ENA_DAC				2
#define SUN8I_MOD_RST_CTL				0x014
#define SUN8I_MOD_RST_CTL_AIF1				15
#define SUN8I_MOD_RST_CTL_AIF2				14
#define SUN8I_MOD_RST_CTL_AIF3				13
#define SUN8I_MOD_RST_CTL_ADC				3
#define SUN8I_MOD_RST_CTL_DAC				2
#define SUN8I_SYS_SR_CTRL				0x018
#define SUN8I_SYS_SR_CTRL_AIF_FS(n)			(16 - 4 * (n))
#define SUN8I_AIF_CLK_CTRL(n)				(0x040 * (n))
#define SUN8I_AIF_CLK_CTRL_MSTR_MOD			15
#define SUN8I_AIF_CLK_CTRL_CLK_INV			13
#define SUN8I_AIF_CLK_CTRL_BCLK_DIV			9
#define SUN8I_AIF_CLK_CTRL_LRCK_DIV			6
#define SUN8I_AIF_CLK_CTRL_WORD_SIZ			4
#define SUN8I_AIF_CLK_CTRL_DATA_FMT			2
#define SUN8I_AIF_CLK_CTRL_MONO_PCM			1
#define SUN8I_AIF1_ADCDAT_CTRL				0x044
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA		15
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA		14
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_SRC		10
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_SRC		8
#define SUN8I_AIF1_DACDAT_CTRL				0x048
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA		15
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA		14
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_SRC		10
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_SRC		8
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_LOOP_ENA		0
#define SUN8I_AIF1_MXR_SRC				0x04c
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF1DA0L	15
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACL	14
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_ADCL		13
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACR	12
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF1DA0R	11
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACR	10
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_ADCR		9
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACL	8
#define SUN8I_AIF1_VOL_CTRL1				0x050
#define SUN8I_AIF1_VOL_CTRL1_AD0L_VOL			8
#define SUN8I_AIF1_VOL_CTRL1_AD0R_VOL			0
#define SUN8I_AIF1_VOL_CTRL3				0x058
#define SUN8I_AIF1_VOL_CTRL3_DA0L_VOL			8
#define SUN8I_AIF1_VOL_CTRL3_DA0R_VOL			0
#define SUN8I_AIF2_ADCDAT_CTRL				0x084
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_ENA		15
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_ENA		14
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_SRC		10
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_SRC		8
#define SUN8I_AIF2_DACDAT_CTRL				0x088
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_ENA		15
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_ENA		14
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_SRC		10
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_SRC		8
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_LOOP_ENA		0
#define SUN8I_AIF2_MXR_SRC				0x08c
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA0L	15
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA1L	14
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF2DACR	13
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_ADCL		12
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA0R	11
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA1R	10
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF2DACL	9
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_ADCR		8
#define SUN8I_AIF2_VOL_CTRL1				0x090
#define SUN8I_AIF2_VOL_CTRL1_ADCL_VOL			8
#define SUN8I_AIF2_VOL_CTRL1_ADCR_VOL			0
#define SUN8I_AIF2_VOL_CTRL2				0x098
#define SUN8I_AIF2_VOL_CTRL2_DACL_VOL			8
#define SUN8I_AIF2_VOL_CTRL2_DACR_VOL			0
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF1		(0x0 << 0)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF2		(0x1 << 0)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF1CLK	(0x2 << 0)
#define SUN8I_AIF3_DACDAT_CTRL				0x0c8
#define SUN8I_AIF3_DACDAT_CTRL_AIF3_LOOP_ENA		0
#define SUN8I_AIF3_PATH_CTRL				0x0cc
#define SUN8I_AIF3_PATH_CTRL_AIF3_ADC_SRC		10
#define SUN8I_AIF3_PATH_CTRL_AIF2_DAC_SRC		8
#define SUN8I_AIF3_PATH_CTRL_AIF3_PINS_TRI		7
#define SUN8I_ADC_DIG_CTRL				0x100
#define SUN8I_ADC_DIG_CTRL_ENAD				15
#define SUN8I_ADC_DIG_CTRL_ADOUT_DTS			2
#define SUN8I_ADC_DIG_CTRL_ADOUT_DLY			1
#define SUN8I_ADC_VOL_CTRL				0x104
#define SUN8I_ADC_VOL_CTRL_ADCL_VOL			8
#define SUN8I_ADC_VOL_CTRL_ADCR_VOL			0
#define SUN8I_HMIC_CTRL_1				0x110
#define SUN8I_HMIC_CTRL_1_HMIC_M			12
#define SUN8I_HMIC_CTRL_1_HMIC_M_MASK			(0xf<<12)
#define SUN8I_HMIC_CTRL_1_HMIC_N			8
#define SUN8I_HMIC_CTRL_1_HMIC_N_MASK			(0xf<<8)
#define SUN8I_HMIC_CTRL_1_JACK_IN_IRQ_EN		4
#define SUN8I_HMIC_CTRL_1_JACK_OUT_IRQ_EN		3
#define SUN8I_HMIC_CTRL_1_MIC_DET_IRQ_EN		0
#define SUN8I_HMIC_CTRL_2				0x114
#define SUN8I_HMIC_CTRL_2_MDATA_THRESHOLD		8
#define SUN8I_HMIC_CTRL_2_MDATA_THRESHOLD_MASK		(0x1f<<8)
#define SUN8I_HMIC_STS					0x118
#define SUN8I_HMIC_STS_MDATA_THRESHOLD_EN		15
#define SUN8I_HMIC_STS_MDATA_DISCARD			13
#define SUN8I_HMIC_STS_MDATA_DISCARD_MASK		(0x3<<13)
#define SUN8I_HMIC_STS_HMIC_DATA			8
#define SUN8I_HMIC_STS_HMIC_DATA_MASK			(0x1f<<8)
#define SUN8I_HMIC_STS_JACK_DET_ST			6
#define SUN8I_HMIC_STS_JACK_DET_IIRQ			4
#define SUN8I_HMIC_STS_JACK_DET_OIRQ			3
#define SUN8I_HMIC_STS_MIC_DET_ST			0
#define SUN8I_DAC_DIG_CTRL				0x120
#define SUN8I_DAC_DIG_CTRL_ENDA				15
#define SUN8I_DAC_VOL_CTRL				0x124
#define SUN8I_DAC_VOL_CTRL_DACL_VOL			8
#define SUN8I_DAC_VOL_CTRL_DACR_VOL			0
#define SUN8I_DAC_MXR_SRC				0x130
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA0L		15
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA1L		14
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF2DACL		13
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_ADCL		12
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA0R		11
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA1R		10
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF2DACR		9
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_ADCR		8

#define SUN8I_SYSCLK_CTL_AIF1CLK_SRC_MASK	GENMASK(9, 8)
#define SUN8I_SYSCLK_CTL_AIF2CLK_SRC_MASK	GENMASK(7, 4)
#define SUN8I_SYS_SR_CTRL_AIF_FS_MASK(n)	(GENMASK(19, 16) >> (4 * (n)))
#define SUN8I_AIF_CLK_CTRL_CLK_INV_MASK		GENMASK(14, 13)
#define SUN8I_AIF_CLK_CTRL_BCLK_DIV_MASK	GENMASK(12, 9)
#define SUN8I_AIF_CLK_CTRL_LRCK_DIV_MASK	GENMASK(8, 6)
#define SUN8I_AIF_CLK_CTRL_WORD_SIZ_MASK	GENMASK(5, 4)
#define SUN8I_AIF_CLK_CTRL_DATA_FMT_MASK	GENMASK(3, 2)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_MASK	GENMASK(1, 0)

#define SUN8I_AIF_PCM_FMTS  (SNDRV_PCM_FMTBIT_S8|\
			     SNDRV_PCM_FMTBIT_S16_LE|\
			     SNDRV_PCM_FMTBIT_S20_LE|\
			     SNDRV_PCM_FMTBIT_S24_LE)
#define SUN8I_AIF_PCM_RATES (SNDRV_PCM_RATE_8000_48000|\
			     SNDRV_PCM_RATE_96000|\
			     SNDRV_PCM_RATE_192000|\
			     SNDRV_PCM_RATE_KNOT)

enum sun8i_type {
	SUN8I_TYPE_A33 = 0,
	SUN8I_TYPE_A64,
	SUN8I_TYPE_AC100,
};

enum sun8i_jack_event {
	SUN8I_JACK_INSERTED = 0,
	SUN8I_JACK_REMOVED,
	SUN8I_JACK_BUTTON_PRESSED
};

#define AC100_NUM_SUPPLIES 4

struct sun8i_codec {
	struct regmap			*regmap;
	struct clk			*clk_module;
	struct delayed_work		jackdet_init_work;
	struct delayed_work 		jack_detect_work;
	struct sun8i_jack_detection	*jackdet;
	struct snd_soc_jack 		*jack;
	enum sun8i_type			type;
	enum sun8i_jack_event		jack_event;
	bool				inverted_jackdet;

	struct regmap	*ac100_regmap;
	struct regulator_bulk_data supplies[AC100_NUM_SUPPLIES];
};

static int sun8i_codec_get_hw_rate(struct snd_pcm_hw_params *params)
{
	unsigned int rate = params_rate(params);

	switch (rate) {
	case 8000:
	case 7350:
		return 0x0;
	case 11025:
		return 0x1;
	case 12000:
		return 0x2;
	case 16000:
		return 0x3;
	case 22050:
		return 0x4;
	case 24000:
		return 0x5;
	case 32000:
		return 0x6;
	case 44100:
		return 0x7;
	case 48000:
		return 0x8;
	case 96000:
		return 0x9;
	case 192000:
		return 0xa;
	default:
		return -EINVAL;
	}
}

static int sun8i_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(dai->component);
	unsigned int reg = SUN8I_AIF_CLK_CTRL(dai->id);
	u32 value;

	if (dai->id < 3) {
		/* clock masters */
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS: /* Codec slave, DAI master */
			value = 0x1;
			break;
		case SND_SOC_DAIFMT_CBM_CFM: /* Codec Master, DAI slave */
			value = 0x0;
			break;
		default:
			return -EINVAL;
		}
		regmap_update_bits(scodec->regmap, reg,
				   BIT(SUN8I_AIF_CLK_CTRL_MSTR_MOD),
				   value << SUN8I_AIF_CLK_CTRL_MSTR_MOD);
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* Normal */
		value = 0x0;
		break;
	case SND_SOC_DAIFMT_NB_IF: /* Inverted LRCK */
		value = 0x1;
		break;
	case SND_SOC_DAIFMT_IB_NF: /* Inverted BCLK */
		value = 0x2;
		break;
	case SND_SOC_DAIFMT_IB_IF: /* Both inverted */
		value = 0x3;
		break;
	default:
		return -EINVAL;
	}
	/*
	 * It appears that the DAI and the codec in the A33 SoC don't
	 * share the same polarity for the LRCK signal when they mean
	 * 'normal' and 'inverted' in the datasheet.
	 *
	 * Since the DAI here is our regular i2s driver that have been
	 * tested with way more codecs than just this one, it means
	 * that the codec probably gets it backward, and we have to
	 * invert the value here.
	 */
	if (scodec->type != SUN8I_TYPE_A64)
		value ^= 0x1;
	regmap_update_bits(scodec->regmap, reg,
			   SUN8I_AIF_CLK_CTRL_CLK_INV_MASK,
			   value << SUN8I_AIF_CLK_CTRL_CLK_INV);

	if (dai->id < 3) {
		/* DAI format */
		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			value = 0x0;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			value = 0x1;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			value = 0x2;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			value = 0x3;
			break;
		default:
			return -EINVAL;
		}
		regmap_update_bits(scodec->regmap, reg,
				   SUN8I_AIF_CLK_CTRL_DATA_FMT_MASK,
				   value << SUN8I_AIF_CLK_CTRL_DATA_FMT);
	}

	return 0;
}

struct sun8i_codec_clk_div {
	u8	div;
	u8	val;
};

static const struct sun8i_codec_clk_div sun8i_codec_bclk_div[] = {
	{ .div = 1,	.val = 0 },
	{ .div = 2,	.val = 1 },
	{ .div = 4,	.val = 2 },
	{ .div = 6,	.val = 3 },
	{ .div = 8,	.val = 4 },
	{ .div = 12,	.val = 5 },
	{ .div = 16,	.val = 6 },
	{ .div = 24,	.val = 7 },
	{ .div = 32,	.val = 8 },
	{ .div = 48,	.val = 9 },
	{ .div = 64,	.val = 10 },
	{ .div = 96,	.val = 11 },
	{ .div = 128,	.val = 12 },
	{ .div = 192,	.val = 13 },
};

static u8 sun8i_codec_get_bclk_div(struct sun8i_codec *scodec,
				   unsigned int rate,
				   unsigned int channels,
				   unsigned int word_size)
{
	unsigned long clk_rate = clk_get_rate(scodec->clk_module);
	unsigned int div = clk_rate / rate / word_size / channels;
	unsigned int best_val = 0, best_diff = ~0;
	int i;

	for (i = 0; i < ARRAY_SIZE(sun8i_codec_bclk_div); i++) {
		const struct sun8i_codec_clk_div *bdiv = &sun8i_codec_bclk_div[i];
		unsigned int diff = abs(bdiv->div - div);

		if (diff < best_diff) {
			best_diff = diff;
			best_val = bdiv->val;
		}
	}

	return best_val;
}

static int sun8i_codec_get_lrck_div(unsigned int channels,
				    unsigned int word_size)
{
	unsigned int div = word_size * channels;

	if (div < 16)
		div = 16;
	if (div > 256)
		return -EINVAL;

	return ilog2(div) - 4;
}

static int sun8i_codec_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(dai->component);
	unsigned int slot_width = params_physical_width(params);
	unsigned int channels = params_channels(params);
	unsigned int reg = SUN8I_AIF_CLK_CTRL(dai->id);
	int sample_rate, lrck_div;
	u8 bclk_div;
	u32 value;

	/*
	 * There should be at least two slots in each frame, or else the codec
	 * cuts off the first bit of each sample, and often de-syncs.
	 */
	if (channels == 1)
		slot_width *= 2;

	if (dai->id < 3) {
		bclk_div = sun8i_codec_get_bclk_div(scodec, params_rate(params),
						    channels, slot_width);
		regmap_update_bits(scodec->regmap, reg,
				   SUN8I_AIF_CLK_CTRL_BCLK_DIV_MASK,
				   bclk_div << SUN8I_AIF_CLK_CTRL_BCLK_DIV);

		lrck_div = sun8i_codec_get_lrck_div(channels, slot_width);
		if (lrck_div < 0)
			return lrck_div;

		regmap_update_bits(scodec->regmap, reg,
				   SUN8I_AIF_CLK_CTRL_LRCK_DIV_MASK,
				   lrck_div << SUN8I_AIF_CLK_CTRL_LRCK_DIV);
	} else {
		regmap_update_bits(scodec->regmap, reg,
				   SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_MASK,
				   SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF2);
	}

	switch (params_width(params)) {
	case 8:
		value = 0x0;
		break;
	case 16:
		value = 0x1;
		break;
	case 20:
		value = 0x2;
		break;
	case 24:
		value = 0x3;
		break;
	default:
		return -EINVAL;
	}
	regmap_update_bits(scodec->regmap, reg,
			   SUN8I_AIF_CLK_CTRL_WORD_SIZ_MASK,
			   value << SUN8I_AIF_CLK_CTRL_WORD_SIZ);

	if (dai->id < 3) {
		value = channels == 1;
		regmap_update_bits(scodec->regmap, reg,
				   BIT(SUN8I_AIF_CLK_CTRL_MONO_PCM),
				   value << SUN8I_AIF_CLK_CTRL_MONO_PCM);

		sample_rate = sun8i_codec_get_hw_rate(params);
		if (sample_rate < 0)
			return sample_rate;

		regmap_update_bits(scodec->regmap, SUN8I_SYS_SR_CTRL,
				   SUN8I_SYS_SR_CTRL_AIF_FS_MASK(dai->id),
				   sample_rate << SUN8I_SYS_SR_CTRL_AIF_FS(dai->id));
	}

	return 0;
}

static const struct snd_soc_dai_ops sun8i_codec_dai_ops = {
	.hw_params = sun8i_codec_hw_params,
	.set_fmt = sun8i_set_fmt,
};

static struct snd_soc_dai_driver sun8i_codec_dais[] = {
	{
		.name = "sun8i-codec-aif1",
		.id = 1,
		/* playback capabilities */
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
		},
		/* capture capabilities */
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
			.sig_bits = 24,
		},
		/* pcm operations */
		.ops = &sun8i_codec_dai_ops,
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "sun8i-codec-aif2",
		.id = 2,
		/* playback capabilities */
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
		},
		/* capture capabilities */
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
			.sig_bits = 24,
		},
		/* pcm operations */
		.ops = &sun8i_codec_dai_ops,
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "sun8i-codec-aif3",
		.id = 3,
		/* playback capabilities */
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
		},
		/* capture capabilities */
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
			.sig_bits = 24,
		},
		/* pcm operations */
		.ops = &sun8i_codec_dai_ops,
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},
};

static int sun8i_codec_aif2clk_event(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol, int event)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(w->dapm->component);

	/* Set SYSCLK clock source to AIF2CLK if AIF2 is running. */
	regmap_update_bits(scodec->regmap, SUN8I_SYSCLK_CTL,
			   BIT(SUN8I_SYSCLK_CTL_SYSCLK_SRC),
			   SND_SOC_DAPM_EVENT_ON(event) ?
			   SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF2CLK :
			   SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF1CLK);

	return 0;
}

static const DECLARE_TLV_DB_SCALE(sun8i_codec_vol_scale, -12000, 75, 1);

static const struct snd_kcontrol_new sun8i_codec_controls[] = {
	SOC_DOUBLE_TLV("AIF1 AD0 Capture Volume",
		       SUN8I_AIF1_VOL_CTRL1,
		       SUN8I_AIF1_VOL_CTRL1_AD0L_VOL,
		       SUN8I_AIF1_VOL_CTRL1_AD0R_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF1 DA0 Playback Volume",
		       SUN8I_AIF1_VOL_CTRL3,
		       SUN8I_AIF1_VOL_CTRL3_DA0L_VOL,
		       SUN8I_AIF1_VOL_CTRL3_DA0R_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF2 ADC Capture Volume",
		       SUN8I_AIF2_VOL_CTRL1,
		       SUN8I_AIF2_VOL_CTRL1_ADCL_VOL,
		       SUN8I_AIF2_VOL_CTRL1_ADCR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF2 DAC Playback Volume",
		       SUN8I_AIF2_VOL_CTRL2,
		       SUN8I_AIF2_VOL_CTRL2_DACL_VOL,
		       SUN8I_AIF2_VOL_CTRL2_DACR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("ADC Capture Volume",
		       SUN8I_ADC_VOL_CTRL,
		       SUN8I_ADC_VOL_CTRL_ADCL_VOL,
		       SUN8I_ADC_VOL_CTRL_ADCR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("DAC Playback Volume",
		       SUN8I_DAC_VOL_CTRL,
		       SUN8I_DAC_VOL_CTRL_DACL_VOL,
		       SUN8I_DAC_VOL_CTRL_DACR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
};

static const struct snd_kcontrol_new sun8i_aif1_loopback_switch =
	SOC_DAPM_SINGLE("AIF1 Loopback Switch",
			SUN8I_AIF1_DACDAT_CTRL,
			SUN8I_AIF1_DACDAT_CTRL_AIF1_LOOP_ENA, 1, 0);

static const struct snd_kcontrol_new sun8i_aif2_loopback_switch =
	SOC_DAPM_SINGLE("AIF2 Loopback Switch",
			SUN8I_AIF2_DACDAT_CTRL,
			SUN8I_AIF2_DACDAT_CTRL_AIF2_LOOP_ENA, 1, 0);

static const struct snd_kcontrol_new sun8i_aif3_loopback_switch =
	SOC_DAPM_SINGLE("Switch",
			SUN8I_AIF3_DACDAT_CTRL,
			SUN8I_AIF3_DACDAT_CTRL_AIF3_LOOP_ENA, 1, 0);

static const char *const sun8i_aif_stereo_mux_enum_names[] = {
	"Stereo", "Reverse Stereo", "Sum Mono", "Mix Mono"
};

static SOC_ENUM_DOUBLE_DECL(sun8i_aif1_ad0_stereo_mux_enum,
			    SUN8I_AIF1_ADCDAT_CTRL,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_SRC,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif1_ad0_stereo_mux_control =
	SOC_DAPM_ENUM("AIF1 AD0 Stereo Capture Route",
		      sun8i_aif1_ad0_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif1_da0_stereo_mux_enum,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_SRC,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif1_da0_stereo_mux_control =
	SOC_DAPM_ENUM("AIF1 DA0 Stereo Playback Route",
		      sun8i_aif1_da0_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif2_adc_stereo_mux_enum,
			    SUN8I_AIF2_ADCDAT_CTRL,
			    SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_SRC,
			    SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif2_adc_stereo_mux_control =
	SOC_DAPM_ENUM("AIF2 ADC Stereo Capture Route",
		      sun8i_aif2_adc_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif2_dac_stereo_mux_enum,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_SRC,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif2_dac_stereo_mux_control =
	SOC_DAPM_ENUM("AIF2 DAC Stereo Playback Route",
		      sun8i_aif2_dac_stereo_mux_enum);

static const char *const sun8i_aif3_mux_enum_names[] = {
	"None", "AIF2 Left", "AIF2 Right"
};

static SOC_ENUM_SINGLE_DECL(sun8i_aif3_adc_mux_enum,
			    SUN8I_AIF3_PATH_CTRL,
			    SUN8I_AIF3_PATH_CTRL_AIF3_ADC_SRC,
			    sun8i_aif3_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif3_adc_mux_control =
	SOC_DAPM_ENUM("AIF3 ADC Capture Route",
		      sun8i_aif3_adc_mux_enum);

static SOC_ENUM_SINGLE_DECL(sun8i_aif2_dac_mux_enum,
			    SUN8I_AIF3_PATH_CTRL,
			    SUN8I_AIF3_PATH_CTRL_AIF2_DAC_SRC,
			    sun8i_aif3_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif2_dac_mux_control =
	SOC_DAPM_ENUM("AIF3 DAC Playback Route",
		      sun8i_aif2_dac_mux_enum);

static const struct snd_kcontrol_new sun8i_aif1_ad0_mixer_controls[] = {
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer AIF1 DA0 Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF1DA0L,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer AIF2 DAC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACL,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACR, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer ADC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_ADCL,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_ADCR, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer AIF2 DAC Rev Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACR,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACL, 1, 0),
};

static const struct snd_kcontrol_new sun8i_aif2_adc_mixer_controls[] = {
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF1 DA0 Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA0L,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF1 DA1 Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA1L,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA1R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF2 DAC Rev Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF2DACR,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF2DACL, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer ADC Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_ADCL,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_ADCR, 1, 0),
};

static const struct snd_kcontrol_new sun8i_dac_mixer_controls[] = {
	SOC_DAPM_DOUBLE("DAC Mixer AIF1 DA0 Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA0L,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("DAC Mixer AIF1 DA1 Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA1L,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA1R, 1, 0),
	SOC_DAPM_DOUBLE("DAC Mixer AIF2 DAC Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF2DACL,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF2DACR, 1, 0),
	SOC_DAPM_DOUBLE("DAC Mixer ADC Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_ADCL,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_ADCR, 1, 0),
};

static const struct snd_soc_dapm_widget sun8i_codec_dapm_widgets[] = {
	/* AIF Loopback Switches */
	SND_SOC_DAPM_SWITCH("AIF1 Slot 0 Left Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif1_loopback_switch),
	SND_SOC_DAPM_SWITCH("AIF1 Slot 0 Right Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif1_loopback_switch),

	SND_SOC_DAPM_SWITCH("AIF2 Left Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif2_loopback_switch),
	SND_SOC_DAPM_SWITCH("AIF2 Right Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif2_loopback_switch),

	SND_SOC_DAPM_SWITCH("AIF3 Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif3_loopback_switch),

	/* AIF "ADC" Outputs */
	SND_SOC_DAPM_AIF_OUT("AIF1 AD0 Left", "AIF1 Capture", 0,
			     SUN8I_AIF1_ADCDAT_CTRL,
			     SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1 AD0 Right", "AIF1 Capture", 1,
			     SUN8I_AIF1_ADCDAT_CTRL,
			     SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA, 0),

	SND_SOC_DAPM_AIF_OUT("AIF2 ADC Left", "AIF2 Capture", 0,
			     SUN8I_AIF2_ADCDAT_CTRL,
			     SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_ENA, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2 ADC Right", "AIF2 Capture", 1,
			     SUN8I_AIF2_ADCDAT_CTRL,
			     SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_ENA, 0),

	SND_SOC_DAPM_AIF_OUT("AIF3 ADC", "AIF3 Capture", 0,
			     SND_SOC_NOPM, 0, 0),

	/* AIF "ADC" Mono/Stereo Muxes */
	SND_SOC_DAPM_MUX("AIF1 AD0 Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_ad0_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF1 AD0 Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_ad0_stereo_mux_control),

	SND_SOC_DAPM_MUX("AIF2 ADC Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_adc_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF2 ADC Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_adc_stereo_mux_control),

	/* AIF "ADC" Muxes */
	SND_SOC_DAPM_MUX("AIF3 ADC Capture Route", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif3_adc_mux_control),

	/* AIF "ADC" Mixers */
	SOC_MIXER_ARRAY("AIF1 AD0 Left Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),
	SOC_MIXER_ARRAY("AIF1 AD0 Right Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),

	SOC_MIXER_ARRAY("AIF2 ADC Left Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif2_adc_mixer_controls),
	SOC_MIXER_ARRAY("AIF2 ADC Right Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif2_adc_mixer_controls),

	/* AIF "DAC" Muxes */
	SND_SOC_DAPM_MUX("AIF2 DAC Left Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_mux_control),
	SND_SOC_DAPM_MUX("AIF2 DAC Right Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_mux_control),

	/* AIF "DAC" Mono/Stereo Muxes */
	SND_SOC_DAPM_MUX("AIF1 DA0 Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_da0_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF1 DA0 Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_da0_stereo_mux_control),

	SND_SOC_DAPM_MUX("AIF2 DAC Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF2 DAC Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_stereo_mux_control),

	/* AIF "DAC" Inputs */
	SND_SOC_DAPM_AIF_IN("AIF1 DA0 Left", "AIF1 Playback", 0,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA, 0),
	SND_SOC_DAPM_AIF_IN("AIF1 DA0 Right", "AIF1 Playback", 1,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA, 0),

	SND_SOC_DAPM_AIF_IN("AIF2 DAC Left", "AIF2 Playback", 0,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_ENA, 0),
	SND_SOC_DAPM_AIF_IN("AIF2 DAC Right", "AIF2 Playback", 1,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_ENA, 0),

	SND_SOC_DAPM_AIF_IN("AIF3 DAC", "AIF3 Playback", 0,
			    SND_SOC_NOPM, 0, 0),

	/* Main DAC Outputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_PGA("DAC Left", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC Right", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC", SUN8I_DAC_DIG_CTRL,
			    SUN8I_DAC_DIG_CTRL_ENDA, 0, NULL, 0),

	/* Main DAC Mixers */
	SOC_MIXER_ARRAY("DAC Left Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_dac_mixer_controls),
	SOC_MIXER_ARRAY("DAC Right Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_dac_mixer_controls),

	/* Main ADC Inputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_PGA("ADC Left", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADC Right", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ADC", SUN8I_ADC_DIG_CTRL,
			    SUN8I_ADC_DIG_CTRL_ENAD, 0, NULL, 0),

	/* Module Resets */
	SND_SOC_DAPM_SUPPLY("RST AIF1", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST AIF2", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST AIF3", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST ADC", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_ADC, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST DAC", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_DAC, 0, NULL, 0),

	/* Module Clocks */
	SND_SOC_DAPM_SUPPLY("MODCLK AIF1", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK AIF2", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK AIF3", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK ADC", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_ADC, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK DAC", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_DAC, 0, NULL, 0),

	/* Clock Supplies */
	SND_SOC_DAPM_SUPPLY("AIF1CLK", SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_AIF1CLK_ENA, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF2CLK", SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_AIF2CLK_ENA, 0,
			    sun8i_codec_aif2clk_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("SYSCLK", SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_SYSCLK_ENA, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget sun8i_codec_dapm_widgets_sun8i[] = {
	SND_SOC_DAPM_CLOCK_SUPPLY("mod"),
};

static const struct snd_soc_dapm_route sun8i_codec_dapm_routes[] = {
	/* AIF Loopback Routes */
	{ "AIF1 Slot 0 Left Loopback", "AIF1 Loopback Switch", "AIF1 AD0 Left" },
	{ "AIF1 Slot 0 Right Loopback", "AIF1 Loopback Switch", "AIF1 AD0 Right" },

	{ "AIF2 Left Loopback", "AIF2 Loopback Switch", "AIF2 ADC Left" },
	{ "AIF2 Right Loopback", "AIF2 Loopback Switch", "AIF2 ADC Right" },

	{ "AIF3 Loopback", "Switch", "AIF3 ADC" },

	/* AIF "ADC" Output Routes */
	{ "AIF1 AD0 Left", NULL, "AIF1 AD0 Left Stereo Mux" },
	{ "AIF1 AD0 Right", NULL, "AIF1 AD0 Right Stereo Mux" },

	{ "AIF1 AD0 Left", NULL, "AIF1CLK" },
	{ "AIF1 AD0 Right", NULL, "AIF1CLK" },

	{ "AIF2 ADC Left", NULL, "AIF2 ADC Left Stereo Mux" },
	{ "AIF2 ADC Right", NULL, "AIF2 ADC Right Stereo Mux" },

	{ "AIF2 ADC Left", NULL, "AIF2CLK" },
	{ "AIF2 ADC Right", NULL, "AIF2CLK" },

	{ "AIF3 ADC", NULL, "AIF3 ADC Capture Route" },

	{ "AIF3 ADC", NULL, "AIF2CLK" },

	/* AIF "ADC" Mono/Stereo Mux Routes */
	{ "AIF1 AD0 Left Stereo Mux", "Stereo", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Reverse Stereo", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Sum Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Sum Mono", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Mix Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Mix Mono", "AIF1 AD0 Right Mixer" },

	{ "AIF1 AD0 Right Stereo Mux", "Stereo", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Reverse Stereo", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Sum Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Sum Mono", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Mix Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Mix Mono", "AIF1 AD0 Right Mixer" },

	{ "AIF2 ADC Left Stereo Mux", "Stereo", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Reverse Stereo", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Sum Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Sum Mono", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Mix Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Mix Mono", "AIF2 ADC Right Mixer" },

	{ "AIF2 ADC Right Stereo Mux", "Stereo", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Reverse Stereo", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Sum Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Sum Mono", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Mix Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Mix Mono", "AIF2 ADC Right Mixer" },

	/* AIF "ADC" Mux Routes */
	{ "AIF3 ADC Capture Route", "AIF2 Left", "AIF2 ADC Left Mixer" },
	{ "AIF3 ADC Capture Route", "AIF2 Right", "AIF2 ADC Right Mixer" },

	/* AIF "ADC" Mixer Routes */
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Left Stereo Mux" },
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer AIF2 DAC Capture Switch", "AIF2 DAC Left Mux" },
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer ADC Capture Switch", "ADC Left" },
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Right Mux" },

	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Right Stereo Mux" },
	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer AIF2 DAC Capture Switch", "AIF2 DAC Right Mux" },
	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer ADC Capture Switch", "ADC Right" },
	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Left Mux" },

	{ "AIF2 ADC Left Mixer", "AIF2 ADC Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Left Stereo Mux" },
	{ "AIF2 ADC Left Mixer", "AIF2 ADC Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Right Mux" },
	{ "AIF2 ADC Left Mixer", "AIF2 ADC Mixer ADC Capture Switch", "ADC Left" },

	{ "AIF2 ADC Right Mixer", "AIF2 ADC Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Right Stereo Mux" },
	{ "AIF2 ADC Right Mixer", "AIF2 ADC Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Left Mux" },
	{ "AIF2 ADC Right Mixer", "AIF2 ADC Mixer ADC Capture Switch", "ADC Right" },

	/* AIF "DAC" Mux Routes */
	{ "AIF2 DAC Left Mux", "None", "AIF2 DAC Left Stereo Mux" },
	{ "AIF2 DAC Left Mux", "AIF2 Left", "AIF3 DAC" },
	{ "AIF2 DAC Left Mux", "AIF2 Right", "AIF2 DAC Left Stereo Mux" },

	{ "AIF2 DAC Right Mux", "None", "AIF2 DAC Right Stereo Mux" },
	{ "AIF2 DAC Right Mux", "AIF2 Left", "AIF2 DAC Right Stereo Mux" },
	{ "AIF2 DAC Right Mux", "AIF2 Right", "AIF3 DAC" },

	/* AIF "DAC" Mono/Stereo Mux Routes */
	{ "AIF1 DA0 Left Stereo Mux", "Stereo", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Left Stereo Mux", "Reverse Stereo", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Left Stereo Mux", "Sum Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Left Stereo Mux", "Sum Mono", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Left Stereo Mux", "Mix Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Left Stereo Mux", "Mix Mono", "AIF1 DA0 Right" },

	{ "AIF1 DA0 Right Stereo Mux", "Stereo", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Right Stereo Mux", "Reverse Stereo", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Right Stereo Mux", "Sum Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Right Stereo Mux", "Sum Mono", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Right Stereo Mux", "Mix Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Right Stereo Mux", "Mix Mono", "AIF1 DA0 Right" },

	{ "AIF2 DAC Left Stereo Mux", "Stereo", "AIF2 DAC Left" },
	{ "AIF2 DAC Left Stereo Mux", "Reverse Stereo", "AIF2 DAC Right" },
	{ "AIF2 DAC Left Stereo Mux", "Sum Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Left Stereo Mux", "Sum Mono", "AIF2 DAC Right" },
	{ "AIF2 DAC Left Stereo Mux", "Mix Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Left Stereo Mux", "Mix Mono", "AIF2 DAC Right" },

	{ "AIF2 DAC Right Stereo Mux", "Stereo", "AIF2 DAC Right" },
	{ "AIF2 DAC Right Stereo Mux", "Reverse Stereo", "AIF2 DAC Left" },
	{ "AIF2 DAC Right Stereo Mux", "Sum Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Right Stereo Mux", "Sum Mono", "AIF2 DAC Right" },
	{ "AIF2 DAC Right Stereo Mux", "Mix Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Right Stereo Mux", "Mix Mono", "AIF2 DAC Right" },

	/* AIF "DAC" Input Routes */
	{ "AIF1 DA0 Left", NULL, "AIF1 Slot 0 Left Loopback" },
	{ "AIF1 DA0 Right", NULL, "AIF1 Slot 0 Right Loopback" },

	{ "AIF1 DA0 Left", NULL, "AIF1CLK" },
	{ "AIF1 DA0 Right", NULL, "AIF1CLK" },

	{ "AIF2 DAC Left", NULL, "AIF2 Left Loopback" },
	{ "AIF2 DAC Right", NULL, "AIF2 Right Loopback" },

	{ "AIF2 DAC Left", NULL, "AIF2CLK" },
	{ "AIF2 DAC Right", NULL, "AIF2CLK" },

	{ "AIF3 DAC", NULL, "AIF3 Loopback" },

	{ "AIF3 DAC", NULL, "AIF2CLK" },

	/* Main DAC Output Routes */
	{ "DAC Left", NULL, "DAC Left Mixer" },
	{ "DAC Right", NULL, "DAC Right Mixer" },

	{ "DAC Left", NULL, "DAC" },
	{ "DAC Right", NULL, "DAC" },

	/* Main DAC Mixer Routes */
	{ "DAC Left Mixer", "DAC Mixer AIF1 DA0 Playback Switch", "AIF1 DA0 Left Stereo Mux" },
	{ "DAC Left Mixer", "DAC Mixer AIF2 DAC Playback Switch", "AIF2 DAC Left Mux" },
	{ "DAC Left Mixer", "DAC Mixer ADC Playback Switch", "ADC Left" },

	{ "DAC Right Mixer", "DAC Mixer AIF1 DA0 Playback Switch", "AIF1 DA0 Right Stereo Mux" },
	{ "DAC Right Mixer", "DAC Mixer AIF2 DAC Playback Switch", "AIF2 DAC Right Mux" },
	{ "DAC Right Mixer", "DAC Mixer ADC Playback Switch", "ADC Right" },

	/* Main ADC Input Routes */
	{ "ADC Left", NULL, "ADC" },
	{ "ADC Right", NULL, "ADC" },

	/* Module Supply Routes */
	{ "AIF1 AD0 Left", NULL, "RST AIF1" },
	{ "AIF1 AD0 Right", NULL, "RST AIF1" },
	{ "AIF1 DA0 Left", NULL, "RST AIF1" },
	{ "AIF1 DA0 Right", NULL, "RST AIF1" },

	{ "AIF2 ADC Left", NULL, "RST AIF2" },
	{ "AIF2 ADC Right", NULL, "RST AIF2" },
	{ "AIF2 DAC Left", NULL, "RST AIF2" },
	{ "AIF2 DAC Right", NULL, "RST AIF2" },

	/* AIF3 gets its bitclock from AIF2 */
	{ "AIF3 ADC", NULL, "RST AIF2" },
	{ "AIF3 ADC", NULL, "RST AIF3" },
	{ "AIF3 DAC", NULL, "RST AIF2" },
	{ "AIF3 DAC", NULL, "RST AIF3" },

	{ "ADC", NULL, "RST ADC" },
	{ "DAC", NULL, "RST DAC" },

	/* Module Reset Routes */
	{ "RST AIF1", NULL, "MODCLK AIF1" },
	{ "RST AIF2", NULL, "MODCLK AIF2" },
	{ "RST AIF3", NULL, "MODCLK AIF3" },
	{ "RST ADC", NULL, "MODCLK ADC" },
	{ "RST DAC", NULL, "MODCLK DAC" },

	/* Module Clock Routes */
	{ "MODCLK AIF1", NULL, "SYSCLK" },
	{ "MODCLK AIF2", NULL, "SYSCLK" },
	{ "MODCLK AIF3", NULL, "SYSCLK" },
	{ "MODCLK ADC", NULL, "SYSCLK" },
	{ "MODCLK DAC", NULL, "SYSCLK" },

	/* Clock Supply Routes */
	{ "SYSCLK", NULL, "AIF1CLK" },
};

static const struct snd_soc_dapm_route sun8i_codec_dapm_routes_sun8i[] = {
	{ "AIF1CLK", NULL, "mod" },
	{ "AIF2CLK", NULL, "mod" },
};

static int ac100_codec_component_probe(struct snd_soc_component *component);

static int sun8i_codec_component_probe(struct snd_soc_component *component)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	if (scodec->ac100_regmap)
                return ac100_codec_component_probe(component);

	ret = snd_soc_dapm_new_controls(dapm,
					sun8i_codec_dapm_widgets_sun8i,
					ARRAY_SIZE(sun8i_codec_dapm_widgets_sun8i));
	if (ret)
		return ret;

	ret = snd_soc_dapm_add_routes(dapm,
				      sun8i_codec_dapm_routes_sun8i,
				      ARRAY_SIZE(sun8i_codec_dapm_routes_sun8i));
	if (ret)
		return ret;

	/* Set AIF1CLK clock source to PLL */
	regmap_update_bits(scodec->regmap, SUN8I_SYSCLK_CTL,
			   SUN8I_SYSCLK_CTL_AIF1CLK_SRC_MASK,
			   SUN8I_SYSCLK_CTL_AIF1CLK_SRC_PLL);

	/* Set AIF2CLK clock source to PLL */
	regmap_update_bits(scodec->regmap, SUN8I_SYSCLK_CTL,
			   SUN8I_SYSCLK_CTL_AIF2CLK_SRC_MASK,
			   SUN8I_SYSCLK_CTL_AIF2CLK_SRC_PLL);

	/* Set SYSCLK clock source to AIF1CLK */
	regmap_update_bits(scodec->regmap, SUN8I_SYSCLK_CTL,
			   BIT(SUN8I_SYSCLK_CTL_SYSCLK_SRC),
			   SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF1CLK);

	return 0;
}

static void sun8i_codec_component_remove(struct snd_soc_component *component)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(component);
	unsigned int irq_mask = BIT(SUN8I_HMIC_CTRL_1_JACK_IN_IRQ_EN)  |
				BIT(SUN8I_HMIC_CTRL_1_JACK_OUT_IRQ_EN) |
				BIT(SUN8I_HMIC_CTRL_1_MIC_DET_IRQ_EN);

	if (scodec->type == SUN8I_TYPE_A64) {
		/* Disable jack detection interrupts */
		regmap_update_bits(scodec->regmap, SUN8I_HMIC_CTRL_1,
				   irq_mask, 0);
	}
}

static const struct snd_soc_component_driver sun8i_soc_component = {
	.controls		= sun8i_codec_controls,
	.num_controls		= ARRAY_SIZE(sun8i_codec_controls),
	.dapm_widgets		= sun8i_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun8i_codec_dapm_widgets),
	.dapm_routes		= sun8i_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun8i_codec_dapm_routes),
	.probe			= sun8i_codec_component_probe,
	.remove			= sun8i_codec_component_remove,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct regmap_config sun8i_codec_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN8I_DAC_MXR_SRC,
};

static void jackdet_init(struct work_struct *work)
{
	struct sun8i_codec *scodec =
		container_of(work, struct sun8i_codec, jackdet_init_work.work);
	unsigned int irq_mask = BIT(SUN8I_HMIC_CTRL_1_JACK_IN_IRQ_EN) |
				BIT(SUN8I_HMIC_CTRL_1_JACK_OUT_IRQ_EN);

	regmap_write(scodec->regmap, SUN8I_HMIC_STS,
		     0x2 << SUN8I_HMIC_STS_MDATA_DISCARD);
	regmap_write(scodec->regmap, SUN8I_HMIC_CTRL_2,
		     0x10 << SUN8I_HMIC_CTRL_2_MDATA_THRESHOLD);
	regmap_write(scodec->regmap, SUN8I_HMIC_CTRL_1,
		     SUN8I_HMIC_CTRL_1_HMIC_N_MASK);
	regmap_update_bits(scodec->regmap, SUN8I_HMIC_CTRL_1,
			   irq_mask, irq_mask);
}

int sun8i_codec_set_jack_detect(struct sun8i_codec *scodec,
			        struct sun8i_jack_detection *jackdet,
			        struct snd_soc_jack *jack)
{
	if (!jackdet || !jack)
		return -1;

	scodec->jack = jack;
	scodec->jackdet = jackdet;
	queue_delayed_work(system_power_efficient_wq,
			   &scodec->jackdet_init_work,
			   msecs_to_jiffies(300));
	snd_soc_jack_report(scodec->jack, 0, SND_JACK_HEADSET);

	return 0;
}
EXPORT_SYMBOL_GPL(sun8i_codec_set_jack_detect);

static unsigned int jack_read_button(struct sun8i_codec *scodec)
{
	unsigned int status, value;
	int ret = 0;

	regmap_read(scodec->regmap, SUN8I_HMIC_STS, &status);
	status &= SUN8I_HMIC_STS_HMIC_DATA_MASK;
	value = (status >> SUN8I_HMIC_STS_HMIC_DATA);

	if (value < 0x2)
		ret = SND_JACK_BTN_0;
	else if (value < 0x7)
		ret = SND_JACK_BTN_1;
	else if (value < 0x10)
		ret = SND_JACK_BTN_2;

	return ret;
}

static void jack_detect(struct work_struct *work)
{
	struct sun8i_codec *scodec =
		container_of(work, struct sun8i_codec, jack_detect_work.work);
	struct sun8i_jack_detection *jackdet = scodec->jackdet;
	unsigned int value;
	int jack_type;

	if (scodec->jack_event == SUN8I_JACK_INSERTED) {
		jackdet->enable_micdet(jackdet->component, true);
		msleep(600);
		value = jack_read_button(scodec);
		if (value == SND_JACK_BTN_0) {
			jackdet->enable_micdet(jackdet->component, false);
			jack_type = SND_JACK_HEADPHONE;
		} else {
			jack_type = SND_JACK_HEADSET;
			regmap_update_bits(scodec->regmap, SUN8I_HMIC_CTRL_1,
					BIT(SUN8I_HMIC_CTRL_1_MIC_DET_IRQ_EN),
					BIT(SUN8I_HMIC_CTRL_1_MIC_DET_IRQ_EN));
		}
		snd_soc_jack_report(scodec->jack, jack_type, SND_JACK_HEADSET);
	} else if (scodec->jack_event == SUN8I_JACK_REMOVED) {
		regmap_update_bits(scodec->regmap, SUN8I_HMIC_CTRL_1,
				   BIT(SUN8I_HMIC_CTRL_1_MIC_DET_IRQ_EN), 0);
		jackdet->enable_micdet(jackdet->component, false);
		snd_soc_jack_report(scodec->jack, 0, SND_JACK_HEADSET);
	} else if (scodec->jack_event == SUN8I_JACK_BUTTON_PRESSED) {
		value = jack_read_button(scodec);
		snd_soc_jack_report(scodec->jack, value,
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2);
	}
}

static irqreturn_t sun8i_codec_interrupt(int irq, void *dev_id)
{
	struct sun8i_codec *scodec = dev_id;
	unsigned int status, irq_mask;
	irqreturn_t ret = IRQ_NONE;

	regmap_read(scodec->regmap, SUN8I_HMIC_STS, &status);
	irq_mask = BIT(SUN8I_HMIC_STS_JACK_DET_IIRQ) |
		   BIT(SUN8I_HMIC_STS_JACK_DET_OIRQ) |
		   BIT(SUN8I_HMIC_STS_MIC_DET_ST);

	if (status & irq_mask) {
		if (status & BIT(SUN8I_HMIC_STS_JACK_DET_IIRQ)) {
			if (scodec->inverted_jackdet)
				scodec->jack_event = SUN8I_JACK_REMOVED;
			else
				scodec->jack_event = SUN8I_JACK_INSERTED;
		}
		if (status & BIT(SUN8I_HMIC_STS_JACK_DET_OIRQ)) {
			if (scodec->inverted_jackdet)
				scodec->jack_event = SUN8I_JACK_INSERTED;
			else
				scodec->jack_event = SUN8I_JACK_REMOVED;
		}
		if (status & BIT(SUN8I_HMIC_STS_MIC_DET_ST)) {
			scodec->jack_event = SUN8I_JACK_BUTTON_PRESSED;
		}
		queue_delayed_work(system_power_efficient_wq,
				   &scodec->jack_detect_work, 0);
		regmap_update_bits(scodec->regmap, SUN8I_HMIC_STS,
				   irq_mask, irq_mask);
		ret = IRQ_HANDLED;
	}

	return ret;
}

/* AC100 Codec Support (digital parts) */

static int sun8i_codec_ac100_regmap_read(void *context, unsigned int reg, unsigned int *val)
{
	struct sun8i_codec *scodec = context;
	int ret;

	ret = regmap_read(scodec->ac100_regmap, reg / 4, val);
	if (ret == 0)
		pr_err("R%02x => %04x\n", reg / 4, *val);

	return ret;
}

static int sun8i_codec_ac100_regmap_write(void *context, unsigned int reg, unsigned int val)
{
	struct sun8i_codec *scodec = context;

	pr_err("W%02x <= %04x\n", reg / 4, val);

	return regmap_write(scodec->ac100_regmap, reg / 4, val);
}

static struct regmap_bus sun8i_codec_ac100_regmap_bus = {
	.reg_write = sun8i_codec_ac100_regmap_write,
	.reg_read = sun8i_codec_ac100_regmap_read,
};

static const char *const ac100_supply_names[AC100_NUM_SUPPLIES] = {
	"LDOIN",
	"AVCC",
	"VDDIO1",
	"VDDIO2",
};

#define AC100_SYSCLK_CTRL_PLLCLK_ENA_OFF                        15
#define AC100_SYSCLK_CTRL_PLLCLK_ENA_MASK                       BIT(15)
#define AC100_SYSCLK_CTRL_PLLCLK_ENA_DISABLED                   0
#define AC100_SYSCLK_CTRL_PLLCLK_ENA_ENABLED                    BIT(15)
#define AC100_SYSCLK_CTRL_PLLCLK_SRC_OFF                        12
#define AC100_SYSCLK_CTRL_PLLCLK_SRC_MASK                       GENMASK(13, 12)
#define AC100_SYSCLK_CTRL_PLLCLK_SRC_MCLK1                      (0x0 << 12)
#define AC100_SYSCLK_CTRL_PLLCLK_SRC_MCLK2                      (0x1 << 12)
#define AC100_SYSCLK_CTRL_PLLCLK_SRC_BCLK1                      (0x2 << 12)
#define AC100_SYSCLK_CTRL_PLLCLK_SRC_BCLK2                      (0x3 << 12)
#define AC100_SYSCLK_CTRL_I2S1CLK_ENA_OFF                       11
#define AC100_SYSCLK_CTRL_I2S1CLK_ENA_MASK                      BIT(11)
#define AC100_SYSCLK_CTRL_I2S1CLK_ENA_DISABLED                  0
#define AC100_SYSCLK_CTRL_I2S1CLK_ENA_ENABLED                   BIT(11)
#define AC100_SYSCLK_CTRL_I2S1CLK_SRC_OFF                       8
#define AC100_SYSCLK_CTRL_I2S1CLK_SRC_MASK                      GENMASK(9, 8)
#define AC100_SYSCLK_CTRL_I2S1CLK_SRC_MCLK1                     (0x0 << 8)
#define AC100_SYSCLK_CTRL_I2S1CLK_SRC_MCLK2                     (0x1 << 8)
#define AC100_SYSCLK_CTRL_I2S1CLK_SRC_PLL                       (0x2 << 8)
#define AC100_SYSCLK_CTRL_I2S2CLK_ENA_OFF                       7
#define AC100_SYSCLK_CTRL_I2S2CLK_ENA_MASK                      BIT(7)
#define AC100_SYSCLK_CTRL_I2S2CLK_ENA_DISABLED                  0
#define AC100_SYSCLK_CTRL_I2S2CLK_ENA_ENABLED                   BIT(7)
#define AC100_SYSCLK_CTRL_I2S2CLK_SRC_OFF                       4
#define AC100_SYSCLK_CTRL_I2S2CLK_SRC_MASK                      GENMASK(5, 4)
#define AC100_SYSCLK_CTRL_I2S2CLK_SRC_MCLK1                     (0x0 << 4)
#define AC100_SYSCLK_CTRL_I2S2CLK_SRC_MCLK2                     (0x1 << 4)
#define AC100_SYSCLK_CTRL_I2S2CLK_SRC_PLL                       (0x2 << 4)
#define AC100_SYSCLK_CTRL_SYSCLK_ENA_OFF                        3
#define AC100_SYSCLK_CTRL_SYSCLK_ENA_MASK                       BIT(3)
#define AC100_SYSCLK_CTRL_SYSCLK_ENA_DISABLED                   0
#define AC100_SYSCLK_CTRL_SYSCLK_ENA_ENABLED                    BIT(3)
#define AC100_SYSCLK_CTRL_SYSCLK_SRC_OFF                        0
#define AC100_SYSCLK_CTRL_SYSCLK_SRC_MASK                       BIT(0)
#define AC100_SYSCLK_CTRL_SYSCLK_SRC_I2S1CLK                    0
#define AC100_SYSCLK_CTRL_SYSCLK_SRC_I2S2CLK                    BIT(0)


static int ac100_codec_component_probe(struct snd_soc_component *component)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(component);

        // The system clock(SYSCLK) of AC100 must be 512*fs(fs=48KHz or 44.1KHz)

        // Source clocks from the SoC

        regmap_update_bits(scodec->ac100_regmap, AC100_SYSCLK_CTRL,
                            AC100_SYSCLK_CTRL_I2S1CLK_SRC_MASK,
                            AC100_SYSCLK_CTRL_I2S1CLK_SRC_MCLK1);
        regmap_update_bits(scodec->ac100_regmap, AC100_SYSCLK_CTRL,
                            AC100_SYSCLK_CTRL_I2S2CLK_SRC_MASK,
                            AC100_SYSCLK_CTRL_I2S2CLK_SRC_MCLK1);
        regmap_update_bits(scodec->ac100_regmap, AC100_SYSCLK_CTRL,
                            AC100_SYSCLK_CTRL_SYSCLK_SRC_MASK,
                            AC100_SYSCLK_CTRL_SYSCLK_SRC_I2S1CLK);
        return 0;
}

static int sun8i_codec_probe_ac100(struct platform_device *pdev)
{
	struct ac100_dev *ac100 = dev_get_drvdata(pdev->dev.parent);
	struct device* dev = &pdev->dev;
	struct sun8i_codec *scodec;
	int ret, i;

	scodec = devm_kzalloc(dev, sizeof(*scodec), GFP_KERNEL);
	if (!scodec)
		return -ENOMEM;

	scodec->ac100_regmap = ac100->regmap;
	platform_set_drvdata(pdev, scodec);

	// caching is done by the MFD regmap
	sun8i_codec_regmap_config.cache_type = REGCACHE_NONE;

	// we need to create a custom regmap_bus that will map reads/writes to the MFD regmap
	scodec->regmap = __regmap_lockdep_wrapper(__devm_regmap_init,
		 "ac100-regmap-codec", dev,
		  &sun8i_codec_ac100_regmap_bus, scodec,
		  &sun8i_codec_regmap_config);
	if (IS_ERR(scodec->regmap)) {
		dev_err(dev, "Failed to create our regmap\n");
		return PTR_ERR(scodec->regmap);
	}

	for (i = 0; i < ARRAY_SIZE(scodec->supplies); i++)
		scodec->supplies[i].supply = ac100_supply_names[i];

        ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(scodec->supplies),
                                      scodec->supplies);
        if (ret != 0) {
              if (ret != -EPROBE_DEFER)
                       dev_err(dev, "Failed to request supplies: %d\n", ret);
                return ret;
        }

	ret = regulator_bulk_enable(ARRAY_SIZE(scodec->supplies),
				    scodec->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(dev, &sun8i_soc_component,
					      sun8i_codec_dais,
					      ARRAY_SIZE(sun8i_codec_dais));
	if (ret) {
		dev_err(dev, "Failed to register codec\n");
		goto err_disable_reg;
	}

	return ret;

err_disable_reg:
	regulator_bulk_disable(ARRAY_SIZE(scodec->supplies),
			       scodec->supplies);
	return ret;
}

static int sun8i_codec_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct sun8i_codec *scodec;
	void __iomem *base;
	int irq, ret;

	if (!node) {
		dev_err(&pdev->dev, "of node is missing.\n");
		return -ENODEV;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "x-powers,ac100-codec"))
		return sun8i_codec_probe_ac100(pdev);

	scodec = devm_kzalloc(&pdev->dev, sizeof(*scodec), GFP_KERNEL);
	if (!scodec)
		return -ENOMEM;

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (irq < 0)
		return irq;

	scodec->clk_module = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(scodec->clk_module)) {
		dev_err(&pdev->dev, "Failed to get the module clock\n");
		return PTR_ERR(scodec->clk_module);
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Failed to map the registers\n");
		return PTR_ERR(base);
	}

	scodec->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "bus", base,
						   &sun8i_codec_regmap_config);
	if (IS_ERR(scodec->regmap)) {
		dev_err(&pdev->dev, "Failed to create our regmap\n");
		return PTR_ERR(scodec->regmap);
	}

	scodec->type = (uintptr_t)of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, scodec);

	if (scodec->type == SUN8I_TYPE_A64) {
		ret = devm_request_irq(&pdev->dev, irq, sun8i_codec_interrupt,
				       0, "sun8i-jackdet-irq", scodec);
		if (ret) {
			dev_err(&pdev->dev, "Failed to setup interrupt\n");
			return ret;
		}

		scodec->inverted_jackdet = of_property_read_bool(node,
					"allwinner,inverted-jack-detection");
		INIT_DELAYED_WORK(&scodec->jackdet_init_work, jackdet_init);
		INIT_DELAYED_WORK(&scodec->jack_detect_work, jack_detect);
	}

	return devm_snd_soc_register_component(&pdev->dev, &sun8i_soc_component,
					       sun8i_codec_dais,
					       ARRAY_SIZE(sun8i_codec_dais));
}

static int sun8i_codec_remove(struct platform_device *pdev)
{
	struct sun8i_codec *scodec = platform_get_drvdata(pdev);

	if (scodec->ac100_regmap) {
		regulator_bulk_disable(ARRAY_SIZE(scodec->supplies),
				       scodec->supplies);
		return 0;
	}

	return 0;
}

static const struct of_device_id sun8i_codec_of_match[] = {
	{
		.compatible = "allwinner,sun8i-a33-codec",
		.data = (void *)SUN8I_TYPE_A33,
	},
	{
		.compatible = "allwinner,sun50i-a64-codec",
		.data = (void *)SUN8I_TYPE_A64,
	},
	{
		.compatible = "x-powers,ac100-codec",
		.data = (void *)SUN8I_TYPE_AC100,
	},
	{}
};
MODULE_DEVICE_TABLE(of, sun8i_codec_of_match);

static struct platform_driver sun8i_codec_driver = {
	.driver = {
		.name = "sun8i-codec",
		.of_match_table = sun8i_codec_of_match,
	},
	.probe = sun8i_codec_probe,
	.remove = sun8i_codec_remove,
};
module_platform_driver(sun8i_codec_driver);

MODULE_DESCRIPTION("Allwinner A33 (sun8i) codec driver");
MODULE_AUTHOR("Mylène Josserand <mylene.josserand@free-electrons.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun8i-codec");

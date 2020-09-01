/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BCM2835 ISP driver
 *
 * Copyright © 2019-2020 Raspberry Pi (Trading) Ltd.
 *
 * Author: Naushir Patuck (naush@raspberrypi.com)
 *
 */

#ifndef BCM2835_ISP_FMTS
#define BCM2835_ISP_FMTS

#include <linux/videodev2.h>
#include "vchiq-mmal/mmal-encodings.h"

struct bcm2835_isp_fmt {
	u32 fourcc;
	int depth;
	int bytesperline_align;
	u32 flags;
	u32 mmal_fmt;
	int size_multiplier_x2;
	enum v4l2_colorspace colorspace;
	unsigned int step_size;
};

struct bcm2835_isp_fmt_list {
	struct bcm2835_isp_fmt const **list;
	unsigned int num_entries;
};

static const struct bcm2835_isp_fmt supported_formats[] = {
	{
		/* YUV formats */
		.fourcc		    = V4L2_PIX_FMT_YUV420,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_I420,
		.size_multiplier_x2 = 3,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_YVU420,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_YV12,
		.size_multiplier_x2 = 3,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_NV12,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_NV12,
		.size_multiplier_x2 = 3,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_NV21,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_NV21,
		.size_multiplier_x2 = 3,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_YUYV,
		.depth		    = 16,
		.bytesperline_align = 64,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_YUYV,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_UYVY,
		.depth		    = 16,
		.bytesperline_align = 64,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_UYVY,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_YVYU,
		.depth		    = 16,
		.bytesperline_align = 64,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_YVYU,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_VYUY,
		.depth		    = 16,
		.bytesperline_align = 64,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_VYUY,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SMPTE170M,
		.step_size	    = 2,
	}, {
		/* RGB formats */
		.fourcc		    = V4L2_PIX_FMT_RGB24,
		.depth		    = 24,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_RGB24,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SRGB,
		.step_size	    = 1,
	}, {
		.fourcc		    = V4L2_PIX_FMT_RGB565,
		.depth		    = 16,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_RGB16,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SRGB,
		.step_size	    = 1,
	}, {
		.fourcc		    = V4L2_PIX_FMT_BGR24,
		.depth		    = 24,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BGR24,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SRGB,
		.step_size	    = 1,
	}, {
		.fourcc		    = V4L2_PIX_FMT_ABGR32,
		.depth		    = 32,
		.bytesperline_align = 64,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BGRA,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_SRGB,
		.step_size	    = 1,
	}, {
		/* Bayer formats */
		/* 8 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB8,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SRGGB8,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SBGGR8,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SBGGR8,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGRBG8,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGRBG8,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGBRG8,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGBRG8,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 10 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB10P,
		.depth		    = 10,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SRGGB10P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SBGGR10P,
		.depth		    = 10,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SBGGR10P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGRBG10P,
		.depth		    = 10,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGRBG10P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGBRG10P,
		.depth		    = 10,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGBRG10P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 12 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB12P,
		.depth		    = 12,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SRGGB12P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SBGGR12P,
		.depth		    = 12,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SBGGR12P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGRBG12P,
		.depth		    = 12,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGRBG12P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGBRG12P,
		.depth		    = 12,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGBRG12P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 14 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB14P,
		.depth		    = 14,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SRGGB14P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SBGGR14P,
		.depth		    = 14,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SBGGR14P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGRBG14P,
		.depth		    = 14,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGRBG14P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGBRG14P,
		.depth		    = 14,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGBRG14P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 16 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB16,
		.depth		    = 16,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SRGGB16,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SBGGR16,
		.depth		    = 16,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SBGGR16,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGRBG16,
		.depth		    = 16,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGRBG16,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_PIX_FMT_SGBRG16,
		.depth		    = 16,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_BAYER_SGBRG16,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* Monochrome MIPI formats */
		/* 8 bit */
		.fourcc		    = V4L2_PIX_FMT_GREY,
		.depth		    = 8,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_GREY,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 10 bit */
		.fourcc		    = V4L2_PIX_FMT_Y10P,
		.depth		    = 10,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_Y10P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 12 bit */
		.fourcc		    = V4L2_PIX_FMT_Y12P,
		.depth		    = 12,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_Y12P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 14 bit */
		.fourcc		    = V4L2_PIX_FMT_Y14P,
		.depth		    = 14,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_Y14P,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		/* 16 bit */
		.fourcc		    = V4L2_PIX_FMT_Y16,
		.depth		    = 16,
		.bytesperline_align = 32,
		.flags		    = 0,
		.mmal_fmt	    = MMAL_ENCODING_Y16,
		.size_multiplier_x2 = 2,
		.colorspace	    = V4L2_COLORSPACE_RAW,
		.step_size	    = 2,
	}, {
		.fourcc		    = V4L2_META_FMT_BCM2835_ISP_STATS,
		.mmal_fmt	    = MMAL_ENCODING_BRCM_STATS,
		/* The rest are not valid fields for stats. */
	}
};

#endif

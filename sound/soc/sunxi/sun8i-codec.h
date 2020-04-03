/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Collabora Ltd
 */

#ifndef __SUN8I_CODEC_H__
#define __SUN8I_CODEC_H__

#include <sound/soc.h>

struct sun8i_codec;

struct sun8i_jack_detection {
	struct snd_soc_component *component;
	void (*enable_micdet)(struct snd_soc_component *component, bool enable);
};

int sun8i_codec_set_jack_detect(struct sun8i_codec *codec,
				struct sun8i_jack_detection *jackdet);

#endif /* __SUN8I_CODEC_H__ */

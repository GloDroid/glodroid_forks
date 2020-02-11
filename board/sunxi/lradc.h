#pragma once

enum {
	KEY_NONE = 0,
	KEY_VOLUMEDOWN = 1,
	KEY_VOLUMEUP = 2,
};

int lradc_get_pressed_key(void);
void lradc_enable(void);
void lradc_disable(void);

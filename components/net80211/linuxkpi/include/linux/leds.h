#ifndef _LINUXKPI_LINUX_LEDS_H_
#define _LINUXKPI_LINUX_LEDS_H_
#include "../../linuxkpi_core.h"

/* ieee80211_create_tpt_led_trigger's table type (throughput blink). */
struct ieee80211_tpt_blink {
	u64	throughput;
	u32	blink_time;
};

#endif

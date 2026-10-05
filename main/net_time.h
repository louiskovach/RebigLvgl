#pragma once
#include <stdbool.h>

/* Wi-Fi (credentials from menuconfig -> Filter HMI) + SNTP "Auto Network Time". */
void        net_time_init(void);
void        net_time_apply(bool enable);   /* start/stop automatic time sync */
bool        net_time_available(void);      /* Wi-Fi configured */
const char *net_time_status(void);
bool        net_link_up(void);
const char *net_ip(void);

/* main/net_time.h: SNTP, so the alarm clock has the time whenever Wi-Fi is up. */
#pragma once

/* Starts the SNTP client once (the IP got an address). The system clock it sets is what app_time_read() reports; a PC
 * can still set it over USB, and the next SNTP sync (hourly) overrides that. No-op when already started. */
void net_time_start(void);

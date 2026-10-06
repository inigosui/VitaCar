#include "sysinfo.h"

#ifdef __vita__

#include <psp2/power.h>
#include <psp2/rtc.h>

int sys_battery_percent(void)
{
    return scePowerGetBatteryLifePercent();
}

bool sys_battery_charging(void)
{
    return scePowerIsBatteryCharging();
}

void sys_local_time(int *hour, int *minute)
{
    SceDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    *hour = t.hour;
    *minute = t.minute;
}

#else /* Compilación en PC */

#include <time.h>

int sys_battery_percent(void)
{
    return 100;
}

bool sys_battery_charging(void)
{
    return false;
}

void sys_local_time(int *hour, int *minute)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    *hour = t->tm_hour;
    *minute = t->tm_min;
}

#endif

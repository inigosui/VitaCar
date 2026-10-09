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

void sys_local_date(int *year, int *month, int *day)
{
    SceDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    *year = t.year;
    *month = t.month;
    *day = t.day;
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

void sys_local_date(int *year, int *month, int *day)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    *year = t->tm_year + 1900;
    *month = t->tm_mon + 1;
    *day = t->tm_mday;
}

#endif

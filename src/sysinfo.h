#pragma once

#include <stdbool.h>

int  sys_battery_percent(void);
bool sys_battery_charging(void);
void sys_local_time(int *hour, int *minute);

/*
 * fand
 *
 * Copyright 2016-present Celestica. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Daemon to manage the fan speed to ensure that we stay within a reasonable
 * temperature range.  We're using a simplistic algorithm to get started:
 *
 * If the fan is already on high, we'll move it to medium if we fall below
 * a top temperature.  If we're on medium, we'll move it to high
 * if the temperature goes over the top value, and to low if the
 * temperature falls to a bottom level.  If the fan is on low,
 * we'll increase the speed if the temperature rises to the top level.
 *
 * To ensure that we're not just turning the fans up, then back down again,
 * we'll require an extra few degrees of temperature drop before we lower
 * the fan speed.
 *
 * We check the RPM of the fans against the requested RPMs to determine
 * whether the fans are failing, in which case we'll turn up all of
 * the other fans and report the problem..
 *
 * TODO:  Implement a PID algorithm to closely track the ideal temperature.
 * TODO:  Determine if the daemon is already started.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <dirent.h>
#include <fcntl.h>
#include "fand_v2.h"

//#define DEBUG
//#define FOR_F2B
//#define FANCTRL_SIMULATION 1
//#define CONFIG_PSU_FAN_CONTROL_INDEPENDENT 1
#define CONFIG_FSC_CONTROL_PID 1 //for PID control

#define TOTAL_FANS 4
#define TOTAL_PSUS 2
#define FAN_MEDIUM 128
#define FAN_HIGH 100
#define FAN_MAX 255
#define FAN_MIN 76
#define FAN_NORMAL_MAX 178
#define FAN_NORMAL_MIN FAN_MIN
#define FAN_ONE_FAIL_MAX FAN_MAX
#define FAN_ONE_FAIL_MIN 153
#define RAISING_TEMP_LOW 25
#define RAISING_TEMP_HIGH 40
#define FALLING_TEMP_LOW 23
#define FALLING_TEMP_HIGH 40
#define SYSTEM_LIMIT 80

#define ALARM_TEMP_THRESHOLD 1
#define ALARM_START_REPORT 3
#define WARN_RECOVERY_COUNT 90 //remain 5 minutes to recovery normal speed

#define CRITICAL_TEMP_HYST 2

#define REPORT_TEMP 720  /* Report temp every so many cycles */
#define FAN_FAILURE_OFFSET 30
#define FAN_FAILURE_THRESHOLD 3 /* How many times can a fan fail */
#define SYS_FAN_LED_PATH "/sys/bus/i2c/drivers/syscpld/70-000d/fan_led_ctrl_en"
#define SYS_FAN_LED_GREEN 0
#define SYS_FAN_LED_RED 1
#define FAN_LED_GREEN 1
#define FAN_LED_RED 2
#define PSU_LED_GREEN 0
#define PSU_LED_RED 1
#define SHUTDOWN_DELAY_TIME 72 /*if trigger shutdown event, delay 6 minutes to shutdown */

#define BAD_TEMP (-60)
#define ERROR_TEMP_MAX 5

#define FAN_FAIL_COUNT 9
#define FAN_FAIL_RPM 1000
#define FAN_FRONTT_SPEED_MAX 24150
#define FAN_REAR_SPEED_MAX 28950
#define PSU_SPEED_MAX 26496

#define FAN_DIR_INIT -1
#define FAN_DIR_FAULT 0
#define FAN_DIR_B2F 1
#define FAN_DIR_F2B 2
#define THERMAL_DIR_F2B_STR "R1241-F9019-01"
#define THERMAL_DIR_B2F_STR "Undefined"
#define FAN_DIR_F2B_STR "R1241-FN019-013JW"
#define FAN_DIR_B2F_STR "R1241-F9002"
#define DELTA_PSU_DIR_F2B_STR "DPS-1100FB"
#define DELTA_PSU_DIR_B2F_STR "DPS-1100AB"
#define ACBEL_PSU_DIR_F2B_STR "FSJ026-A20G"
#define ACBEL_PSU_DIR_B2F_STR "FSJ038-A20G"

#define PWM_UNIT_MAX 255

#define FAN_WDT_TIME (0x3c) //5 * 60
#define FAN_WDT_ENABLE_SYSFS "/sys/bus/i2c/drivers/fancpld/66-000d/wdt_en"
#define FAN_WDT_TIME_SYSFS "/sys/bus/i2c/drivers/fancpld/66-000d/wdt_time"
#define PSU1_SHUTDOWN_SYSFS "/sys/bus/i2c/drivers/dps1100/76-0059/control/shutdown"
#define PSU2_SHUTDOWN_SYSFS "/sys/bus/i2c/drivers/dps1100/75-0058/control/shutdown"
#define PSU_SPEED_CTRL_NODE "fan1_cfg"
#define PSU_SPEED_CTRL_ENABLE 0x90

#define PID_CONFIG_PATH "/usr/local/etc/pid_config_questone2bd.ini"
#define PID_FILE_LINE_MAX 100

#define DISABLE 0
#define LOW_WARN_BIT (0x1 << 0)
#define HIGH_WARN_BIT (0x1 << 1)
#define PID_CTRL_BIT (0x1 << 2)
#define SWITCH_SENSOR_BIT (0x1 << 3)
#define CRITICAL_SENSOR_BIT (0x1 << 4)
#define HIGH_MAX_BIT (0x1 << 5)

#define NORMAL_K        ((float)(FAN_NORMAL_MAX - FAN_NORMAL_MIN) / (RAISING_TEMP_HIGH - RAISING_TEMP_LOW))
#define ONE_FAIL_K      ((float)(FAN_ONE_FAIL_MAX - FAN_ONE_FAIL_MIN) / (RAISING_TEMP_HIGH - RAISING_TEMP_LOW))

static int calculate_fan_normal_pwm(int cur_temp, int last_temp);
static int calculate_fan_one_fail_pwm(int cur_temp, int last_temp);

static int read_temp_sysfs(struct sensor_info_sysfs *sensor);
static int read_temp_directly_sysfs(struct sensor_info_sysfs *sensor);
static void get_direction_str(int direction, char *message);

struct line_policy fishbone48_f2b_normal = {
    .temp_hyst = CRITICAL_TEMP_HYST,
    .begin = {
        .temp = RAISING_TEMP_LOW,
        .speed = FAN_NORMAL_MIN,
    },
    .end = {
        .temp = RAISING_TEMP_HIGH,
        .speed = FAN_NORMAL_MAX,
    },
    .get_speed = calculate_line_speed,
};

struct line_policy fishbone48_f2b_onefail = {
    .temp_hyst = CRITICAL_TEMP_HYST,
    .begin = {
        .temp = RAISING_TEMP_LOW,
        .speed = FAN_ONE_FAIL_MIN,
    },
    .end = {
        .temp = RAISING_TEMP_HIGH,
        .speed = FAN_ONE_FAIL_MAX,
    },
    .get_speed = calculate_line_speed,
};

struct line_policy fishbone48_b2f_normal = {
    .temp_hyst = CRITICAL_TEMP_HYST,
    .begin = {
        .temp = RAISING_TEMP_LOW,
        .speed = FAN_NORMAL_MIN,
    },
    .end = {
        .temp = RAISING_TEMP_HIGH,
        .speed = FAN_NORMAL_MAX,
    },
    .get_speed = calculate_line_speed,
};

struct line_policy fishbone48_b2f_onefail = {
    .temp_hyst = CRITICAL_TEMP_HYST,
    .begin = {
        .temp = RAISING_TEMP_LOW,
        .speed = FAN_ONE_FAIL_MIN,
    },
    .end = {
        .temp = RAISING_TEMP_HIGH,
        .speed = FAN_ONE_FAIL_MAX,
    },
    .get_speed = calculate_line_speed,
};

/* Fan EEPROM Path: Index is matters
 * See fantray_info[]
 */
const char *fan_eeprom_path[] = {
    /* FAN tray EEPROM */
    "/sys/bus/i2c/devices/2-0050/eeprom",
    "/sys/bus/i2c/devices/4-0050/eeprom",
    "/sys/bus/i2c/devices/6-0050/eeprom",
    "/sys/bus/i2c/devices/8-0050/eeprom",
    /* PSU EEPROM */
    "/sys/bus/i2c/devices/76-0051/eeprom",
    "/sys/bus/i2c/devices/75-0050/eeprom",
};

static struct sensor_info_sysfs sensor_inlet_u52_critical_info = {
    .prefix = "/sys/bus/i2c/drivers/lm75/9-0048",
    .suffix = "temp1_input",
    .error_cnt = 0,
    .temp = 0,
    .t1 = 0,
    .t2 = 0,
    .old_pwm = 0,
    .setpoint = 0,
    .p = 0,
    .i = 0,
    .d = 0,
    .min_output = FAN_MIN,
    .max_output = FAN_MAX,
    .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs sensor_inlet_u28_critical_info = {
    .prefix = "/sys/bus/i2c/drivers/lm75/67-004d",
    .suffix = "temp1_input",
    .error_cnt = 0,
    .temp = 0,
    .t1 = 0,
    .t2 = 0,
    .old_pwm = 0,
    .setpoint = 0,
    .p = 0,
    .i = 0,
    .d = 0,
    .min_output = FAN_MIN,
    .max_output = FAN_MAX,
    .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs sensor_bcm5870_inlet_critical_info_f2b = {
    .prefix = "/sys/bus/i2c/drivers/syscpld/70-000d",
    .suffix = "temp1_input",
    .error_cnt = 0,
    .temp = 0,
    .t1 = 0,
    .t2 = 0,
    .old_pwm = 0,
    .setpoint = 95,
    .p = 3,
    .i = 0.3,
    .d = 0.3,
    .min_output = FAN_MIN,
    .max_output = FAN_MAX,
    .read_sysfs = &read_temp_directly_sysfs,
};

static struct sensor_info_sysfs sensor_bcm5870_inlet_critical_info_b2f = {
    .prefix = "/sys/bus/i2c/drivers/syscpld/70-000d",
    .suffix = "temp1_input",
    .error_cnt = 0,
    .temp = 0,
    .t1 = 0,
    .t2 = 0,
    .old_pwm = 0,
    .setpoint = 95,
    .p = 3,
    .i = 0.5,
    .d = 0.5,
    .min_output = FAN_MIN,
    .max_output = FAN_MAX,
    .read_sysfs = &read_temp_directly_sysfs,
};

static struct sensor_info_sysfs sensor_cpu_inlet_critical_info_f2b = {
    .prefix = "/sys/bus/i2c/drivers/syscpld/70-000d",
    .suffix = "temp2_input",
    .error_cnt = 0,
    .temp = 0,
    .t1 = 0,
    .t2 = 0,
    .old_pwm = 0,
    .setpoint = -15,
    .p = 2,
    .i = 0.5,
    .d = 0.5,
    .min_output = FAN_MIN,
    .max_output = FAN_MAX,
    .read_sysfs = &read_temp_directly_sysfs,
};

static struct sensor_info_sysfs sensor_cpu_inlet_critical_info_b2f = {
    .prefix = "/sys/bus/i2c/drivers/syscpld/70-000d",
    .suffix = "temp2_input",
    .error_cnt = 0,
    .temp = 0,
    .t1 = 0,
    .t2 = 0,
    .old_pwm = 0,
    .setpoint = -15,
    .p = 2,
    .i = 0.3,
    .d = 0.3,
    .min_output = FAN_MIN,
    .max_output = FAN_MAX,
    .read_sysfs = &read_temp_directly_sysfs,
};


/* fantray info*/
static struct fan_info_stu_sysfs fan4_info = {
    .prefix = "/sys/bus/i2c/drivers/fancpld/66-000d",
    .front_fan_prefix = "fan1_input",
    .rear_fan_prefix = "fan2_input",
    .pwm_prefix = "fan1_pwm",
    .fan_led_prefix = "fan1_led",
    .fan_present_prefix = "fan1_present",
    .fan_status_prefix = NULL,
    //.present = 1,
    .front_failed = 0,
    .rear_failed = 0,
};

static struct fan_info_stu_sysfs fan3_info = {
    .prefix = "/sys/bus/i2c/drivers/fancpld/66-000d",
    .front_fan_prefix = "fan3_input",
    .rear_fan_prefix = "fan4_input",
    .pwm_prefix = "fan2_pwm",
    .fan_led_prefix = "fan2_led",
    .fan_present_prefix = "fan2_present",
    .fan_status_prefix = NULL,
    //.present = 1,
    .front_failed = 0,
    .rear_failed = 0,
};

static struct fan_info_stu_sysfs fan2_info = {
    .prefix = "/sys/bus/i2c/drivers/fancpld/66-000d",
    .front_fan_prefix = "fan5_input",
    .rear_fan_prefix = "fan6_input",
    .pwm_prefix = "fan3_pwm",
    .fan_led_prefix = "fan3_led",
    .fan_present_prefix = "fan3_present",
    .fan_status_prefix = NULL,
    //.present = 1,
    .front_failed = 0,
    .rear_failed = 0,
};

static struct fan_info_stu_sysfs fan1_info = {
    .prefix = "/sys/bus/i2c/drivers/fancpld/66-000d",
    .front_fan_prefix = "fan7_input",
    .rear_fan_prefix = "fan8_input",
    .pwm_prefix = "fan4_pwm",
    .fan_led_prefix = "fan4_led",
    .fan_present_prefix = "fan4_present",
    .fan_status_prefix = NULL,
    //.present = 1,
    .front_failed = 0,
    .rear_failed = 0,
};

static struct fan_info_stu_sysfs psu2_fan_info = {
    .prefix = "/sys/bus/i2c/drivers/syscpld/70-000d",
    .front_fan_prefix = "fan1_input",
    .rear_fan_prefix = "/sys/bus/i2c/drivers/dps1100/75-0058",
    .pwm_prefix = "fan1_pct",
    .fan_led_prefix = "psu_l_led_ctrl_en",
    .fan_present_prefix = "psu_l_present",
    .fan_status_prefix = "psu_l_status",
    //.present = 1,
    .front_failed = 0,
    .rear_failed = 0,
};

static struct fan_info_stu_sysfs psu1_fan_info = {
    .prefix = "/sys/bus/i2c/drivers/syscpld/70-000d",
    .front_fan_prefix = "fan1_input",
    .rear_fan_prefix = "/sys/bus/i2c/drivers/dps1100/76-0059",
    .pwm_prefix = "fan1_pct",
    .fan_led_prefix = "psu_r_led_ctrl_en",
    .fan_present_prefix = "psu_r_present",
    .fan_status_prefix = "psu_r_status",
    //.present = 1,
    .front_failed = 0,
    .rear_failed = 0,
};




/************board and fantray info*****************/
static struct board_info_stu_sysfs board_info[] = {
    /*B2F*/
    {
        .name = "INLET_TEMP",
        .slot_id = FAN_DIR_B2F,
        .correction = -1,
        .lwarn = 40,
        .hwarn = 43,
        .warn_count = 0,
        .recovery_count = 0,
        .flag = CRITICAL_SENSOR_BIT,
        .critical = &sensor_inlet_u52_critical_info,
        .alarm = &sensor_inlet_u52_critical_info,
    },
#ifdef CONFIG_FSC_CONTROL_PID
    {
        .name = "SWITCH_TEMP",
        .slot_id = FAN_DIR_B2F,
        .correction = 15,
        .lwarn = 108,
        .hwarn = 112,
        .warn_count = 0,
        .recovery_count = 0,
        .flag = PID_CTRL_BIT,
        .critical = &sensor_bcm5870_inlet_critical_info_b2f,
        .alarm = &sensor_bcm5870_inlet_critical_info_b2f,
    },
    {
        .name = "CPU_TEMP",
        .slot_id = FAN_DIR_B2F,
        .correction = -104,
        .lwarn = -3,
        .hwarn = -1,
        .warn_count = 0,
        .recovery_count = 0,
        .flag = PID_CTRL_BIT,//PID_CTRL_BIT,
        .critical = &sensor_cpu_inlet_critical_info_b2f,
        .alarm = &sensor_cpu_inlet_critical_info_b2f,
    },
#endif

    /*F2B*/
    {
        .name = "INLET_TEMP",
        .slot_id = FAN_DIR_F2B,
        .correction = -6,
        .lwarn = 40,
        .hwarn = 43,
        .warn_count = 0,
        .recovery_count = 0,
        .flag = CRITICAL_SENSOR_BIT,
        .critical = &sensor_inlet_u28_critical_info,
        .alarm = &sensor_inlet_u28_critical_info,
    },
#ifdef CONFIG_FSC_CONTROL_PID
    {
        .name = "SWITCH_TEMP",
        .slot_id = FAN_DIR_F2B,
        .correction = 15,
        .lwarn = 108,
        .hwarn = 112,
        .warn_count = 0,
        .recovery_count = 0,
        .flag = PID_CTRL_BIT,
        .critical = &sensor_bcm5870_inlet_critical_info_f2b,
        .alarm = &sensor_bcm5870_inlet_critical_info_f2b,
    },
    {
        .name = "CPU_TEMP",
        .slot_id = FAN_DIR_F2B,
        .correction = -104,
        .lwarn = -3,
        .hwarn = -1,
        .warn_count = 0,
        .recovery_count = 0,
        .flag = PID_CTRL_BIT,//PID_CTRL_BIT,
        .critical = &sensor_cpu_inlet_critical_info_f2b,
        .alarm = &sensor_cpu_inlet_critical_info_f2b,
    },
#endif
    NULL,
};

static struct fantray_info_stu_sysfs fantray_info[] = {
    {
        .name = "FAN1",
        .present = 1,
        .read_eeprom = 1,
        .status = 1,
        .failed = 0,
        .direction = FAN_DIR_INIT,
        .eeprom_fail = 0,
        .fan1 = fan1_info,
    },
    {
        .name = "FAN2",
        .present = 1,
        .read_eeprom = 1,
        .status = 1,
        .failed = 0,
        .direction = FAN_DIR_INIT,
        .eeprom_fail = 0,
        .fan1 = fan2_info,
    },
    {
        .name = "FAN3",
        .present = 1,
        .read_eeprom = 1,
        .status = 1,
        .failed = 0,
        .direction = FAN_DIR_INIT,
        .eeprom_fail = 0,
        .fan1 = fan3_info,
    },
    {
        .name = "FAN4",
        .present = 1,
        .read_eeprom = 1,
        .status = 1,
        .failed = 0,
        .direction = FAN_DIR_INIT,
        .eeprom_fail = 0,
        .fan1 = fan4_info,
    },
    {
        .name = "PSU1",
        .present = 1,
        .read_eeprom = 1,
        .status = 1,
        .failed = 0,
        .direction = FAN_DIR_INIT,
        .eeprom_fail = 0,
        .fan1 = psu1_fan_info,
    },
    {
        .name = "PSU2",
        .present = 1,
        .read_eeprom = 1,
        .status = 1,
        .failed = 0,
        .direction = FAN_DIR_INIT,
        .eeprom_fail = 0,
        .fan1 = psu2_fan_info,
    },
    NULL,
};

#define BOARD_INFO_SIZE (sizeof(board_info) \
                        / sizeof(struct board_info_stu_sysfs))
#define FANTRAY_INFO_SIZE (sizeof(fantray_info) \
                        / sizeof(struct fantray_info_stu_sysfs))

struct rpm_to_pct_map rpm_front_map[] = {{20, 4950},
    {25, 6150},
    {30, 7500},
    {35, 8700},
    {40, 9900},
    {45, 11250},
    {50, 12300},
    {55, 13650},
    {60, 14850},
    {65, 16050},
    {70, 17400},
    {75, 18600},
    {80, 19950},
    {85, 21000},
    {90, 22350},
    {95, 23550},
    {100, 24150}
};

struct rpm_to_pct_map rpm_rear_map[] = {{20, 6000},
    {25, 7500},
    {30, 8850},
    {35, 10500},
    {40, 12150},
    {45, 13350},
    {50, 14850},
    {55, 16650},
    {60, 18000},
    {65, 19350},
    {70, 20850},
    {75, 22350},
    {80, 24000},
    {85, 25350},
    {90, 26850},
    {95, 28350},
    {100, 28950}
};

struct rpm_to_pct_map psu_rpm_map[] = {{20, 8800},
    {25, 8800},
    {30, 8800},
    {35, 8800},
    {40, 8800},
    {45, 9920},
    {50, 11520},
    {55, 13120},
    {60, 14560},
    {65, 16192},
    {70, 17760},
    {75, 19296},
    {80, 20800},
    {85, 21760},
    {90, 23424},
    {95, 24800},
    {100, 26496}
};

#define FRONT_MAP_SIZE (sizeof(rpm_front_map) / sizeof(struct rpm_to_pct_map))
#define REAR_MAP_SIZE (sizeof(rpm_rear_map) / sizeof(struct rpm_to_pct_map))
#define PSU_MAP_SIZE (sizeof(psu_rpm_map) / sizeof(struct rpm_to_pct_map))

static struct thermal_policy f2b_normal_policy = {
    .pwm = FAN_NORMAL_MIN,
    .old_pwm = FAN_NORMAL_MIN,
    .line = &fishbone48_f2b_normal,
};

static struct thermal_policy f2b_one_fail_policy = {
    .pwm = FAN_ONE_FAIL_MIN,
    .old_pwm = FAN_ONE_FAIL_MIN,
    .line = &fishbone48_f2b_onefail,
};

static struct thermal_policy b2f_normal_policy = {
    .pwm = FAN_NORMAL_MIN,
    .old_pwm = FAN_NORMAL_MIN,
    .line = &fishbone48_b2f_normal,
};

static struct thermal_policy b2f_one_fail_policy = {
    .pwm = FAN_ONE_FAIL_MIN,
    .old_pwm = FAN_ONE_FAIL_MIN,
    .line = &fishbone48_b2f_onefail,
};

/* Global variables */
static struct thermal_policy *policy = NULL;
static int pid_using = 0;
static int direction = FAN_DIR_INIT;
static int sys_fan_led_color = 0;
static int psu_led_color = 0;
static int fan_speed_temp = FAN_MEDIUM;

static int write_fan_led(const int fan, const int color);
static int write_fan_speed(const int fan, const int value);
static int write_psu_fan_speed(const int fan, int value);

/*
 * Initialize path cache by writing 0-length string
 */
static int init_path_cache(void)
{
    int i = 0;
    // Temp Sensor datastructure
    for (i = 0; i < BOARD_INFO_SIZE; i++)
    {
        if (board_info[i].alarm != NULL)
            snprintf(board_info[i].alarm->path_cache, PATH_CACHE_SIZE, "");
        if (board_info[i].critical != NULL)
            snprintf(board_info[i].critical->path_cache, PATH_CACHE_SIZE, "");
    }

    return 0;
}

/*
 * Helper function to probe directory, and make full path
 * Will probe directory structure, then make a full path
 * using "<prefix>/hwmon/hwmonxxx/<suffix>"
 * returns < 0, if hwmon directory does not exist or something goes wrong
 */
int assemble_sysfs_path(const char* prefix, const char* suffix,
                        char* full_name, int buffer_size)
{
    int rc = 0;
    int dirname_found = 0;
    char temp_str[PATH_CACHE_SIZE];
    DIR *dir = NULL;
    struct dirent *ent;

    if (full_name == NULL)
        return -1;

    snprintf(temp_str, (buffer_size - 1), "%s/hwmon", prefix);
    dir = opendir(temp_str);
    if (dir == NULL) {
        rc = ENOENT;
        goto close_dir_out;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, "hwmon")) {
            // found the correct 'hwmon??' directory
            snprintf(full_name, buffer_size, "%s/%s/%s",
                     temp_str, ent->d_name, suffix);
            dirname_found = 1;
            break;
        }
    }

close_dir_out:
    if (dir != NULL) {
        closedir(dir);
    }

    if (dirname_found == 0) {
        rc = ENOENT;
    }

    return rc;
}

static int adjust_sysnode_path(const char* prefix, const char* suffix,
                               char* full_name, int buffer_size)
{
    int rc = 0;
    FILE *fp;
    int dirname_found = 0;
    char temp_str[PATH_CACHE_SIZE];
    DIR *dir = NULL;
    struct dirent *ent;

    if (full_name == NULL)
        return -1;
    snprintf(temp_str, (buffer_size - 1), "%s/%s", prefix, suffix);
    fp = fopen(temp_str, "r");
    if (fp) {
        fclose(fp);
        return 0;
    }

    /*adjust the path, because the hwmon id may be changed*/
    snprintf(temp_str, (buffer_size - 1), "%s/hwmon", prefix);
    dir = opendir(temp_str);
    if (dir == NULL) {
        rc = ENOENT;
        goto close_dir_out;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, "hwmon")) {
            // found the correct 'hwmon??' directory
            snprintf(full_name, buffer_size, "%s/%s/%s",
                     temp_str, ent->d_name, suffix);
            dirname_found = 1;
            break;
        }
    }

close_dir_out:
    if (dir != NULL) {
        closedir(dir);
    }

    if (dirname_found == 0) {
        rc = ENOENT;
    }

    return rc;

}

// Functions for reading from sysfs stub
static int read_sysfs_raw_internal(const char *device, char *value, int log)
{
    FILE *fp;
    int rc, err;

    fp = fopen(device, "r");
    if (!fp) {
        if (log) {
            err = errno;
            syslog(LOG_ERR, "failed to open device %s for read: %s",
                   device, strerror(err));
            errno = err;
        }
        return -1;
    }

    rc = fscanf(fp, "%s", value);
    fclose(fp);

    if (rc != 1) {
        if (log) {
            err = errno;
            syslog(LOG_ERR, "failed to read device %s: %s",
                   device, strerror(err));
            errno = err;
        }
        return -1;
    }

    return 0;
}

static int read_sysfs_raw(char *sysfs_path, char *buffer)
{
    return read_sysfs_raw_internal(sysfs_path, buffer, 1);
}

// Returns 0 for success, or -1 on failures.
static int read_sysfs_int(char *sysfs_path, int *buffer)
{
    int rc;
    char readBuf[PATH_CACHE_SIZE];

    if (sysfs_path == NULL || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    rc = read_sysfs_raw(sysfs_path, readBuf);
    if (rc == 0)
    {
        if (strstr(readBuf, "0x") || strstr(readBuf, "0X"))
            sscanf(readBuf, "%x", buffer);
        else
            sscanf(readBuf, "%d", buffer);
    }
    return rc;
}

static int write_sysfs_raw_internal(const char *device, char *value, int log)
{
    FILE *fp;
    int rc, err;

    fp = fopen(device, "w");
    if (!fp) {
        if (log) {
            err = errno;
            syslog(LOG_ERR, "failed to open device %s for write : %s",
                   device, strerror(err));
            errno = err;
        }
        return err;
    }

    rc = fputs(value, fp);
    fclose(fp);

    if (rc < 0) {
        if (log) {
            err = errno;
            syslog(LOG_ERR, "failed to write to device %s", device);
            errno = err;
        }
        return -1;
    }

    return 0;
}

static int write_sysfs_raw(const char *sysfs_path, char* buffer)
{
    return write_sysfs_raw_internal(sysfs_path, buffer, 1);
}

// Returns 0 for success, or -1 on failures.
static int write_sysfs_int(const char *sysfs_path, int buffer)
{
    int rc;
    char writeBuf[PATH_CACHE_SIZE];

    if (sysfs_path == NULL) {
        errno = EINVAL;
        return -1;
    }

    snprintf(writeBuf, PATH_CACHE_SIZE, "%d", buffer);
    return write_sysfs_raw(sysfs_path, writeBuf);
}

static int read_temp_directly_sysfs(struct sensor_info_sysfs *sensor)
{
    int ret;
    int value;
    char fullpath[PATH_CACHE_SIZE];
    bool use_cache = false;
    int cache_str_len = 0;

    if (sensor == NULL) {
        syslog(LOG_ERR, "sensor is null\n");
        return BAD_TEMP;
    }
    // Check if cache is available
    if (sensor->path_cache != NULL) {
        cache_str_len = strlen(sensor->path_cache);
        if (cache_str_len != 0)
            use_cache = true;
    }

    if (use_cache == false) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", sensor->prefix, sensor->suffix);
        // Update cache, if possible.
        if (sensor->path_cache != NULL)
            snprintf(sensor->path_cache, (PATH_CACHE_SIZE - 1), "%s", fullpath);
        use_cache = true;
    }
    /*
    * By the time control reaches here, use_cache is always true
    * or this function already returned -1. So assume the cache is always on
    */
    ret = read_sysfs_int(sensor->path_cache, &value);

    /*  Note that Kernel sysfs stub pre-converts raw value in xxxxx format,
    *  which is equivalent to xx.xxx degree - all we need to do is to divide
    *  the read value by 1000
    */
    if (ret < 0)
        value = ret;
    else
        value = value / 1000;

    if (value < 0) {
        syslog(LOG_ERR, "failed to read temperature bus %s", fullpath);
        return BAD_TEMP;
    }

    usleep(11000);
    return value;
}


static int read_temp_sysfs(struct sensor_info_sysfs *sensor)
{
    int ret;
    int value;
    char fullpath[PATH_CACHE_SIZE];
    bool use_cache = false;
    int cache_str_len = 0;

    if (sensor == NULL) {
        syslog(LOG_ERR, "sensor is null\n");
        return BAD_TEMP;
    }
    // Check if cache is available
    if (sensor->path_cache != NULL) {
        cache_str_len = strlen(sensor->path_cache);
        if (cache_str_len != 0)
            use_cache = true;
    }

    if (use_cache == false) {
        // No cached value yet. Calculate the full path first
        ret = assemble_sysfs_path(sensor->prefix, sensor->suffix, fullpath, sizeof(fullpath));
        if (ret != 0) {
            syslog(LOG_ERR, "%s: I2C bus %s not available. Failed reading %s\n", __FUNCTION__, sensor->prefix, sensor->suffix);
            return BAD_TEMP;
        }
        // Update cache, if possible.
        if (sensor->path_cache != NULL)
            snprintf(sensor->path_cache, (PATH_CACHE_SIZE - 1), "%s", fullpath);
        use_cache = true;
    }

    /*
    * By the time control reaches here, use_cache is always true
    * or this function already returned -1. So assume the cache is always on
    */
    ret = read_sysfs_int(sensor->path_cache, &value);

    /*  Note that Kernel sysfs stub pre-converts raw value in xxxxx format,
    *  which is equivalent to xx.xxx degree - all we need to do is to divide
    *  the read value by 1000
    */
    if (ret < 0)
        value = ret;
    else
        value = value / 1000;

    if (value < 0) {
        syslog(LOG_ERR, "failed to read temperature bus %s", fullpath);
        return BAD_TEMP;
    }

    usleep(11000);
    return value;
}


static int read_critical_max_temp(void)
{
    int i;
    int temp, max_temp = BAD_TEMP;
#ifdef FANCTRL_SIMULATION
    static float t = 50;
    static float div = -0.25;
#endif

    struct board_info_stu_sysfs *info;

    for (i = 0; i < BOARD_INFO_SIZE; i++) {
        info = &board_info[i];
        if (info->slot_id != direction)
            continue;
        if (info->critical && (info->flag & CRITICAL_SENSOR_BIT)) {
            temp = info->critical->read_sysfs(info->critical);
            if (temp != BAD_TEMP) {
                if (info->critical->error_cnt)
                    syslog(LOG_WARNING, "%s is NORMAL", info->name);
                info->critical->error_cnt = 0;
                temp += info->correction;
                if ((info->critical->t2 == BAD_TEMP) || (info->critical->t2 == 0))
                    info->critical->t2 = temp;
                else
                    info->critical->t2 = info->critical->t1;
                if ((info->critical->t1 == BAD_TEMP) || (info->critical->t1 == 0))
                    info->critical->t1 = temp;
                else
                    info->critical->t1 = info->critical->temp;
                info->critical->temp = temp;
            } else {
                if (info->critical->error_cnt < ERROR_TEMP_MAX)
                    info->critical->error_cnt++;
                if (info->critical->error_cnt == 1)
                    syslog(LOG_WARNING, "Sensor [%s] temp lost detected", info->name);
            }
            if (info->critical->temp > max_temp)
                max_temp = info->critical->temp;
        }
    }
#ifdef FANCTRL_SIMULATION
    if (t <= 15) {
        div = 0.25;
    } else if (t >= 50) {
        div = -0.25;
    }
    t += div;
    max_temp = (int)t;
#endif
#ifdef DEBUG
    syslog(LOG_DEBUG, "[zmzhan]%s: critical: max_temp=%d", __func__, max_temp);
#endif
    return max_temp;
}

static int calculate_line_pwm(void)
{
    int max_pwm = 0;
    int pwm = 0;
    struct board_info_stu_sysfs *info;
    int i;
    for (i = 0; i < BOARD_INFO_SIZE; i++) {
        info = &board_info[i];
        if (info->slot_id != direction)
            continue;
        if (info->critical && (info->flag & CRITICAL_SENSOR_BIT)) {
            if (info->critical->error_cnt) {
                if (info->critical->error_cnt == ERROR_TEMP_MAX) {
                    if (policy->old_pwm != FAN_MAX)
                        syslog(LOG_ERR, "%s status is ABNORMAL, get %s failed", info->name, info->name);
                    pwm = FAN_MAX;
                }
                else {
                    pwm = policy->old_pwm;
                }
            } else {
                pwm = policy->line->get_speed(info->critical, policy->line);
            }
            if (max_pwm < pwm)
                max_pwm = pwm;
        }
    }

    return max_pwm;
}

static int read_pid_max_temp(void)
{
    int i;
    int temp, max_temp = BAD_TEMP;
    struct board_info_stu_sysfs *info;

    for (i = 0; i < BOARD_INFO_SIZE; i++) {
        info = &board_info[i];
        if (info->slot_id != direction)
            continue;
        if (info->critical && (info->flag & PID_CTRL_BIT)) {
            temp = info->critical->read_sysfs(info->critical);
            if (temp != BAD_TEMP) {
                if (info->critical->error_cnt)
                    syslog(LOG_WARNING, "%s is NORMAL", info->name);
                info->critical->error_cnt = 0;
                temp += info->correction;
                if ((info->critical->t2 == BAD_TEMP) || (info->critical->t2 == 0))
                    info->critical->t2 = temp;
                else
                    info->critical->t2 = info->critical->t1;
                if ((info->critical->t1 == BAD_TEMP) || (info->critical->t1 == 0))
                    info->critical->t1 = temp;
                else
                    info->critical->t1 = info->critical->temp;
                info->critical->temp = temp;
            } else {
                if (info->critical->error_cnt < ERROR_TEMP_MAX) {
                    info->critical->error_cnt++;
                    if (info->critical->error_cnt == 1)
                        syslog(LOG_WARNING, "Sensor [%s] temp lost detected", info->name);
                }
            }
            if (info->critical->temp > max_temp)
                max_temp = info->critical->temp;
#ifdef DEBUG
            syslog(LOG_DEBUG, "[zmzhan]%s: %s: temp=%d", __func__, info->name, temp);
#endif
        }
    }

    return max_temp;
}

static int calculate_pid_pwm(int fan_pwm)
{
    int i;
    int pwm, max_pwm = 0;
    struct board_info_stu_sysfs *info;
    struct sensor_info_sysfs *critical;

    for (i = 0; i < BOARD_INFO_SIZE; i++) {
        info = &board_info[i];
        if (info->slot_id != direction)
            continue;
        if (info->critical && (info->flag & PID_CTRL_BIT)) {
            critical = info->critical;
            critical->old_pwm = fan_pwm;

            if (critical->error_cnt) {
                if (critical->error_cnt == ERROR_TEMP_MAX) {
                    if (critical->old_pwm != FAN_MAX)
                        syslog(LOG_ERR, "%s status is ABNORMAL, get %s failed", info->name, info->name);
                    pwm = FAN_MAX;
                }
                else {
                    pwm = critical->old_pwm;
                }
            } else {
                pwm = critical->old_pwm + critical->p * (critical->temp - critical->t1)
                    + critical->i * (critical->temp - critical->setpoint)
                    + critical->d * (critical->temp + critical->t2 - 2 * critical->t1);
            }
#ifdef DEBUG
            syslog(LOG_DEBUG, "[zmzhan]%s: %s: pwm=%d, old_pwm=%d, p=%f, i=%f, d=%f, setpoint=%f \
                                temp=%d, t1=%d, t2=%d", __func__, info->name, pwm, critical->old_pwm, critical->p,
                   critical->i, critical->d, critical->setpoint, critical->temp, critical->t1, critical->t2);
#endif
            if (pwm < critical->min_output)
                pwm = critical->min_output;
            if (pwm > max_pwm)
                max_pwm = pwm;
            if (max_pwm > critical->max_output)
                max_pwm = critical->max_output;
#ifdef DEBUG
            syslog(LOG_DEBUG, "[zmzhan]%s: %s: pwm=%d, old_pwm=%d, p=%f, i=%f, d=%f, setpoint=%f \
                                temp=%d, t1=%d, t2=%d", __func__, info->name, pwm, critical->old_pwm, critical->p,
                   critical->i, critical->d, critical->setpoint, critical->temp, critical->t1, critical->t2);
#endif
        }
    }

    return max_pwm;
}


static int alarm_temp_update(int *alarm)
{
    int i, fan;
    int temp, max_temp = 0;
    struct board_info_stu_sysfs *info;
    *alarm &= ~HIGH_MAX_BIT;

    for (i = 0; i < BOARD_INFO_SIZE; i++) {
        info = &board_info[i];
        if ((info->slot_id != direction) || (info->flag == DISABLE))
            continue;
        if (info->alarm) {
            temp = info->alarm->temp;
            if (info->hwarn != BAD_TEMP &&
                    (temp >= info->hwarn || ((info->flag & HIGH_MAX_BIT) &&
                                             (info->hwarn - temp <= ALARM_TEMP_THRESHOLD) && info->warn_count))) {
                if (++info->warn_count >= ALARM_START_REPORT) {
                    if (!(info->flag & HIGH_WARN_BIT))
                        syslog(LOG_ERR, "%s exceeded upper critical, value is %d C, upper critical is %d C",
                               info->name, temp, info->hwarn);
                    for (fan = 0; fan < TOTAL_FANS; fan++) {
                        write_fan_speed(fan, FAN_MAX);
                    }
                    write_psu_fan_speed(fan, FAN_MAX);
                    info->warn_count = 0;
                    info->flag |= (HIGH_WARN_BIT | HIGH_MAX_BIT);
                    info->recovery_count = 0;
                }
            } else if (info->lwarn != BAD_TEMP &&
                       (temp >= info->lwarn || ((info->flag & LOW_WARN_BIT) &&
                                                (info->lwarn - temp <= ALARM_TEMP_THRESHOLD) && info->warn_count))) {
                if (++info->warn_count >= ALARM_START_REPORT) {
                    if (!(info->flag & LOW_WARN_BIT))
                        syslog(LOG_WARNING, "%s exceeded upper high, value is %d C, upper high is %d C",
                               info->name, temp, info->lwarn);
                    info->warn_count = 0;
                    info->flag |= LOW_WARN_BIT;
                    info->recovery_count = 0;
                }
            } else {
                if (info->flag & HIGH_WARN_BIT) {
                    syslog(LOG_INFO, "%s is NORMAL, value is %d C", info->name, temp);
                    info->flag &= ~HIGH_WARN_BIT;
                } else if (info->flag & LOW_WARN_BIT) {
                    syslog(LOG_INFO, "%s is NORMAL, value is %d C", info->name, temp);
                    info->flag &= ~LOW_WARN_BIT;
                } else if (info->flag & HIGH_MAX_BIT) {
                    info->recovery_count++;
                    if (info->recovery_count >= WARN_RECOVERY_COUNT) {
                        info->flag &= ~HIGH_MAX_BIT;
                        syslog(LOG_INFO, "%s is NORMAL, set fan normal speed", info->name);
                    }
#ifdef DEBUG
                    syslog(LOG_DEBUG, "[xuth] Major max bit: %d, recovery count: %d", *alarm & HIGH_MAX_BIT ? 1 : 0, info->recovery_count);
#endif
                }
            }

            if (info->flag & HIGH_MAX_BIT)
                *alarm |= HIGH_MAX_BIT;
        }
    }
    if (*alarm & HIGH_MAX_BIT)
        fan_speed_temp = FAN_MAX;

    return max_temp;
}

static inline int check_fan_normal_pwm(int pwm)
{
    if (pwm > FAN_NORMAL_MAX)
        return FAN_NORMAL_MAX;
    if (pwm < FAN_NORMAL_MIN)
        return FAN_NORMAL_MIN;
    return pwm;
}

static inline int check_fan_one_fail_pwm(int pwm)
{
    if (pwm > FAN_ONE_FAIL_MAX)
        return FAN_ONE_FAIL_MAX;
    if (pwm < FAN_ONE_FAIL_MIN)
        return FAN_ONE_FAIL_MIN;
    return pwm;
}

static inline int check_fan_speed(int speed, struct line_policy *line)
{
    if (speed > line->end.speed)
        return line->end.speed;
    if (speed < line->begin.speed)
        return line->begin.speed;
    return speed;
}

static inline float get_line_k(struct point begin, struct point end)
{
    return (float)(end.speed - begin.speed) / (end.temp - begin.temp);
}

static inline int check_fall_temp(int temp, struct line_policy *line)
{
    if (temp > line->end.temp)
        return line->end.temp;
    if (temp < line->begin.temp)
        return line->begin.temp;
    return temp;
}

static inline int get_fall_temp(int speed, struct line_policy *line)
{
    float k = get_line_k(line->begin, line->end);
    int fall_temp = (speed + 1 - line->begin.speed) / k + line->begin.temp;
    return check_fall_temp(fall_temp, line);
}

static int calculate_line_speed(struct sensor_info_sysfs *sensor, struct line_policy *line)
{
    float k = get_line_k(line->begin, line->end);
    int fall_temp = get_fall_temp(policy->old_pwm, line);
    int speed;
    int cur_temp = sensor->temp;
    int old_temp = sensor->t1;

    if (cur_temp > old_temp) {
        speed = (int)(k * (cur_temp - line->begin.temp) + line->begin.speed);
#ifdef DEBUG
        syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d cal_last_temp=%d k=%f Raising line_pwm=%d",
               __func__, cur_temp, fall_temp, k, speed);
#endif
    } else {
        if (fall_temp - cur_temp <= line->temp_hyst) {
            speed = (int)(k * (fall_temp - line->begin.temp) + line->begin.speed);
        } else {
            speed = (int)(k * (cur_temp - line->begin.temp) + line->begin.speed);
        }
#ifdef DEBUG
        syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d cal_last_temp=%d k=%f Falling line_pwm=%d",
               __func__, cur_temp, fall_temp, k, speed);
#endif
    }

    return check_fan_speed(speed, line);
}

int calculate_pid_speed(struct pid_policy *pid)
{
    int output = 0;
    output = pid->last_output + pid->kp * (pid->cur_temp - pid->t1) +
             pid->ki * (pid->cur_temp - pid->set_point) +
             pid->kd * (pid->cur_temp + pid->t2 - 2 * pid->t1);
    if (output > pid->max_output) {
        return pid->max_output;
    }
    if (output < pid->min_output) {
        return pid->min_output;
    }
    return output;
}

static int calculate_fan_normal_pwm(int cur_temp, int last_temp)
{
    int value;
    int fall_temp = (policy->old_pwm + 1 - FAN_NORMAL_MIN) / NORMAL_K + RAISING_TEMP_LOW;
    if (fall_temp > RAISING_TEMP_HIGH) fall_temp = RAISING_TEMP_HIGH;
    if (fall_temp < RAISING_TEMP_LOW) fall_temp = RAISING_TEMP_LOW;

    if (cur_temp >= fall_temp) {
        value = (int)(NORMAL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_NORMAL_MIN);
#ifdef DEBUG
        syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f Raising line_pwm=%d",
               __func__, cur_temp, last_temp, fall_temp, NORMAL_K, value);
#endif
    } else {
        if (fall_temp - cur_temp <= CRITICAL_TEMP_HYST) {
            value = (int)(NORMAL_K * (fall_temp - RAISING_TEMP_LOW) + FAN_NORMAL_MIN);
        }
        else {
            value = (int)(NORMAL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_NORMAL_MIN);
        }
#ifdef DEBUG
        syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f Falling line_pwm=%d",
               __func__, cur_temp, last_temp, fall_temp, NORMAL_K, value);
#endif
    }

    return check_fan_normal_pwm(value);
}

static int calculate_fan_one_fail_pwm(int cur_temp, int last_temp)
{
    int value;
    int fall_temp = (policy->old_pwm + 1 - FAN_ONE_FAIL_MIN) / ONE_FAIL_K + RAISING_TEMP_LOW;
    if (fall_temp > RAISING_TEMP_HIGH) fall_temp = RAISING_TEMP_HIGH;
    if (fall_temp < RAISING_TEMP_LOW) fall_temp = RAISING_TEMP_LOW;

    if (cur_temp >= fall_temp) {
        value = (int)(ONE_FAIL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_ONE_FAIL_MIN);
#ifdef DEBUG
        syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f One fail raising line_pwm=%d",
               __func__, cur_temp, last_temp, fall_temp, ONE_FAIL_K, value);
#endif
    } else {
        if (fall_temp - cur_temp <= CRITICAL_TEMP_HYST) {
            value = (int)(ONE_FAIL_K * (fall_temp - RAISING_TEMP_LOW) + FAN_ONE_FAIL_MIN);
        }
        else {
            value = (int)(ONE_FAIL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_ONE_FAIL_MIN);
        }
#ifdef DEBUG
        syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f One fail falling line_pwm=%d",
               __func__, cur_temp, last_temp, fall_temp, ONE_FAIL_K, value);
#endif
    }

    return check_fan_one_fail_pwm(value);
}

#define PSU_FAN_LOW 45
static int calculate_psu_raising_fan_pwm(int temp)
{
    int slope;
    int val;

    if (temp < RAISING_TEMP_LOW) {
        return PSU_FAN_LOW;
    } else if (temp >= RAISING_TEMP_LOW && temp < RAISING_TEMP_HIGH) {
        slope = (FAN_HIGH - PSU_FAN_LOW) / (RAISING_TEMP_HIGH - RAISING_TEMP_LOW);
        val = PSU_FAN_LOW + slope * temp;
        return val;
    } else  {
        return FAN_HIGH;
    }
    return FAN_HIGH;
}

static int calculate_psu_falling_fan_pwm(int temp)
{
    int slope;
    int val;

    if (temp < FALLING_TEMP_LOW) {
        return PSU_FAN_LOW;
    } else if (temp >= FALLING_TEMP_LOW && temp < FALLING_TEMP_HIGH) {
        slope = (FAN_HIGH - PSU_FAN_LOW) / (FALLING_TEMP_HIGH - FALLING_TEMP_LOW);
        val = PSU_FAN_LOW + slope * temp;
        return val;
    } else  {
        return FAN_HIGH;
    }

    return FAN_HIGH;
}

/*
 * Fan number here is 0-based
 * Note that 1 means present
 */
static int fan_is_present_sysfs(int fan, struct fan_info_stu_sysfs *fan_info)
{
    int ret;
    char buf[PATH_CACHE_SIZE];
    int rc = 0;
    struct fantray_info_stu_sysfs *fantray;
    fantray = &fantray_info[fan];

    snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->fan_present_prefix);

    rc = read_sysfs_int(buf, &ret);
    if (rc < 0) {
        syslog(LOG_ERR, "failed to read module present %s node", fan_info->fan_present_prefix);
        return -1;
    }

    usleep(11000);

    if (ret != 0) {
        if (fantray->present == 1) {
            syslog(LOG_ERR, "%s is ABSENT", fantray->name);
            fantray->present = 0;
            fantray->read_eeprom = 1;
        }
    } else {
        if (fan < TOTAL_FANS) {
            if (fantray->present == 0) {
                syslog(LOG_WARNING, "%s is PRESENT", fantray->name);
                fantray->present = 1;
                fantray->read_eeprom = 1;
            }
            return 1;
        }
        snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->fan_status_prefix);
        rc = read_sysfs_int(buf, &ret);
        if (rc < 0) {
            syslog(LOG_ERR, "failed to read %s status %s node", fantray->name, fan_info->fan_present_prefix);
            return -1;
        }

        usleep(11000);

        if (ret == 0) {
            if ((fantray->present == 1) && (fantray->status == 1)) {
                fantray->status = 0;
                syslog(LOG_ERR, "%s is power off", fantray->name);
            }
            psu_led_color |= (0x1 << (fan - TOTAL_FANS));
        } else {
            if ((fantray->present == 1) && (fantray->status == 0)) {
                fantray->status = 1;
                fantray->read_eeprom = 1;
                syslog(LOG_WARNING, "%s is power on", fantray->name);
            }
            if (fantray->direction != direction)
                psu_led_color |= (0x1 << (fan - TOTAL_FANS));
        }
        if (fantray->present == 0) {
            syslog(LOG_WARNING, "%s is PRESENT", fantray->name);
            fantray->present = 1;
            fantray->read_eeprom = 1;
        }
        return 1;
    }

    if (fan < TOTAL_FANS) {
        sys_fan_led_color |= (0x1 << fan);
    } else {
        psu_led_color |= (0x1 << (fan - TOTAL_FANS));
    }
    return 0;
}


// Note that the fan number here is 0-based
static int set_fan_sysfs(int fan, int value)
{
    int ret;
    struct fantray_info_stu_sysfs *fantray;
    struct fan_info_stu_sysfs *fan_info;

    fantray = &fantray_info[fan];
    fan_info = &fantray->fan1;

    char fullpath[PATH_CACHE_SIZE];

    ret = fan_is_present_sysfs(fan, fan_info);
    if (ret == 0) {
        fantray->present = 0; //not present
        sys_fan_led_color |= (0x1 << fan);
        return -1;
    } else if (ret == 1) {
        fantray->present = 1;
    } else {
        sys_fan_led_color |= (0x1 << fan);
        return -1;
    }

    if (fantray->direction != direction) {
        if (fantray->direction != FAN_DIR_FAULT)
            value = 89;
        sys_fan_led_color |= (0x1 << fan);
    }
    snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->pwm_prefix);
    adjust_sysnode_path(fan_info->prefix, fan_info->pwm_prefix, fullpath, sizeof(fullpath));
    ret = write_sysfs_int(fullpath, value);
    if (ret < 0) {
        syslog(LOG_ERR, "failed to set fan %s/%s, value %#x",
               fan_info->prefix, fan_info->pwm_prefix, value);
        return -1;
    }
    usleep(11000);

    return 0;
}

static int write_fan_led_sysfs(int fan, const int color)
{
    int ret;
    char fullpath[PATH_CACHE_SIZE];
    struct fantray_info_stu_sysfs *fantray;
    struct fan_info_stu_sysfs *fan_info;

    fantray = &fantray_info[fan];
    fan_info = &fantray->fan1;


    ret = fan_is_present_sysfs(fan, fan_info);
    if (ret == 0) {
        fantray->present = 0; //not present
        return -1;
    } else if (ret == 1) {
        fantray->present = 1;
    } else {
        return -1;
    }

    snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->fan_led_prefix);
    ret = write_sysfs_int(fullpath, color);
    if (ret < 0) {
        syslog(LOG_ERR, "failed to set fan %s/%s, value %#x",
               fan_info->prefix, fan_info->fan_led_prefix, color);
        return -1;
    }
    usleep(11000);

    return 0;
}


/* Set fan speed as a percentage */
static int write_fan_speed(const int fan, const int value)
{
    return set_fan_sysfs(fan, value);
}

/* Set up fan LEDs */
static int write_fan_led(const int fan, const int color)
{
    return write_fan_led_sysfs(fan, color);
}

static int write_sys_fan_led(const int color)
{
    int ret;
    char fullpath[PATH_CACHE_SIZE];
    snprintf(fullpath, PATH_CACHE_SIZE, SYS_FAN_LED_PATH);
    ret = write_sysfs_int(fullpath, color);
    if (ret < 0) {
        syslog(LOG_ERR, "failed to set fan %s, value %#x", SYS_FAN_LED_PATH, color);
        return -1;
    }
    usleep(11000);

    return 0;
}

static int get_psu_pwm(void)
{
    int i;
    int ret;
    int pwm = 0, tmp = 0;
    struct fantray_info_stu_sysfs *fantray;
    struct fan_info_stu_sysfs *fan_info;

    for (i = TOTAL_FANS; i < TOTAL_FANS + TOTAL_PSUS; i++) {
        fantray = &fantray_info[i];
        fan_info = &fantray->fan1;

        char fullpath[PATH_CACHE_SIZE];

        ret = fan_is_present_sysfs(i, fan_info);
        if (ret == 0) {
            fantray->present = 0; //not present
            continue;
        } else if (ret == 1) {
            fantray->present = 1;
            if (fantray->status == 0)
                continue;
        } else {
            continue;
        }

        snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, fan_info->pwm_prefix);
        adjust_sysnode_path(fan_info->rear_fan_prefix, fan_info->pwm_prefix, fullpath, sizeof(fullpath));
        read_sysfs_int(fullpath, &tmp);
        if (tmp > 100)
            tmp = 100;
        if (tmp < 0)
            tmp = 0;
        if (tmp > pwm)
            pwm = tmp;
        usleep(11000);
    }

    pwm = pwm * FAN_MAX / 100;

    return pwm;
}
/* Set PSU fan speed as a percentage */
static int write_psu_fan_speed(const int fan, int value)
{
    int i;
    int ret;
    char fullpath[PATH_CACHE_SIZE];
    struct fantray_info_stu_sysfs *fantray;
    struct fan_info_stu_sysfs *fan_info;


    value = value * 100 / FAN_MAX; //convert it to pct
    for (i = TOTAL_FANS; i < TOTAL_FANS + TOTAL_PSUS; i++) {
        fantray = &fantray_info[i];
        fan_info = &fantray->fan1;

        ret = fan_is_present_sysfs(i, fan_info);
        if (ret == 0) {
            fantray->present = 0; //not present
            continue;
        } else if (ret == 1) {
            fantray->present = 1;
            if (fantray->status == 0) //power off
                continue;
        } else {
            continue;
        }
        snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, PSU_SPEED_CTRL_NODE);
        adjust_sysnode_path(fan_info->rear_fan_prefix, PSU_SPEED_CTRL_NODE, fullpath, sizeof(fullpath));
        ret = write_sysfs_int(fullpath, PSU_SPEED_CTRL_ENABLE);
        if (ret < 0) {
            syslog(LOG_ERR, "failed to enable control PSU speed");
        }

        snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, fan_info->pwm_prefix);
        adjust_sysnode_path(fan_info->rear_fan_prefix, fan_info->pwm_prefix, fullpath, sizeof(fullpath));
        if (fantray->direction == direction) {
            ret = write_sysfs_int(fullpath, value);
        } else {
            ret = write_sysfs_int(fullpath, 35);
        }
        if (ret < 0) {
            syslog(LOG_ERR, "failed to set fan %s/%s, value %#x",
                   fan_info->prefix, fan_info->pwm_prefix, value);
            continue;
        }
        usleep(11000);
    }

    return 0;
}

/* Set up fan LEDs */
static int write_psu_fan_led(const int fan, const int color)
{
    int err;

    err = write_fan_led_sysfs(fan, color);

    return err;
}

static int fan_rpm_to_pct(const struct rpm_to_pct_map *table, const int table_len, int rpm)
{
    int i;

    for (i = 0; i < table_len; i++) {
        if (table[i].rpm > rpm) {
            break;
        }
    }

    /*
     * If the fan RPM is lower than the lowest value in the table,
     * we may have a problem -- fans can only go so slow, and it might
     * have stopped.  In this case, we'll return an interpolated
     * percentage, as just returning zero is even more problematic.
     */

    if (i == 0) {
        return (rpm * table[i].pct) / table[i].rpm;
    } else if (i == table_len) { // Fell off the top?
        return table[i - 1].pct;
    }

    // Interpolate the right percentage value:

    int percent_diff = table[i].pct - table[i - 1].pct;
    int rpm_diff = table[i].rpm - table[i - 1].rpm;
    int fan_diff = table[i].rpm - rpm;

    return table[i].pct - (fan_diff * percent_diff / rpm_diff);
}

/*return: 1 OK, 0 not OK*/
int fan_speed_okay(const int fan, int speed, const int slop)
{
    int ret;
    char buf[PATH_CACHE_SIZE];
    int rc = 1;
    int front_speed, front_pct;
    int rear_speed, rear_pct;
    struct fantray_info_stu_sysfs *fantray;
    struct fan_info_stu_sysfs *fan_info;

    fantray = &fantray_info[fan];
    fan_info = &fantray->fan1;

    ret = fan_is_present_sysfs(fan, fan_info);
    if (ret == 0) {
        fantray->present = 0; //not present
        return 0;
    } else if (ret == 1) {
        fantray->present = 1;
    } else {
        return 0;
    }
    if (fantray->direction != direction)
        return 0;
    snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->front_fan_prefix);

    rc = read_sysfs_int(buf, &ret);
    if (rc < 0) {
        syslog(LOG_ERR, "failed to read %s node", fan_info->front_fan_prefix);
        return -1;
    }
    front_speed = ret;
    usleep(11000);
    if (front_speed < FAN_FAIL_RPM) {
        fan_info->front_failed++;
        if (fan_info->front_failed == 1)
            syslog(LOG_WARNING, "%s-1 speed %d, less than %d detected",
                   fantray->name, front_speed, FAN_FAIL_RPM);
        if (fan_info->front_failed == FAN_FAIL_COUNT)
            syslog(LOG_ERR, "%s-1 status is ABNORMAL, speed less than 1000 RPM for over 30 seconds",
                   fantray->name);
        if (fan_info->front_failed > FAN_FAIL_COUNT)
            fan_info->front_failed = FAN_FAIL_COUNT;
    } else if (speed == FAN_MAX && (front_speed < (FAN_FRONTT_SPEED_MAX * (100 - slop) / 100))) {
        fan_info->front_failed++;
        if (fan_info->front_failed == 1)
            syslog(LOG_WARNING, "%s-1 speed %d, less than %d%% of max speed(%d) detected",
                   fantray->name, front_speed, 100 - slop, speed);
        if (fan_info->front_failed == FAN_FAIL_COUNT)
            syslog(LOG_ERR, "%s-1 status is ABNORMAL, speed is set to 100%% but real speed is lower than 70%% of max speed",
                   fantray->name);
        if (fan_info->front_failed > FAN_FAIL_COUNT)
            fan_info->front_failed = FAN_FAIL_COUNT;
    } else {
        if (fan_info->front_failed)
            syslog(LOG_WARNING, "%s-1 status is NORMAL", fantray->name);
        fan_info->front_failed = 0;
    }

    memset(buf, 0, PATH_CACHE_SIZE);
    snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->rear_fan_prefix);

    rc = read_sysfs_int(buf, &ret);
    if (rc < 0) {
        syslog(LOG_ERR, "failed to read %s node", fan_info->front_fan_prefix);
        return -1;
    }
    rear_speed = ret;
    if (rear_speed < FAN_FAIL_RPM) {
        fan_info->rear_failed++;
        if (fan_info->rear_failed == 1)
            syslog(LOG_WARNING, "%s-2 speed %d, less than %d detected",
                   fantray->name, rear_speed, FAN_FAIL_RPM);
        if (fan_info->rear_failed == FAN_FAIL_COUNT)
            syslog(LOG_ERR, "%s-2 status is ABNORMAL, speed less than 1000 RPM for over 30 seconds",
                   fantray->name);
        if (fan_info->rear_failed > FAN_FAIL_COUNT)
            fan_info->rear_failed = FAN_FAIL_COUNT;
    } else if (speed == FAN_MAX && (rear_speed < (FAN_REAR_SPEED_MAX * (100 - slop) / 100))) {
        fan_info->rear_failed++;
        if (fan_info->rear_failed == 1)
            syslog(LOG_WARNING, "%s-2 speed %d, less than %d%% of max speed(%d) detected",
                   fantray->name, rear_speed, 100 - slop, speed);
        if (fan_info->rear_failed == FAN_FAIL_COUNT)
            syslog(LOG_ERR, "%s-2 status is ABNORMAL, speed is set to 100%% but real speed is lower than 70%% of max speed",
                   fantray->name);
        if (fan_info->rear_failed > FAN_FAIL_COUNT)
            fan_info->rear_failed = FAN_FAIL_COUNT;
    } else {
        if (fan_info->rear_failed)
            syslog(LOG_WARNING, "%s-2 status is NORMAL", fantray->name);
        fan_info->rear_failed = 0;
    }

    if (fan_info->front_failed >= FAN_FAIL_COUNT && fan_info->rear_failed >= FAN_FAIL_COUNT) {
        if (fantray->failed == 0)
            syslog(LOG_WARNING, "%s failed, set fan max speed", fantray->name);
        fantray->failed = 1;
    }

    if (fan_info->front_failed >= FAN_FAIL_COUNT || fan_info->rear_failed >= FAN_FAIL_COUNT) {
        return 0;
    }

    return 1;

}

/*return: 1 OK, 0 not OK*/
int psu_speed_okay(const int fan, int speed, const int slop)
{
    int ret;
    char buf[PATH_CACHE_SIZE];
    int rc = 0;
    int psu_speed, pct;
    struct fantray_info_stu_sysfs *fantray;
    struct fan_info_stu_sysfs *fan_info;

    fantray = &fantray_info[fan];
    fan_info = &fantray->fan1;

    ret = fan_is_present_sysfs(fan, fan_info);
    if (ret == 0) {
        fantray->present = 0; //not present
        return 0;
    } else if (ret == 1) {
        fantray->present = 1;
        if (fantray->status == 0)
            return 0;
    } else {
        return 0;
    }

    snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, fan_info->front_fan_prefix);
    adjust_sysnode_path(fan_info->rear_fan_prefix, fan_info->front_fan_prefix, buf, sizeof(buf));
    rc = read_sysfs_int(buf, &ret);
    if (rc < 0) {
        syslog(LOG_ERR, "failed to read %s node", fan_info->front_fan_prefix);
        return -1;
    }
    psu_speed = ret;
    usleep(11000);
    if (psu_speed < FAN_FAIL_RPM) {
        fan_info->front_failed++;
        if (fan_info->front_failed == 1)
            syslog(LOG_WARNING, "%s speed %d, less than %d detected",
                   fantray->name, psu_speed, FAN_FAIL_RPM);
        if (fan_info->front_failed == FAN_FAIL_COUNT)
            syslog(LOG_ERR, "%s status is ABNORMAL, speed less than 1000 RPM for over 30 seconds",
                   fantray->name);
        if (fan_info->front_failed > FAN_FAIL_COUNT)
            fan_info->front_failed = FAN_FAIL_COUNT;
    } else if (speed == FAN_MAX && (psu_speed < (PSU_SPEED_MAX * (100 - slop) / 100))) {
        fan_info->front_failed++;
        if (fan_info->front_failed == 1)
            syslog(LOG_WARNING, "%s speed %d, less than %d%% of max speed(%d) detected",
                   fantray->name, psu_speed, 100 - slop, speed);
        if (fan_info->front_failed == FAN_FAIL_COUNT)
            syslog(LOG_ERR, "%s status is ABNORMAL, speed is set to 100%% but real speed is lower than 70%% of max speed",
                   fantray->name);
        if (fan_info->front_failed > FAN_FAIL_COUNT)
            fan_info->front_failed = FAN_FAIL_COUNT;
    } else {
        if (fan_info->front_failed)
            syslog(LOG_WARNING, "%s status is NORMAL", fantray->name);
        fan_info->front_failed = 0;
    }

    if (fan_info->front_failed >= FAN_FAIL_COUNT)
        return 0;

    return 1;

}

static int fancpld_watchdog_enable(void)
{
    int ret;
    char fullpath[PATH_CACHE_SIZE];

    snprintf(fullpath, PATH_CACHE_SIZE, "%s", FAN_WDT_ENABLE_SYSFS);
    ret = write_sysfs_int(fullpath, 1);
    if (ret < 0) {
        syslog(LOG_ERR, "failed to set fan %s, value 1",
               FAN_WDT_ENABLE_SYSFS);
        return -1;
    }
    usleep(11000);
    return 0;
}

static int system_shutdown(const char *why)
{
    int ret;

    syslog(LOG_EMERG, "Shutting down:  %s", why);

    ret = write_sysfs_int(PSU1_SHUTDOWN_SYSFS, 1);
    if (ret < 0) {
        syslog(LOG_ERR, "failed to set PSU1 shutdown");
        return -1;
    }
    ret = write_sysfs_int(PSU2_SHUTDOWN_SYSFS, 1);
    if (ret < 0) {
        syslog(LOG_ERR, "failed to set PSU2 shutdown");
        return -1;
    }

    sleep(2);
    exit(2);

    return 0;
}

char* find_sub_string(char *src, const char *sub, int src_len)
{
    for (int i = 0; i < src_len; i++) {
        if (*(src + i) == *sub) {
            int flag = 1;
            for (int index = 1; index < strlen(sub); index++) {
                if (*(src + index + i) != *(sub + index)) {
                    flag = 0;
                    break;
                }
            }
            if (flag) return (src + i);
        }
    }
    return NULL;
}

/*
 * Determine the system's thermal direction from the fan EEPROMs.
 * If the fans are not all in the same direction, thermal direction will
 * determine by the large majority. If the total number of fan directions
 * are even, assume F2B.
 *
 * This also updates fans status and info in global variable fantray_info[].
 *
 * @param direction System thermal direction.
 * @return fan direction one of FAN_DIR_F2B or FAN_DIR_B2F.
 */
static int get_fan_direction(int direction)
{
    struct fantray_info_stu_sysfs *fantray;
    char buffer[128];
    char command[128];
    int f2r_fan_cnt = 0;
    int r2f_fan_cnt = 0;
    FILE *fp;
    int i = 0;
    char *pn;
    int ret;
    char direction_str[8];

    get_direction_str(direction, direction_str);

    for (; i < TOTAL_FANS + TOTAL_PSUS; i++)
    {
        if (i >= sizeof(fan_eeprom_path) / sizeof(fan_eeprom_path[0]))
            continue;
        fantray = &fantray_info[i];
        if (!fantray->read_eeprom)
            continue;
        fp = fopen(fan_eeprom_path[i], "rb");
        if (!fp) {
            if (fantray->direction != FAN_DIR_FAULT) {
                syslog(LOG_ERR, "failed to get %s direction", fantray->name);
                syslog(LOG_WARNING, "%s direction changed to [Fault]", fantray->name);
                fantray->direction = FAN_DIR_FAULT;
                fantray->read_eeprom = 0;
                continue;
            }
        }
        char temp;
        int len;
        memset(buffer, 0, sizeof(buffer));
        ret = fread(buffer, sizeof(char), sizeof(buffer), fp);
        fclose(fp);
        if (i < TOTAL_FANS) {
            if (pn = find_sub_string(buffer, FAN_DIR_F2B_STR, sizeof(buffer))) {
                f2r_fan_cnt++;
                if (fantray->direction == FAN_DIR_FAULT) {
                    syslog(LOG_WARNING, "%s eeprom is NORMAL", fantray->name);
                    if (fantray->eeprom_fail) {
                        syslog(LOG_WARNING, "%s model match, part number is %s", fantray->name, FAN_DIR_F2B_STR);
                        fantray->eeprom_fail = 0;
                    }
                }
                fantray->direction = FAN_DIR_F2B;
                if (direction != fantray->direction)
                    syslog(LOG_ERR, "%s airflow direction mismatch, direction is F2B, system direction is %s", fantray->name, direction_str);
                else
                    syslog(LOG_WARNING, "%s airflow direction match, direction is F2B, system direction is %s", fantray->name, direction_str);
            } else if (find_sub_string(buffer, FAN_DIR_B2F_STR, sizeof(buffer))) {
                r2f_fan_cnt++;
                if (fantray->direction == FAN_DIR_FAULT) {
                    syslog(LOG_WARNING, "%s eeprom is NORMAL", fantray->name);
                    if (fantray->eeprom_fail) {
                        syslog(LOG_WARNING, "%s model match, part number is %s", fantray->name, FAN_DIR_B2F_STR);
                        fantray->eeprom_fail = 0;
                    }
                }
                fantray->direction = FAN_DIR_B2F;
                if (direction != fantray->direction)
                    syslog(LOG_ERR, "%s airflow direction mismatch, direction is B2F, system direction is %s", fantray->name, direction_str);
                else
                    syslog(LOG_WARNING, "%s airflow direction match, direction is B2F, system direction is %s", fantray->name, direction_str);
            } else {
                fantray->direction = FAN_DIR_FAULT;
                if (ret > 0) {
                    fantray->eeprom_fail = 1;
                    syslog(LOG_CRIT, "%s model mismatch, part number is %s", fantray->name, pn);
                } else {
                    syslog(LOG_WARNING, "%s eeprom is ABNORMAL, read %s eeprom failed", fantray->name, fantray->name);
                }
            }
        } else {
            if (pn = find_sub_string(buffer, DELTA_PSU_DIR_F2B_STR, sizeof(buffer))) {
                if (fantray->direction == FAN_DIR_FAULT) {
                    syslog(LOG_WARNING, "%s eeprom is NORMAL", fantray->name);
                    if (fantray->eeprom_fail) {
                        syslog(LOG_WARNING, "%s model match, part number is %s", fantray->name, DELTA_PSU_DIR_F2B_STR);
                        fantray->eeprom_fail = 0;
                    }
                }
                fantray->direction = FAN_DIR_F2B;
                if (direction != fantray->direction)
                    syslog(LOG_ERR, "%s airflow direction mismatch, direction is F2B, system direction is %s", fantray->name, direction_str);
                else
                    syslog(LOG_WARNING, "%s airflow direction match, direction is F2B, system direction is %s", fantray->name, direction_str);
            } else if (find_sub_string(buffer, DELTA_PSU_DIR_B2F_STR, sizeof(buffer))) {
                if (fantray->direction == FAN_DIR_FAULT) {
                    syslog(LOG_WARNING, "%s eeprom is NORMAL", fantray->name);
                    if (fantray->eeprom_fail) {
                        syslog(LOG_WARNING, "%s model match, part number is %s", fantray->name, DELTA_PSU_DIR_B2F_STR);
                        fantray->eeprom_fail = 0;
                    }
                }
                fantray->direction = FAN_DIR_B2F;
                if (direction != fantray->direction)
                    syslog(LOG_ERR, "%s airflow direction mismatch, direction is B2F, system direction is %s", fantray->name, direction_str);
                else
                    syslog(LOG_WARNING, "%s airflow direction match, direction is B2F, system direction is %s", fantray->name, direction_str);
            } else if (find_sub_string(buffer, ACBEL_PSU_DIR_F2B_STR, sizeof(buffer))) {
                if (fantray->direction == FAN_DIR_FAULT) {
                    syslog(LOG_WARNING, "%s eeprom is NORMAL", fantray->name);
                    if (fantray->eeprom_fail) {
                        syslog(LOG_WARNING, "%s model match, part number is %s", fantray->name, ACBEL_PSU_DIR_F2B_STR);
                        fantray->eeprom_fail = 0;
                    }
                }
                fantray->direction = FAN_DIR_F2B;
                if (direction != fantray->direction)
                    syslog(LOG_ERR, "%s airflow direction mismatch, direction is F2B, system direction is %s", fantray->name, direction_str);
                else
                    syslog(LOG_WARNING, "%s airflow direction match, direction is F2B, system direction is %s", fantray->name, direction_str);
            } else if (find_sub_string(buffer, ACBEL_PSU_DIR_B2F_STR, sizeof(buffer))) {
                if (fantray->direction == FAN_DIR_FAULT) {
                    syslog(LOG_WARNING, "%s eeprom is NORMAL", fantray->name);
                    if (fantray->eeprom_fail) {
                        syslog(LOG_WARNING, "%s model match, part number is %s", fantray->name, ACBEL_PSU_DIR_B2F_STR);
                        fantray->eeprom_fail = 0;
                    }
                }
                fantray->direction = FAN_DIR_B2F;
                if (direction != fantray->direction)
                    syslog(LOG_ERR, "%s airflow direction mismatch, direction is B2F, system direction is %s", fantray->name, direction_str);
                else
                    syslog(LOG_WARNING, "%s airflow direction match, direction is B2F, system direction is %s", fantray->name, direction_str);
            } else {
                fantray->direction = FAN_DIR_FAULT;
                if (ret > 0) {
                    fantray->eeprom_fail = 1;
                    syslog(LOG_CRIT, "%s model mismatch, part number is %s", fantray->name, pn);
                } else {
                    syslog(LOG_WARNING, "%s eeprom is ABNORMAL, read %s eeprom failed", fantray->name, fantray->name);
                }
            }
        }
        fantray->read_eeprom = 0;
    }

    if (f2r_fan_cnt >= r2f_fan_cnt) {
        return FAN_DIR_F2B;
    } else {
        return FAN_DIR_B2F;
    }
}

/*
 * Get system thermal direction from TLV EEPROM.
 * If falis to read thermal direction, then set the fan speed to maximum.
 *
 * @return the systen thermal direction FAN_DIR_F2B or FAN_DIR_B2F or less than
 * zero if cannot determine the system thermal direction.
 */
int get_thermal_direction(void)
{
    char buffer[128];
    FILE *fp;
    char command[128];
    memset(command, 0, sizeof(command));
    sprintf(command, "/usr/bin/decode-syseeprom | grep 'Part Number' 2> /dev/null");
    fp = popen(command, "r");
    int thermal_dir = FAN_DIR_INIT;
    int fan, fan_speed;

    /* We have 2 possible errors here
     * 1: The eeprom canot be read
     * 2: The PN does not match any expected cases
     * Both will comes to the conclusion that we cannot know the system
     * thermal direction here.
     *
     * We can do either let the fan be 100% and STOP PID control OR
     * determine the fan speed from the fans and let running.
     * But this might cause the program continue running with incorrect
     * thermal parameter.
     *
     * I perfer to go with 100% and exit. The service can be started again,
     * after the issue is fixed.
     */

    if (!fp) {
        syslog(LOG_ERR, "failed to read thermal direction from TLV EEPROM, FAN speed is set to 100%%");
        fan_speed = FAN_MAX;
        for (fan = 0; fan < TOTAL_FANS; fan++) {
            write_fan_speed(fan, fan_speed);
        }
        write_psu_fan_speed(fan, fan_speed);
    } else {
        char temp;
        int len = 0;
        memset(buffer, 0, sizeof(buffer));
        fread(buffer, sizeof(char), sizeof(buffer), fp);
        pclose(fp);
        if (find_sub_string(buffer, THERMAL_DIR_F2B_STR, sizeof(buffer))) {
            syslog(LOG_INFO, "system thermal direction is F2B");
            thermal_dir = FAN_DIR_F2B;
        } else if (find_sub_string(buffer, THERMAL_DIR_B2F_STR, sizeof(buffer))) {
            syslog(LOG_INFO, "system thermal direction is B2F");
            thermal_dir = FAN_DIR_B2F;
        } else {
            syslog(LOG_ERR, "unrecognized system P/N in TLV EEPROM\n");
            syslog(LOG_ERR, "system thermal direction is unknown, FAN speed is set to 100%%");
            fan_speed = FAN_MAX;
            for (fan = 0; fan < TOTAL_FANS; fan++) {
                write_fan_speed(fan, fan_speed);
            }
            write_psu_fan_speed(fan, fan_speed);
        }
    }

    get_fan_direction(thermal_dir);

    return thermal_dir;
}

static int update_thermal_direction()
{
    struct fantray_info_stu_sysfs *fantray;
    int dir = get_thermal_direction();

    /* Just pop out with error code, if error happen */
    if (dir < 0)
        return dir;

    if (direction != dir) {
        /*
         * This line only called once. Even the function name is update.
         * If the thermal direction is not set, the whole program will
         * runs into unexpected behaviors.
         */
        direction = dir;
        if (direction == FAN_DIR_F2B) {
            syslog(LOG_INFO, "setting F2B thermal policy");
            policy = &f2b_normal_policy;
        }
        if (direction == FAN_DIR_B2F) {
            syslog(LOG_INFO, "setting B2F thermal policy");
            policy = &b2f_normal_policy;
        }
    }
    return 0;
}

static int pid_ini_parser(struct board_info_stu_sysfs *info, FILE *fp)
{
    char *p;
    char buf[PID_FILE_LINE_MAX];
    struct sensor_info_sysfs *sensor;
    sensor = info->critical;

    while (fgets(buf, PID_FILE_LINE_MAX, fp) != NULL) {
        if (buf[0] == '#' || strlen(buf) <= 0 || buf[0] == '\r' || buf[0] == '\n')
            continue;

        p = strtok(buf, "=");
        while (p != NULL) {
            if (!strncmp(p, "PID_enable", strlen("PID_enable"))) {
                p = strtok(NULL, "=");
                if (p) {
                    pid_using = atoi(p);
                } else {
                    pid_using = 0;
                }
                if (!pid_using)
                    return 0;
            } else if (!strncmp(p, "setpoint", strlen("setpoint"))) {
                p = strtok(NULL, "=");
                if (p && pid_using)
                    sensor->setpoint = atof(p);
            } else if (!strncmp(p, "P", strlen("P"))) {
                p = strtok(NULL, "=");
                if (p && pid_using)
                    sensor->p = atof(p);
            } else if (!strncmp(p, "I", strlen("I"))) {
                p = strtok(NULL, "=");
                if (p && pid_using)
                    sensor->i = atof(p);
            } else if (!strncmp(p, "D", strlen("D"))) {
                p = strtok(NULL, "=");
                if (p && pid_using)
                    sensor->d = atof(p);
            } else if (!strncmp(p, "min_output", strlen("min_output"))) {
                p = strtok(NULL, "=");
                if (p && pid_using)
                    sensor->min_output = atof(p);
                return 0;
            } else if (!strncmp(p, "max_output", strlen("max_output"))) {
                p = strtok(NULL, "=");
                if (p && pid_using)
                    sensor->max_output = atof(p);
                return 0;
            }
            p = strtok(NULL, "=");
        }
    }

    return 0;
}

static int load_pid_config(void)
{
    int i;
    FILE *fp;
    char buf[PID_FILE_LINE_MAX];
    char *p;
    int len;
    struct board_info_stu_sysfs *binfo = &board_info[0];

    fp = fopen(PID_CONFIG_PATH, "r");
    if (!fp) {
        pid_using = 0;
        syslog(LOG_NOTICE, "PID configure file does not find, using default PID params");
        return 0;
    }
    while (fgets(buf, PID_FILE_LINE_MAX, fp) != NULL) {
        len = strlen(buf);
        buf[len - 1] = '\0';
        if (buf[0] == '#' || strlen(buf) <= 0 || buf[0] == '\r' || buf[0] == '\n')
            continue;
        p = strtok(buf, "[");
        while (p != NULL) {
            if (!strncmp(p, "PID enable", strlen("PID enable"))) {
                pid_ini_parser(binfo, fp);
            } else {
                for (i = 0; i < BOARD_INFO_SIZE; i++) {
                    binfo = &board_info[i];
                    if (!strncmp(binfo->name, p, strlen(binfo->name))) {
                        pid_ini_parser(binfo, fp);
                        break;
                    }
                }
            }
            p = strtok(NULL, "[");
        }
    }

    fclose(fp);
    return 0;
}

static int policy_init(void)
{
    int slope;
    int ret;
    syslog(LOG_NOTICE, "Initializing FSC policy");

    ret = update_thermal_direction();

    if (ret < 0){
        syslog(LOG_NOTICE, "Failed, Unknown thermal direction");
        syslog(LOG_NOTICE, "Trying to exit...");
        return ret;
    }

    load_pid_config();
    if (pid_using == 0) {
        syslog(LOG_NOTICE, "PID configure: using default PID params");
    }

    struct board_info_stu_sysfs *info;
    struct sensor_info_sysfs *critical;
    int i;
    for (i = 0; i < BOARD_INFO_SIZE; i++) {
        info = &board_info[i];
        if (info->slot_id != direction)
            continue;
        if (info->critical && (info->flag & PID_CTRL_BIT)) {
            critical = info->critical;
            syslog(LOG_INFO, "%s: setpoint=%f, p=%f, i=%f, d=%f", info->name, critical->setpoint,
                   critical->p, critical->i, critical->d);
        }
    }

    return 0;
}

static void get_direction_str(int direction, char *message)
{
    const char *airflow_strings[] = { "Unknown",
                                      "Fault",
                                      "B2F",
                                      "F2B"};

    strcpy( message, airflow_strings[ direction + 1] );
}

int main(int argc, char **argv) {
    int ret;
    int critical_temp;
    int old_temp = -1;
    struct fantray_info_stu_sysfs *info;
    int fan_speed = FAN_MEDIUM;
    int bad_reads = 0;
    int fan_failure = 0;
    int sub_failed = 0;
    int one_failed = 0; //recored one system fantray failed
    int old_speed = FAN_MEDIUM;
    int fan_bad[TOTAL_FANS + TOTAL_PSUS] = {0};
    int fan;
    unsigned int log_count = 0; // How many times have we logged our temps?
    int prev_fans_bad = 0;
    int shutdown_delay = 0;
    int psu_pwm;
    int line_pwm = 0;
    int pid_pwm = 0;
    int alarm = 0;
#ifdef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
    int psu_old_temp = 0;
    int psu_raising_pwm;
    int psu_falling_pwm;
    int psu_fan_speed = FAN_MEDIUM;
#endif
    struct fantray_info_stu_sysfs *fantray;
    struct fan_info_stu_sysfs *fan_info;

    // Initialize path cache
    init_path_cache();

    // Start writing to syslog as early as possible for diag purposes.
    openlog("fand_v2", LOG_CONS, LOG_DAEMON);
    daemon(1, 0);
    syslog(LOG_DEBUG, "Starting up;  system should have %d fans.", TOTAL_FANS);
    fancpld_watchdog_enable();
    ret = policy_init();
    if (ret < 0){
        syslog(LOG_NOTICE, "exit");
        return ret;
    }
    sleep(5);  /* Give the fans time to come up to speed */
    while (1) {
        fan_speed_temp = 0;
        /* Read sensors */
        critical_temp = read_critical_max_temp();
        read_pid_max_temp();
        alarm_temp_update(&alarm);

        /*
         * Calculate change needed -- we should eventually
         * do something more sophisticated, like PID.
         *
         * We should use the intake temperature to adjust this
         * as well.
         */

        /* Other systems use a simpler built-in table to determine fan speed. */
        policy->old_pwm = fan_speed;
        line_pwm = calculate_line_pwm();
        if (line_pwm > fan_speed_temp)
            fan_speed_temp = line_pwm;
#ifdef DEBUG
        syslog(LOG_DEBUG, "[zmzhan]%s: line_speed=%d", __func__, fan_speed_temp);
#endif

#ifndef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
        psu_pwm = get_psu_pwm();
#endif
        if (1) {
            pid_pwm = calculate_pid_pwm(fan_speed);
            if (pid_pwm > fan_speed_temp)
                fan_speed_temp = pid_pwm;
        }
        fan_speed = fan_speed_temp;
#ifdef DEBUG
        syslog(LOG_DEBUG, "[zmzhan]%s: fan_speed=%d, pid_using=%d, pid_pwm=%d",
               __func__, fan_speed, pid_using, pid_pwm);
#endif
        policy->pwm = fan_speed;
        old_temp = critical_temp;
#ifdef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
        psu_raising_pwm = calculate_psu_raising_fan_pwm(critical_temp);
        psu_falling_pwm = calculate_psu_falling_fan_pwm(critical_temp);
        if (psu_old_temp <= critical_temp) {
            /*raising*/
            if (psu_raising_pwm >= psu_fan_speed) {
                psu_fan_speed = psu_raising_pwm;
            }
        } else {
            /*falling*/
            if (psu_falling_pwm <= psu_fan_speed ) {
                psu_fan_speed = psu_falling_pwm;
            }
        }
        psu_old_temp = critical_temp;
#endif

        /*
         * Update fans only if there are no failed ones. If any fans failed
         * earlier, all remaining fans should continue to run at max speed.
         */
        if (fan_failure == 0) {
            if (log_count++ % REPORT_TEMP == 0) {
                syslog(LOG_NOTICE, "critical temp %d, fan speed %d%%",
                       critical_temp, fan_speed * 100 / FAN_MAX);
                syslog(LOG_NOTICE, "Fan speed changing from %d%% to %d%%",
                       old_speed * 100 / FAN_MAX, fan_speed * 100 / FAN_MAX);
            }
            for (fan = 0; fan < TOTAL_FANS; fan++) {
                write_fan_speed(fan, fan_speed);
            }
#ifdef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
            write_psu_fan_speed(fan, psu_fan_speed);
#else
            write_psu_fan_speed(fan, fan_speed);
#endif
        }

        /*
         * Wait for some change.  Typical I2C temperature sensors
         * only provide a new value every second and a half, so
         * checking again more quickly than that is a waste.
         *
         * We also have to wait for the fan changes to take effect
         * before measuring them.
         */

        sleep(3);

        /* Check fan RPMs */
        for (fan = 0; fan < TOTAL_FANS; fan++) {
            /*
            * Make sure that we're within some percentage
            * of the requested speed.
            */
            if (fan_speed_okay(fan, fan_speed, FAN_FAILURE_OFFSET)) {
                if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
                    write_fan_led(fan, FAN_LED_GREEN);
                    syslog(LOG_CRIT, "Fan %d has recovered", fan + 1);
                }
                fan_bad[fan] = 0;
            } else {
                fan_bad[fan]++;
            }
        }
        for (fan = TOTAL_FANS; fan < TOTAL_FANS + TOTAL_PSUS; fan++) {
            if (psu_speed_okay(fan, fan_speed, FAN_FAILURE_OFFSET)) {
                if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
                    syslog(LOG_CRIT, "PSU %d has recovered", fan - TOTAL_FANS + 1);
                }
                fan_bad[fan] = 0;
            } else {
                fan_bad[fan]++;
            }
        }

        fan_failure = 0;
        sub_failed = 0;
        one_failed = 0;
        for (fan = 0; fan < TOTAL_FANS + TOTAL_PSUS; fan++) {
            if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
                fantray = &fantray_info[fan];
                fan_info = &fantray->fan1;
                if (fan_info->front_failed >= FAN_FAIL_COUNT) {
                    sub_failed++;
                    one_failed++;
#ifdef DEBUG
                    syslog(LOG_DEBUG, "[zmzhan]%s:fan[%d] front_failed=%d", __func__, fan, fan_info->front_failed);
#endif
                }
                if (fan_info->rear_failed >= FAN_FAIL_COUNT) {
                    sub_failed++;
                    one_failed++;
#ifdef DEBUG
                    syslog(LOG_DEBUG, "[zmzhan]%s:fan[%d] rear_failed=%d", __func__, fan, fan_info->rear_failed);
#endif
                }
                if (fantray->present == 0) {
                    fan_failure++;
                }
                else if ((fantray->failed > 0) || ((fantray->direction != direction) && (fan < TOTAL_FANS))) {
                    fan_failure++;
                }

                write_fan_led(fan, FAN_LED_RED);
            }
        }
#ifdef DEBUG
        syslog(LOG_DEBUG, "[zmzhan]%s: fan_failure=%d, sub_failed=%d", __func__, fan_failure, sub_failed);
#endif
        if (sub_failed >= 2) {
            fan_failure += sub_failed / 2;
        } else if (sub_failed == 1 && one_failed == 1) {
            if (direction == FAN_DIR_B2F) {
                policy = &b2f_one_fail_policy;
            } else {
                policy = &f2b_one_fail_policy;
            }
#ifdef DEBUG
            syslog(LOG_DEBUG, "[zmzhan]%s: Change the policy: policy=%p(fail: b2f:%p, f2b:%p)", __func__, policy, &b2f_one_fail_policy, &f2b_one_fail_policy);
#endif
        } else {
            if (one_failed == 0 && (policy == &b2f_one_fail_policy || policy == &f2b_one_fail_policy)) {
                if (direction == FAN_DIR_B2F) {
                    policy = &b2f_normal_policy;
                } else {
                    policy = &f2b_normal_policy;
                }
#ifdef DEBUG
                syslog(LOG_DEBUG, "[zmzhan]%s: Recovery policy: policy=%p(b2f:%p, f2b:%p)", __func__, policy, &b2f_normal_policy, &f2b_normal_policy);
#endif
            }
        }
        if (fan_failure > 0) {
            if (prev_fans_bad != fan_failure) {
                syslog(LOG_CRIT, "%d fans failed", fan_failure);
            }
            fan_speed = FAN_MAX;
            for (fan = 0; fan < TOTAL_FANS; fan++) {
                write_fan_speed(fan, fan_speed);
            }
            write_psu_fan_speed(fan, fan_speed);
            old_speed = fan_speed;
        } else if (prev_fans_bad != 0 && fan_failure == 0) {
            old_speed = fan_speed;
        } else {
            old_speed = fan_speed;
        }
        /* Suppress multiple warnings for similar number of fan failures. */
        prev_fans_bad = fan_failure;
        if (sys_fan_led_color)
            write_sys_fan_led(SYS_FAN_LED_RED);
        else
            write_sys_fan_led(SYS_FAN_LED_GREEN);
        sys_fan_led_color = 0;
        if (psu_led_color)
            write_psu_fan_led(TOTAL_FANS, PSU_LED_RED);
        else
            write_psu_fan_led(TOTAL_FANS, PSU_LED_GREEN);
        psu_led_color = 0;
        usleep(11000);
        get_fan_direction(direction);
    }

    return 0;
}

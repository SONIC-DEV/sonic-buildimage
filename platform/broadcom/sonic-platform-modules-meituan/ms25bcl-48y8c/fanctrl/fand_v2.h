#ifndef _FAND_V2_H_
#define _FAND_V2_H_

#define uchar unsigned char
#define PATH_CACHE_SIZE 256

struct sensor_info_sysfs
{
    const char *prefix;
    const char *suffix;
    uchar error_cnt;
    int temp;
    int t1;
    int t2;
    int old_pwm;
    float setpoint;
    float p;
    float i;
    float d;
    float min_output;
    float max_output;
    int (*read_sysfs)(struct sensor_info_sysfs *sensor);
    char path_cache[PATH_CACHE_SIZE];
};

struct fan_info_stu_sysfs
{
    const char *prefix;
    const char *front_fan_prefix;
    const char *rear_fan_prefix;
    const char *pwm_prefix;
    const char *fan_led_prefix;
    const char *fan_present_prefix;
    const char *fan_status_prefix;
    //uchar present; //for chassis using, other ignore it
    uchar front_failed;  //for single fan fail
    uchar rear_failed;
};

struct psu_info_sysfs
{
    char* sysfs_path;
    char* shutdown_path;
    int value_to_shutdown;
};

struct board_info_stu_sysfs
{
    const char *name;
    uint slot_id;
    int correction;
    int lwarn;
    int hwarn;
    int warn_count;
    int recovery_count;
    int flag;
    struct sensor_info_sysfs *critical;
    struct sensor_info_sysfs *alarm;
};

struct fantray_info_stu_sysfs
{
    const char *name;
    int present;
    int read_eeprom;
    int status;
    int failed; //for fantray fail
    int direction;
    int eeprom_fail;
    struct fan_info_stu_sysfs fan1;
};

struct rpm_to_pct_map
{
    uint pct;
    uint rpm;
};
struct dictionary_t
{
    char name[20];
    int value;
};

struct thermal_policy
{
    int pwm;
    int old_pwm;
    struct line_policy *line;
    // int (*calculate_pwm)(int cur_temp, int last_temp);
};

struct point
{
    int temp;
    int speed;
};

struct line_policy
{
    int temp_hyst;
    struct point begin;
    struct point end;
    int (*get_speed)(struct sensor_info_sysfs *sensor, struct line_policy *line);
};

struct pid_policy
{
    int cur_temp;
    int t1;
    int t2;
    int set_point;
    float kp;
    float ki;
    float kd;
    int last_output;
    int max_output;
    int min_output;
    int (*get_speed)(struct pid_policy pid);
};

static int calculate_line_speed(struct sensor_info_sysfs *sensor, struct line_policy *line);
int calculate_pid_speed(struct pid_policy *pid);

#endif

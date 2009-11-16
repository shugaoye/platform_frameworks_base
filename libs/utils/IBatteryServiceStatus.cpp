#define LOG_TAG "BatteryStatus"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <utils/Log.h>
#include <cutils/atomic.h>

#include <utils/IBatteryServiceStatus.h>

namespace android {

    int32_t IBatteryServiceStatus::path_checked = 0;
    /*
     * These names are same as the ones defined inside the
     * Linux kernel in power_supply_sysfs.c
     */
    struct feature_prop IBatteryServiceStatus::power_ac_prop[] =  {
        { POWER_AC_NAME,   0,"\0",NULL           },
        { POWER_AC_ONLINE, 0,"\0","online" },
        { -1, 0 , "\0", NULL}
    };

    struct feature_prop IBatteryServiceStatus::power_bat_prop[] = {
        { POWER_BAT_NAME,  0,"\0",NULL          },
        { POWER_BAT_PRESENT,0,"\0","present"},
        { POWER_BAT_STATUS,0,"\0","status"},
        { POWER_BAT_HEALTH,0,"\0","health"},
        { POWER_BAT_ENERGY_NOW,0,"\0","energy_now"},
        { POWER_BAT_ENERGY_FULL,0,"\0","energy_full"},
        { POWER_BAT_CAPACITY_NOW,0,"\0","charge_now"},
        { POWER_BAT_CAPACITY_FULL,0,"\0","charge_full"},
        { POWER_BAT_VOLTAGE_NOW,0,"\0","voltage_now"},
        { POWER_BAT_VOLTAGE_FULL,0,"\0","votage_full"},
        { POWER_BAT_TEMPERATURE,0,"\0","temp" },
        { POWER_BAT_TECH,0,"\0","technology"},
        { -1,0,"\0",NULL}
    };

    struct feature_prop IBatteryServiceStatus::power_usb_prop[] = {
        { POWER_USB_NAME,  0,"\0",NULL          },
        { POWER_USB_ONLINE,0,"\0","online"},
        { -1,0,"\0",NULL}
    };

    void IBatteryServiceStatus::fill_path_info(DIR *d,const char *base,
                                               feature_prop *prop) {
        struct dirent *de;
        DIR *dir;
        char path[SYSFS_PATH_MAX];

        while ((de = readdir(d))) {
            if ( de->d_name[0] != '.') {
                snprintf(path,SYSFS_PATH_MAX,"%s/%s/power_supply",
                         base,de->d_name);
                if (!access(path, F_OK)) {
                    if ((dir = opendir(path)) != NULL) {
                        while( (de = readdir(dir))) {
                            if (de->d_name[0] != '.') {
                                prop->present = 1;
                                prop++;
                                while(prop->id != -1) {
                                    snprintf(prop->prop, SYSFS_PATH_MAX,
                                             "%s/%s/%s",
                                             path,de->d_name,prop->name);
                                    if (!access(prop->prop,F_OK)) {
                                        prop->present = 1;
                                    }
                                    prop++;
                                }
                                closedir(dir);
                                return;
                            }
                        }
                        closedir(dir);
                    } else {
                        LOGE("Can not read directory %s\n",path);
                    }
                }
            }
        }
    }

    IBatteryServiceStatus::IBatteryServiceStatus() {
        char dir[BATTERY_MAX_PATH_LEN];
        int i,type;
        DIR *d;

        if (android_atomic_cmpxchg(0,1,&IBatteryServiceStatus::path_checked)) {
            LOGI("The BSS has been inited\n");
            return;
        }
        /*
         * process AC first
         */
        if ((d = opendir(SYS_FS_ACPI_POWER_AC_BASE))!=NULL) {
            fill_path_info(d,SYS_FS_ACPI_POWER_AC_BASE,power_ac_prop);
            closedir(d);
        } else {
            LOGE(" Can not open directory :%s\n",SYS_FS_ACPI_POWER_AC_BASE);
        }

        if ((d = opendir(SYS_FS_ACPI_POWER_BAT_BASE)) != NULL) {
            fill_path_info(d,SYS_FS_ACPI_POWER_BAT_BASE,power_bat_prop);
            closedir(d);
        } else {
            LOGE(" Can not open directory :%s\n",SYS_FS_ACPI_POWER_BAT_BASE);
        } 

        if ((d = opendir(SYS_FS_ACPI_POWER_USB_BASE)) != NULL) {
            fill_path_info(d,SYS_FS_ACPI_POWER_USB_BASE,power_usb_prop);
            closedir(d);
        } else {
            LOGE(" Can not open directory :%s\n",SYS_FS_ACPI_POWER_USB_BASE);
        } 
        return;
    }

    IBatteryServiceStatus::~IBatteryServiceStatus() {}

    int IBatteryServiceStatus::readFromFile(const char *path, char *buf, size_t size) {
        int fd = open(path, O_RDONLY, 0);
        if (fd == -1) {
            LOGE("Could not open '%s'", path);
            return -1;
        }
        size_t count = read(fd, buf, size);
        if (count > 0) {
            count = (count < size) ? count : size - 1;
            while (count > 0 && buf[count-1] == '\n') count--;
            buf[count] = '\0';
        } else {
            buf[0] = '\0';
        }
        close(fd);
        return count;
    }

    int IBatteryServiceStatus::get_bat_level(void) {
        float now = 0, full = 0;

        if (power_bat_prop->present) {
            if (power_bat_prop[POWER_BAT_ENERGY_NOW].present) {
                now  = get_bat_energy_now();
                full = get_bat_energy_full();
            } else {
                now  = get_bat_capacity_now();
                full = get_bat_capacity_full();
            }
        } else {
            /*
             * We don't have a battery at all, so
             * fake the the battery level
             */
            return 100;
        }

        if (full > 0) {
            return (int)((now / full) * 100);
        }
        return (int)now;
    }

    int IBatteryServiceStatus::get_bat_voltage(void) {
        return get_bat_voltage_now();
    }

    int get_path_func(ac,online,POWER_AC_ONLINE)
    int get_path_func(bat,present,POWER_BAT_PRESENT)
    int get_path_func(usb,online,POWER_USB_ONLINE)
    int get_path_func(bat,status,POWER_BAT_STATUS)
    int get_path_func(bat,health,POWER_BAT_HEALTH)
    int get_path_func(bat,energy_now,POWER_BAT_ENERGY_NOW)
    int get_path_func(bat,energy_full,POWER_BAT_ENERGY_FULL)
    int get_path_func(bat,capacity_now,POWER_BAT_CAPACITY_NOW)
    int get_path_func(bat,capacity_full,POWER_BAT_CAPACITY_FULL)
    int get_path_func(bat,voltage_now,POWER_BAT_VOLTAGE_NOW)
    int get_path_func(bat,voltage_full,POWER_BAT_VOLTAGE_FULL)
    int get_path_func(bat,temperature,POWER_BAT_TEMPERATURE)
    int get_path_func(bat,tech,POWER_BAT_TECH)
    int get_bool_func(ac,online,POWER_AC_ONLINE)
    int get_bool_func(usb,online,POWER_USB_ONLINE)
    int get_bool_func(bat,present,POWER_BAT_PRESENT)
    int get_int_func(bat,capacity_now,POWER_BAT_CAPACITY_NOW)
    int get_int_func(bat,capacity_full,POWER_BAT_CAPACITY_FULL)
    int get_int_func(bat,energy_now,POWER_BAT_ENERGY_NOW)
    int get_int_func(bat,energy_full,POWER_BAT_ENERGY_FULL)
    int get_int_func(bat,voltage_now,POWER_BAT_VOLTAGE_NOW)
    int get_int_func(bat,voltage_full,POWER_BAT_VOLTAGE_FULL)
    int get_int_func(bat,temperature,POWER_BAT_TEMPERATURE)
    int get_string_func(bat,status,POWER_BAT_STATUS)
    int get_string_func(bat,health,POWER_BAT_HEALTH)
    int get_string_func(bat,tech,POWER_BAT_TECH)
};

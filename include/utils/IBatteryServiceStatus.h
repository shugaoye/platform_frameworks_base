#ifndef _IBATTERY_SERVICE_STATUS_H
#define _IBATTERY_SERVICE_STATUS_H

#include <cutils/properties.h>
#include <dirent.h>

#define SYS_FS_ACPI_POWER_AC_BASE    "/sys/bus/acpi/drivers/ac"
#define SYS_FS_ACPI_POWER_BAT_BASE   "/sys/bus/acpi/drivers/battery"
#define SYS_FS_ACPI_POWER_USB_BASE   "/sys/bus/acpi/driver/usb"
#define SYS_FS_POWER_SUPPLY_BASE     "/sys/class/power_supply"
#define SYSFS_PATH_MAX               256


#define get_path_func(dev,type,id)                                      \
    IBatteryServiceStatus::get_##dev##_##type##_path(char **path) {     \
        if (power_##dev##_prop->present) {                              \
            *path = power_##dev##_prop[id].prop ;                       \
            return power_##dev##_prop[id].present;                      \
        }                                                               \
        return 0;                                                       \
    }

#define POWER_BUF_SZ_SMALL 16
#define POWER_BUF_SZ_MID 128

#define get_bool_func(dev,type,id)                                      \
    IBatteryServiceStatus::is_##dev##_##type(void){                     \
        int _value = 0;                                                 \
        char _buf[POWER_BUF_SZ_SMALL];                                  \
        if (power_##dev##_prop[id].present > 0 &&                       \
            (readFromFile(power_##dev##_prop[id].prop,_buf,POWER_BUF_SZ_SMALL) > 0)) { \
            if (_buf[0] == '1') {                                       \
                _value = 1;                                             \
            }                                                           \
        }                                                               \
        return _value;                                                  \
    }

#define get_int_func(dev,type,id)                                       \
    IBatteryServiceStatus::get_##dev##_##type(void){                    \
        int _value = -1;                                                \
        char _buf[POWER_BUF_SZ_MID];                                    \
        if (power_##dev##_prop[id].present > 0 &&                       \
            readFromFile(power_##dev##_prop[id].prop,_buf,POWER_BUF_SZ_MID) > 0 ) { \
            _value = atoi(_buf);                                        \
        }                                                               \
        return _value;                                                  \
    }

#define get_string_func(dev,type,id)                                    \
    IBatteryServiceStatus::get_##dev##_##type(char *buf,size_t sz){     \
        int _value = -1;                                                \
        if (power_##dev##_prop[id].present > 0 ) {                      \
            _value = readFromFile(power_##dev##_prop[id].prop,buf,sz); \
        }                                                               \
        return _value;                                                  \
    }

namespace android {
    enum {
        POWER_AC_NAME = 0,
        POWER_AC_ONLINE
    };

    enum {
        POWER_USB_NAME = 0,
        POWER_USB_ONLINE
    };

    enum {
        POWER_BAT_NAME = 0,
        POWER_BAT_PRESENT,
        POWER_BAT_STATUS,
        POWER_BAT_HEALTH,
        POWER_BAT_ENERGY_NOW,
        POWER_BAT_ENERGY_FULL,
        POWER_BAT_CAPACITY_NOW,
        POWER_BAT_CAPACITY_FULL,
        POWER_BAT_VOLTAGE_NOW,
        POWER_BAT_VOLTAGE_FULL,
        POWER_BAT_TEMPERATURE,
        POWER_BAT_TECH,
    };

#define BATTERY_MAX_PATH_LEN 512
#define POWER_AC_LAST  POWER_AC_ONLINE
#define POWER_USB_LAST POWER_USB_ONLINE
#define POWER_BAT_LAST POWER_BAT_TECH

    struct feature_prop {
        int id;
        int present;
        char prop[PROPERTY_KEY_MAX];
        const char *name;
    };

    class IBatteryServiceStatus {
        static struct feature_prop power_ac_prop[];
        static struct feature_prop power_bat_prop[];
        static struct feature_prop power_usb_prop[];
        static int32_t path_checked;

    public:
        IBatteryServiceStatus();
        virtual ~IBatteryServiceStatus();

        int get_ac_online_path(char **path);
        int get_usb_online_path(char **path);
        int get_bat_status_path(char **path);
        int get_bat_health_path(char **path);
        int get_bat_present_path(char **path);
        int get_bat_capacity_now_path(char **path);
        int get_bat_capacity_full_path(char **path);
        int get_bat_energy_now_path(char **path);
        int get_bat_energy_full_path(char **path);
        int get_bat_voltage_now_path(char **path);
        int get_bat_voltage_full_path(char **path);
        int get_bat_temperature_path(char **path);
        int get_bat_tech_path(char **path);

        int get_bat_status_id(const char *status);
        int get_bat_health_id(const char *status);
        int get_bat_energy_now();
        int get_bat_energy_full();
        int get_bat_capacity_now();
        int get_bat_capacity_full();
        int get_bat_voltage_now();
        int get_bat_voltage_full();

        virtual int readFromFile(const char* path, char* buf, size_t size);
        virtual int is_ac_online();
        virtual int is_usb_online();
        virtual int is_bat_present();
        virtual int get_bat_temperature();
        virtual int get_bat_status(char *buf, size_t size);
        virtual int get_bat_health(char *buf, size_t size);
        virtual int get_bat_tech(char *buf, size_t size);
        virtual int get_bat_level();
        virtual int get_bat_voltage();
    private:
        void fill_path_info(DIR *d,const char *base,feature_prop *prop);

    };

};
#endif

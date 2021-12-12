/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo.Ham <myungjoo.ham@samsung.com>
 *
 * Charger Manager.
 * This framework enables to control and multiple chargers and to
 * monitor charging even in the context of suspend-to-RAM with
 * an interface combining the chargers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
**/

#ifndef _CHARGER_MANAGER_H
#define _CHARGER_MANAGER_H

#include <linux/power_supply.h>
#include <linux/extcon.h>
#include <linux/alarmtimer.h>

enum data_source {
	CM_BATTERY_PRESENT,
	CM_NO_BATTERY,
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	CM_POLL_EXTERNAL_POWER_ONLY,
	CM_POLL_CHARGING_ONLY,
};

enum cm_event_types {
	CM_EVENT_UNKNOWN = 0,
	CM_EVENT_BATT_FULL,
	CM_EVENT_BATT_IN,
	CM_EVENT_BATT_OUT,
	CM_EVENT_BATT_OVERHEAT,
	CM_EVENT_BATT_COLD,
	CM_EVENT_EXT_PWR_IN_OUT,
	CM_EVENT_CHG_START_STOP,
	CM_EVENT_OTHERS,
	CM_EVENT_FAST_CHARGE,
};

enum cm_jeita_types {
	CM_JEITA_DCP = 0,
	CM_JEITA_SDP,
	CM_JEITA_CDP,
	CM_JEITA_UNKNOWN,
	CM_JEITA_FCHG,
	CM_JEITA_MAX,
};

enum cm_charge_status {
	CM_CHARGE_TEMP_OVERHEAT = BIT(0),
	CM_CHARGE_TEMP_COLD = BIT(1),
	CM_CHARGE_VOLTAGE_ABNORMAL = BIT(2),
	CM_CHARGE_HEALTH_ABNORMAL = BIT(3),
	CM_CHARGE_DURATION_ABNORMAL = BIT(4),
};

enum cm_fast_charge_command {
	CM_FAST_CHARGE_NORMAL_CMD = 1,
	CM_FAST_CHARGE_ENABLE_CMD,
	CM_FAST_CHARGE_DISABLE_CMD,
};

struct wireless_data {
	struct power_supply_desc psd;
	struct power_supply *psy;
	int WIRELESS_ONLINE;
};

struct ac_data {
	struct power_supply_desc psd;
	struct power_supply *psy;
	int AC_ONLINE;
};

struct usb_data {
	struct power_supply_desc psd;
	struct power_supply *psy;
	int USB_ONLINE;
};

/**
 * struct charger_cable
 * @extcon_name: the name of extcon device.
 * @name: the name of charger cable(external connector).
 * @extcon_dev: the extcon device.
 * @wq: the workqueue to control charger according to the state of
 *	charger cable. If charger cable is attached, enable charger.
 *	But if charger cable is detached, disable charger.
 * @nb: the notifier block to receive changed state from EXTCON
 *	(External Connector) when charger cable is attached/detached.
 * @attached: the state of charger cable.
 *	true: the charger cable is attached
 *	false: the charger cable is detached
 * @charger: the instance of struct charger_regulator.
 * @cm: the Charger Manager representing the battery.
 */
struct charger_cable {
	const char *extcon_name;
	const char *name;

	/* The charger-manager use Extcon framework */
	struct extcon_dev *extcon_dev;
	struct notifier_block nb;

	/* The state of charger cable */
	bool attached;

	struct charger_regulator *charger;

	/*
	 * Set min/max current of regulator to protect over-current issue
	 * according to a kind of charger cable when cable is attached.
	 */
	int min_uA;
	int max_uA;

	struct charger_manager *cm;
};

/**
 * struct charger_regulator
 * @regulator_name: the name of regulator for using charger.
 * @consumer: the regulator consumer for the charger.
 * @externally_control:
 *	Set if the charger-manager cannot control charger,
 *	the charger will be maintained with disabled state.
 * @cables:
 *	the array of charger cables to enable/disable charger
 *	and set current limit according to constraint data of
 *	struct charger_cable if only charger cable included
 *	in the array of charger cables is attached/detached.
 * @num_cables: the number of charger cables.
 * @attr_g: Attribute group for the charger(regulator)
 * @attr_name: "name" sysfs entry
 * @attr_state: "state" sysfs entry
 * @attr_externally_control: "externally_control" sysfs entry
 * @attr_jeita_control: "jeita_control" sysfs entry
 * @attrs: Arrays pointing to attr_name/state/externally_control for attr_g
 */
struct charger_regulator {
	/* The name of regulator for charging */
	const char *regulator_name;
	struct regulator *consumer;

	/* charger never on when system is on */
	int externally_control;

	/*
	 * Store constraint information related to current limit,
	 * each cable have different condition for charging.
	 */
	struct charger_cable *cables;
	int num_cables;

	struct attribute_group attr_g;
	struct device_attribute attr_name;
	struct device_attribute attr_state;
	struct device_attribute attr_stop_charge;
	struct device_attribute attr_externally_control;
	struct device_attribute attr_jeita_control;
	struct attribute *attrs[6];

	struct charger_manager *cm;
};

struct charger_jeita_table {
	int temp;
	int recovery_temp;
	int current_ua;
	int term_volt;
};

enum cm_track_state {
	CAP_TRACK_INIT,
	CAP_TRACK_IDLE,
	CAP_TRACK_UPDATING,
	CAP_TRACK_DONE,
	CAP_TRACK_ERR,
};

struct cm_track_capacity {
	enum cm_track_state state;
	int start_clbcnt;
	int start_cap;
	int end_vol;
	int end_cur;
	s64 start_time;
	bool cap_tracking;
	struct delayed_work track_capacity_work;
};

/**
 * struct charger_desc
 * @psy_name: the name of power-supply-class for charger manager
 * @polling_mode:
 *	Determine which polling mode will be used
 * @fullbatt_vchkdrop_ms:
 * @fullbatt_vchkdrop_uV:
 *	Check voltage drop after the battery is fully charged.
 *	If it has dropped more than fullbatt_vchkdrop_uV after
 *	fullbatt_vchkdrop_ms, CM will restart charging.
 * @fullbatt_uV: voltage in microvolt
 *	If VBATT >= fullbatt_uV, it is assumed to be full.
 * @fullbatt_uA: battery current in microamp
 * @first_fullbatt_uA: battery current in microamp of first_full charged
 * @fullbatt_soc: state of Charge in %
 *	If state of Charge >= fullbatt_soc, it is assumed to be full.
 * @fullbatt_full_capacity: full capacity measure
 *	If full capacity of battery >= fullbatt_full_capacity,
 *	it is assumed to be full.
 * @polling_interval_ms: interval in millisecond at which
 *	charger manager will monitor battery health
 * @battery_present:
 *	Specify where information for existence of battery can be obtained
 * @psy_charger_stat: the names of power-supply for chargers
 * @num_charger_regulator: the number of entries in charger_regulators
 * @charger_regulators: array of charger regulators
 * @psy_fuel_gauge: the name of power-supply for fuel gauge
 * @thermal_zone : the name of thermal zone for battery
 * @temp_min : Minimum battery temperature for charging.
 * @temp_max : Maximum battery temperature for charging.
 * @temp_diff : Temperature difference to restart charging.
 * @cap : Battery capacity report to user space.
 * @measure_battery_temp:
 *	true: measure battery temperature
 *	false: measure ambient temperature
 * @charging_max_duration_ms: Maximum possible duration for charging
 *	If whole charging duration exceed 'charging_max_duration_ms',
 *	cm stop charging.
 * @discharging_max_duration_ms:
 *	Maximum possible duration for discharging with charger cable
 *	after full-batt. If discharging duration exceed 'discharging
 *	max_duration_ms', cm start charging.
 * @normal_charge_voltage_max:
 *	maximum normal charge voltage in microVolts
 * @normal_charge_voltage_drop:
 *	drop voltage in microVolts to allow restart normal charging
 * @fast_charge_voltage_max:
 *	maximum fast charge voltage in microVolts
 * @fast_charge_voltage_drop:
 *	drop voltage in microVolts to allow restart fast charging
 * @charger_status: Recording state of charge
 * @charger_type: Recording type of charge
 * @first_trigger_cnt: The number of times the battery is first_fully charged
 * @trigger_cnt: The number of times the battery is fully charged
 * @low_temp_trigger_cnt: The number of times the battery temperature
 *	is less than 10 degree.
 * @cap_one_time: The percentage of electricity is not
 *	allowed to change by 1% in cm->desc->cap_one_time
 * @trickle_time_out: If 99% lasts longer than it , will force set full statu
 * @trickle_time: Record the charging time when battery
 *	capacity is larger than 99%.
 * @trickle_start_time: Record current time when battery capacity is 99%
 * @update_capacity_time: Record the battery capacity update time
 * @last_query_time: Record last time enter cm_batt_works
 * @force_set_full: The flag is indicate whether
 *	there is a mandatory setting of full status
 * @shutdown_voltage: If it has dropped more than shutdown_voltage,
 *	the phone will automatically shut down
 * @wdt_interval: Watch dog time pre-load value
 * @jeita_tab: Specify the jeita temperature table, which is used to
 *	adjust the charging current according to the battery temperature.
 * @jeita_tab_size: Specify the size of jeita temperature table.
 * @jeita_disabled: disable jeita function when needs
 * @jeita_tab_array: Specify the jeita temperature table array, which is used to
 *	save the point of adjust the charging current according to the battery temperature.
 * @temperature: the battery temperature
 * @internal_resist: the battery internal resistance in mOhm
 * @cap_table_len: the length of ocv-capacity table
 * @cap_table: capacity table with corresponding ocv
 * @is_fast_charge: if it is support fast charge or not
 * @enable_fast_charge: if is it start fast charge or not
 * @fast_charge_enable_count: to count the number that satisfy start
 *	fast charge condition.
 * @fast_charge_disable_count: to count the number that satisfy stop
 *	fast charge condition.
 * @double_IC_total_limit_current: if it use two charge IC to support
 *	fast charge, we use total limit current to campare with thermal_val,
 *	to limit the thermal_val under total limit current.
 */
struct charger_desc {
	const char *psy_name;

	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;

	unsigned int fullbatt_vchkdrop_ms;
	unsigned int fullbatt_vchkdrop_uV;
	unsigned int fullbatt_uV;
	unsigned int fullbatt_uA;
	unsigned int first_fullbatt_uA;
	unsigned int fullbatt_soc;
	unsigned int fullbatt_full_capacity;

	enum data_source battery_present;

	const char **psy_charger_stat;
	const char **psy_fast_charger_stat;

	int num_charger_regulators;
	struct charger_regulator *charger_regulators;

	const char *psy_fuel_gauge;

	const char *thermal_zone;

	int temp_min;
	int temp_max;
	int temp_diff;

	int cap;
	bool measure_battery_temp;

	u32 charging_max_duration_ms;
	u32 discharging_max_duration_ms;

	u32 charge_voltage_max;
	u32 charge_voltage_drop;
	u32 normal_charge_voltage_max;
	u32 normal_charge_voltage_drop;
	u32 fast_charge_voltage_max;
	u32 fast_charge_voltage_drop;

	int charger_status;
	u32 charger_type;
	int trigger_cnt;
	int first_trigger_cnt;
	int uvlo_trigger_cnt;
	int low_temp_trigger_cnt;

	u32 cap_one_time;

	u32 trickle_time_out;
	u64 trickle_time;
	u64 trickle_start_time;

	u64 update_capacity_time;
	u64 last_query_time;

	bool force_set_full;
	u32 shutdown_voltage;

	u32 wdt_interval;

	int thm_adjust_cur;

	struct charger_jeita_table *jeita_tab;
	u32 jeita_tab_size;
	bool jeita_disabled;
	struct charger_jeita_table *jeita_tab_array[CM_JEITA_MAX];

	int temperature;

	int internal_resist;
	int cap_table_len;
	struct power_supply_battery_ocv_table *cap_table;

	bool is_fast_charge;
	bool enable_fast_charge;
	u32 fast_charge_enable_count;
	u32 fast_charge_disable_count;
	u32 double_ic_total_limit_current;
};

#define PSY_NAME_MAX	30

/**
 * struct charger_manager
 * @entry: entry for list
 * @dev: device pointer
 * @desc: instance of charger_desc
 * @fuel_gauge: power_supply for fuel gauge
 * @charger_stat: array of power_supply for chargers
 * @tzd_batt : thermal zone device for battery
 * @charger_enabled: the state of charger
 * @fullbatt_vchk_jiffies_at:
 *	jiffies at the time full battery check will occur.
 * @fullbatt_vchk_work: work queue for full battery check
 * @emergency_stop:
 *	When setting true, stop charging
 * @psy_name_buf: the name of power-supply-class for charger manager
 * @charger_psy: power_supply for charger manager
 * @status_save_ext_pwr_inserted:
 *	saved status of external power before entering suspend-to-RAM
 * @status_save_batt:
 *	saved status of battery before entering suspend-to-RAM
 * @charging_start_time: saved start time of enabling charging
 * @charging_end_time: saved end time of disabling charging
 * @charging_status: saved charging status, 0 means charging normal
 */
struct charger_manager {
	struct list_head entry;
	struct device *dev;
	struct charger_desc *desc;

#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzd_batt;
#endif
	bool charger_enabled;

	unsigned long fullbatt_vchk_jiffies_at;
	struct delayed_work fullbatt_vchk_work;
	struct delayed_work cap_update_work;
	struct delayed_work uvlo_work;
	int emergency_stop;

	char psy_name_buf[PSY_NAME_MAX + 1];
	struct power_supply_desc charger_psy_desc;
	struct power_supply *charger_psy;

	u64 charging_start_time;
	u64 charging_end_time;
	u32 charging_status;
	struct cm_track_capacity track;
	struct wakeup_source charge_ws;
};

#ifdef CONFIG_CHARGER_MANAGER
extern void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg);
#else
static inline void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg) { }
#endif
#endif /* _CHARGER_MANAGER_H */

/*get battery id from cmdline*/
static int get_battery_id(void)
{
	extern char *saved_command_line;
	char result[2] = {'\0'};
	char *match = strstr(saved_command_line, "bat.id=");

	if (match) {
	memcpy(result, (match + strlen("bat.id=")), sizeof(result) - 1);
	if (result[0] == '0')
		return 0;
	else
		return 1;
	}
	return 0;

};

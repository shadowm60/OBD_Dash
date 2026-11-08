#include "obd_handler.h"
#include "isotp.h"


static const char *TAG = "OBD_Handler";

static obd_live_data_t obd_live_data;

void obd_handler_process_supported_pid(uint16_t size, uint8_t *data);
void obd_handler_monitor_sts(uint16_t size, uint8_t *data);
void obd_handler_unhandled_pid(uint16_t size, uint8_t *data);
void obd_handler_fuel_system_sts(uint16_t size, uint8_t *data);
void obd_handler_engine_load(uint16_t size, uint8_t *data);
void obd_handler_engine_coolant(uint16_t size, uint8_t *data);
void obd_handler_STFT_B1(uint16_t size, uint8_t *data);
void obd_handler_LTFT_B1(uint16_t size, uint8_t *data);
void obd_handler_STFT_B2(uint16_t size, uint8_t *data);
void obd_handler_LTFT_B2(uint16_t size, uint8_t *data);
void obd_handler_rpm(uint16_t size, uint8_t *data);
void obd_handler_vss(uint16_t size, uint8_t *data);
void obd_handler_advance(uint16_t size, uint8_t *data);
void obd_handler_iat(uint16_t size, uint8_t *data);
void obd_handler_maf(uint16_t size, uint8_t *data);
void obd_handler_tps(uint16_t size, uint8_t *data);
void obd_handler_fuel_level(uint16_t size, uint8_t *data);
void obd_handler_battery_voltage(uint16_t size, uint8_t *data);
void obd_handler_oil_temp(uint16_t size, uint8_t *data);
void obd_handler_fuel_rate(uint16_t size, uint8_t *data);

static ObdPidServiceData obd_pid_info[MAX_PID_COUNT] = 
{
/* 0x00 - 0x0F */
{ .pid = PID_00_SUPPORTED_01_20,            .pid_data_len = 4, .control = IS_SUPPORTED, .timeStamp = 0, .handler = obd_handler_process_supported_pid, .description = "PID_00_SUPPORTED_01_20",  },
{ .pid = PID_01_MONITOR_STATUS_SINCE_DTC,   .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_monitor_sts, .description = "PID_01_MONITOR_STATUS_SINCE_DTC",  },
{ .pid = PID_02_FREEZE_FRAME_DTC,           .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_02_FREEZE_FRAME_DTC",  },
{ .pid = PID_03_FUEL_SYSTEM_STATUS,         .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_fuel_system_sts, .description = "PID_03_FUEL_SYSTEM_STATUS",  },
{ .pid = PID_04_ENGINE_LOAD,                .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_engine_load, .description = "PID_04_ENGINE_LOAD",  },
{ .pid = PID_05_COOLANT_TEMP,               .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_engine_coolant, .description = "PID_05_COOLANT_TEMP",  },
{ .pid = PID_06_STFT_BANK1,                 .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_STFT_B1, .description = "PID_06_STFT_BANK1",  },
{ .pid = PID_07_LTFT_BANK1,                 .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_LTFT_B1, .description = "PID_07_LTFT_BANK1",  },
{ .pid = PID_08_STFT_BANK2,                 .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_STFT_B2, .description = "PID_08_STFT_BANK2",  },
{ .pid = PID_09_LTFT_BANK2,                 .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_LTFT_B2, .description = "PID_09_LTFT_BANK2",  },
{ .pid = PID_0A_FUEL_PRESSURE,              .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_0A_FUEL_PRESSURE",  },
{ .pid = PID_0B_INTAKE_PRESSURE,            .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_0B_INTAKE_PRESSURE",  },
{ .pid = PID_0C_ENGINE_RPM,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_rpm, .description = "PID_0C_ENGINE_RPM",  },
{ .pid = PID_0D_VEHICLE_SPEED,              .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_vss, .description = "PID_0D_VEHICLE_SPEED",  },
{ .pid = PID_0E_TIMING_ADVANCE,             .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_advance, .description = "PID_0E_TIMING_ADVANCE",  },
{ .pid = PID_0F_INTAKE_TEMP,                .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_iat, .description = "PID_0F_INTAKE_TEMP",  },

/* 0x10 - 0x1F */
{ .pid = PID_10_MAF_FLOW_RATE,              .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_maf, .description = "PID_10_MAF_FLOW_RATE",  },
{ .pid = PID_11_THROTTLE_POS,               .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_tps, .description = "PID_11_THROTTLE_POS",  },
{ .pid = PID_12_SECONDARY_AIR_STATUS,       .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_12_SECONDARY_AIR_STATUS",  },
{ .pid = PID_13_O2_SENSORS_PRESENT_2BANKS,  .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_13_O2_SENSORS_PRESENT_2BANKS",  },
{ .pid = PID_14_O2_SENSOR1,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_14_O2_SENSOR1",  },
{ .pid = PID_15_O2_SENSOR2,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_15_O2_SENSOR2",  },
{ .pid = PID_16_O2_SENSOR3,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_16_O2_SENSOR3",  },
{ .pid = PID_17_O2_SENSOR4,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_17_O2_SENSOR4",  },
{ .pid = PID_18_O2_SENSOR5,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_18_O2_SENSOR5",  },
{ .pid = PID_19_O2_SENSOR6,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_19_O2_SENSOR6",  },
{ .pid = PID_1A_O2_SENSOR7,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_1A_O2_SENSOR7",  },
{ .pid = PID_1B_O2_SENSOR8,                 .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_1B_O2_SENSOR8",  },
{ .pid = PID_1C_OBD_STANDARDS,              .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_1C_OBD_STANDARDS",  },
{ .pid = PID_1D_O2_SENSORS_PRESENT_4BANKS,  .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_1D_O2_SENSORS_PRESENT_4BANKS",  },
{ .pid = PID_1E_AUX_INPUT_STATUS,           .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_1E_AUX_INPUT_STATUS",  },
{ .pid = PID_1F_RUNTIME_SINCE_START,        .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_1F_RUNTIME_SINCE_START",  },

/* 0x20 - 0x2F */
{ .pid = PID_20_SUPPORTED_21_40,              .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_process_supported_pid, .description = "PID_20_SUPPORTED_21_40",  },
{ .pid = PID_21_DISTANCE_WITH_MIL,            .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_21_DISTANCE_WITH_MIL",  },
{ .pid = PID_22_FUEL_RAIL_PRESSURE_VAC,       .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_22_FUEL_RAIL_PRESSURE_VAC",  },
{ .pid = PID_23_FUEL_RAIL_PRESSURE_DIRECT,    .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_23_FUEL_RAIL_PRESSURE_DIRECT",  },
{ .pid = PID_24_O2_SENSOR1_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_24_O2_SENSOR1_LAMBDA_VOLT",  },
{ .pid = PID_25_O2_SENSOR2_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_25_O2_SENSOR2_LAMBDA_VOLT",  },
{ .pid = PID_26_O2_SENSOR3_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_26_O2_SENSOR3_LAMBDA_VOLT",  },
{ .pid = PID_27_O2_SENSOR4_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_27_O2_SENSOR4_LAMBDA_VOLT",  },
{ .pid = PID_28_O2_SENSOR5_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_28_O2_SENSOR5_LAMBDA_VOLT",  },
{ .pid = PID_29_O2_SENSOR6_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_29_O2_SENSOR6_LAMBDA_VOLT",  },
{ .pid = PID_2A_O2_SENSOR7_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_2A_O2_SENSOR7_LAMBDA_VOLT",  },
{ .pid = PID_2B_O2_SENSOR8_LAMBDA_VOLT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_2B_O2_SENSOR8_LAMBDA_VOLT",  },
{ .pid = PID_2C_COMMANDED_EGR,                .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_2C_COMMANDED_EGR",  },
{ .pid = PID_2D_EGR_ERROR,                    .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_2D_EGR_ERROR",  },
{ .pid = PID_2E_EVAP_PURGE,                   .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_2E_EVAP_PURGE",  },
{ .pid = PID_2F_FUEL_TANK_LEVEL,              .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_fuel_level, .description = "PID_2F_FUEL_TANK_LEVEL",  },

/* 0x30 - 0x3F */
{ .pid = PID_30_WARMUPS_SINCE_CLEAR,             .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_30_WARMUPS_SINCE_CLEAR",  },
{ .pid = PID_31_DISTANCE_SINCE_CLEAR,            .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_31_DISTANCE_SINCE_CLEAR",  },
{ .pid = PID_32_EVAP_PRESSURE,                   .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_32_EVAP_PRESSURE",  },
{ .pid = PID_33_BAROMETRIC_PRESSURE,             .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_33_BAROMETRIC_PRESSURE",  },
{ .pid = PID_34_O2_SENSOR1_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_34_O2_SENSOR1_LAMBDA_CURRENT",  },
{ .pid = PID_35_O2_SENSOR2_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_35_O2_SENSOR2_LAMBDA_CURRENT",  },
{ .pid = PID_36_O2_SENSOR3_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_36_O2_SENSOR3_LAMBDA_CURRENT",  },
{ .pid = PID_37_O2_SENSOR4_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_37_O2_SENSOR4_LAMBDA_CURRENT",  },
{ .pid = PID_38_O2_SENSOR5_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_38_O2_SENSOR5_LAMBDA_CURRENT",  },
{ .pid = PID_39_O2_SENSOR6_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_39_O2_SENSOR6_LAMBDA_CURRENT",  },
{ .pid = PID_3A_O2_SENSOR7_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_3A_O2_SENSOR7_LAMBDA_CURRENT",  },
{ .pid = PID_3B_O2_SENSOR8_LAMBDA_CURRENT,       .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_3B_O2_SENSOR8_LAMBDA_CURRENT",  },
{ .pid = PID_3C_CAT_TEMP_B1S1,                   .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_3C_CAT_TEMP_B1S1",  },
{ .pid = PID_3D_CAT_TEMP_B2S1,                   .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_3D_CAT_TEMP_B2S1",  },
{ .pid = PID_3E_CAT_TEMP_B1S2,                   .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_3E_CAT_TEMP_B1S2",  },
{ .pid = PID_3F_CAT_TEMP_B2S2,                   .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_3F_CAT_TEMP_B2S2",  },

/* 0x40 - 0x4F */
{ .pid = PID_40_SUPPORTED_41_60,                    .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_process_supported_pid, .description = "PID_40_SUPPORTED_41_60",  },
{ .pid = PID_41_MONITOR_STATUS_THIS_CYCLE,          .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_41_MONITOR_STATUS_THIS_CYCLE",  },
{ .pid = PID_42_CONTROL_MODULE_VOLTAGE,             .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_battery_voltage, .description = "PID_42_CONTROL_MODULE_VOLTAGE",  },
{ .pid = PID_43_ABSOLUTE_LOAD,                      .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_43_ABSOLUTE_LOAD",  },
{ .pid = PID_44_COMMANDED_EQUIV_RATIO,              .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_44_COMMANDED_EQUIV_RATIO",  },
{ .pid = PID_45_RELATIVE_THROTTLE_POS,              .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_45_RELATIVE_THROTTLE_POS",  },
{ .pid = PID_46_AMBIENT_TEMP,                       .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_46_AMBIENT_TEMP",  },
{ .pid = PID_47_ABS_THROTTLE_POS_B,                 .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_47_ABS_THROTTLE_POS_B",  },
{ .pid = PID_48_ABS_THROTTLE_POS_C,                 .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_48_ABS_THROTTLE_POS_C",  },
{ .pid = PID_49_ACCEL_POS_D,                        .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_49_ACCEL_POS_D",  },
{ .pid = PID_4A_ACCEL_POS_E,                        .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_4A_ACCEL_POS_E",  },
{ .pid = PID_4B_ACCEL_POS_F,                        .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_4B_ACCEL_POS_F",  },
{ .pid = PID_4C_COMMANDED_THROTTLE_ACTUATOR,        .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_4C_COMMANDED_THROTTLE_ACTUATOR",  },
{ .pid = PID_4D_TIME_MIL_ON,                        .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_4D_TIME_MIL_ON",  },
{ .pid = PID_4E_TIME_SINCE_CLEAR,                   .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_4E_TIME_SINCE_CLEAR",  },
{ .pid = PID_4F_MAX_EQUIV_RATIO_VOLT_CURR_PRESS,    .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_4F_MAX_EQUIV_RATIO_VOLT_CURR_PRESS",  },

/* 0x50 - 0x5F */
{ .pid = PID_50_MAX_MAF_FLOW_RATE,        .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_50_MAX_MAF_FLOW_RATE",  },
{ .pid = PID_51_FUEL_TYPE,                .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_51_FUEL_TYPE",  },
{ .pid = PID_52_ETHANOL_PERCENT,          .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_52_ETHANOL_PERCENT",  },
{ .pid = PID_53_ABS_EVAP_PRESSURE,        .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_53_ABS_EVAP_PRESSURE",  },
{ .pid = PID_54_EVAP_PRESSURE_ALT,        .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_54_EVAP_PRESSURE_ALT",  },
{ .pid = PID_55_STFT_O2_TRIM_B1_B3,       .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_55_STFT_O2_TRIM_B1_B3",  },
{ .pid = PID_56_LTFT_O2_TRIM_B1_B3,       .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_56_LTFT_O2_TRIM_B1_B3",  },
{ .pid = PID_57_STFT_O2_TRIM_B2_B4,       .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_57_STFT_O2_TRIM_B2_B4",  },
{ .pid = PID_58_LTFT_O2_TRIM_B2_B4,       .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_58_LTFT_O2_TRIM_B2_B4",  },
{ .pid = PID_59_FUEL_RAIL_ABS_PRESSURE,   .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_59_FUEL_RAIL_ABS_PRESSURE",  },
{ .pid = PID_5A_REL_ACCEL_POS,            .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_5A_REL_ACCEL_POS",  },
{ .pid = PID_5B_HYBRID_BATTERY_LIFE,      .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_5B_HYBRID_BATTERY_LIFE",  },
{ .pid = PID_5C_ENGINE_OIL_TEMP,          .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_oil_temp, .description = "PID_5C_ENGINE_OIL_TEMP",  },
{ .pid = PID_5D_FUEL_INJECTION_TIMING,    .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_5D_FUEL_INJECTION_TIMING",  },
{ .pid = PID_5E_ENGINE_FUEL_RATE,         .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_fuel_rate, .description = "PID_5E_ENGINE_FUEL_RATE",  },
{ .pid = PID_5F_EMISSION_REQUIREMENTS,    .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_5F_EMISSION_REQUIREMENTS",  },

/* 0x60 - 0x6F */
{ .pid = PID_60_SUPPORTED_61_80,              .pid_data_len =  4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_process_supported_pid, .description = "PID_60_SUPPORTED_61_80",  },
{ .pid = PID_61_ENGINE_TORQUE_DRIVER_DEMAND,  .pid_data_len =  1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_61_ENGINE_TORQUE_DRIVER_DEMAND",  },
{ .pid = PID_62_ENGINE_TORQUE_ACTUAL,         .pid_data_len =  1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_62_ENGINE_TORQUE_ACTUAL",  },
{ .pid = PID_63_ENGINE_REFERENCE_TORQUE,      .pid_data_len =  2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_63_ENGINE_REFERENCE_TORQUE",  },
{ .pid = PID_64_ENGINE_TORQUE_DATA,           .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_64_ENGINE_TORQUE_DATA",  },
{ .pid = PID_65_AUX_IO_SUPPORTED,             .pid_data_len =  2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_65_AUX_IO_SUPPORTED",  },
{ .pid = PID_66_MAF_SENSOR_DATA,              .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_66_MAF_SENSOR_DATA",  },
{ .pid = PID_67_COOLANT_TEMP_SENSORS,         .pid_data_len =  3, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_67_COOLANT_TEMP_SENSORS",  },
{ .pid = PID_68_INTAKE_TEMP_SENSORS,          .pid_data_len =  3, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_68_INTAKE_TEMP_SENSORS",  },
{ .pid = PID_69_EGR_COMMANDED_ACTUAL_ERROR,   .pid_data_len =  7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_69_EGR_COMMANDED_ACTUAL_ERROR",  },
{ .pid = PID_6A_DIESEL_INTAKE_FLOW_CONTROL,   .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_6A_DIESEL_INTAKE_FLOW_CONTROL",  },
{ .pid = PID_6B_EGR_TEMPERATURE,              .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_6B_EGR_TEMPERATURE",  },
{ .pid = PID_6C_THROTTLE_ACTUATOR_CONTROL,    .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_6C_THROTTLE_ACTUATOR_CONTROL",  },
{ .pid = PID_6D_FUEL_PRESSURE_CONTROL,        .pid_data_len = 11, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_6D_FUEL_PRESSURE_CONTROL",  },
{ .pid = PID_6E_INJECTION_PRESSURE_CONTROL,   .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_6E_INJECTION_PRESSURE_CONTROL",  },
{ .pid = PID_6F_TURBO_INLET_PRESSURE,         .pid_data_len =  3, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_6F_TURBO_INLET_PRESSURE",  },

/* 0x70 - 0x7F */
{ .pid = PID_70_BOOST_PRESSURE_CONTROL,     .pid_data_len = 10, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_70_BOOST_PRESSURE_CONTROL",  },
{ .pid = PID_71_VGT_CONTROL,                .pid_data_len =  6, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_71_VGT_CONTROL",  },
{ .pid = PID_72_WASTEGATE_CONTROL,          .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_72_WASTEGATE_CONTROL",  },
{ .pid = PID_73_EXHAUST_PRESSURE,           .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_73_EXHAUST_PRESSURE",  },
{ .pid = PID_74_TURBOCHARGER_RPM,           .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_74_TURBOCHARGER_RPM",  },
{ .pid = PID_75_TURBO_TEMP_1,               .pid_data_len =  7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_75_TURBO_TEMP_1",  },
{ .pid = PID_76_TURBO_TEMP_2,               .pid_data_len =  7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_76_TURBO_TEMP_2",  },
{ .pid = PID_77_CHARGE_AIR_COOLER_TEMP,     .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_77_CHARGE_AIR_COOLER_TEMP",  },
{ .pid = PID_78_EGT_BANK1,                  .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_78_EGT_BANK1",  },
{ .pid = PID_79_EGT_BANK2,                  .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_79_EGT_BANK2",  },
{ .pid = PID_7A_DPF_DIFFERENTIAL_PRESSURE,  .pid_data_len =  7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_7A_DPF_DIFFERENTIAL_PRESSURE",  },
{ .pid = PID_7B_DPF_DATA,                   .pid_data_len =  7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_7B_DPF_DATA",  },
{ .pid = PID_7C_DPF_TEMPERATURE,            .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_7C_DPF_TEMPERATURE",  },
{ .pid = PID_7D_NOX_NTE_STATUS,             .pid_data_len =  1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_7D_NOX_NTE_STATUS",  },
{ .pid = PID_7E_PM_NTE_STATUS,              .pid_data_len =  1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_7E_PM_NTE_STATUS",  },
{ .pid = PID_7F_ENGINE_RUNTIME,             .pid_data_len = 13, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_7F_ENGINE_RUNTIME",  },

/* 0x80 - 0x8F */
{ .pid = PID_80_SUPPORTED_81_A0,            .pid_data_len =  4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_process_supported_pid, .description = "PID_80_SUPPORTED_81_A0",  },
{ .pid = PID_81_AECD_RUNTIME_1,             .pid_data_len = 41, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_81_AECD_RUNTIME_1",  },
{ .pid = PID_82_AECD_RUNTIME_2,             .pid_data_len = 41, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_82_AECD_RUNTIME_2",  },
{ .pid = PID_83_NOX_SENSOR,                 .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_83_NOX_SENSOR",  },
{ .pid = PID_84_MANIFOLD_SURFACE_TEMP,      .pid_data_len =  1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_84_MANIFOLD_SURFACE_TEMP",  },
{ .pid = PID_85_NOX_REAGENT_SYSTEM,         .pid_data_len = 10, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_85_NOX_REAGENT_SYSTEM",  },
{ .pid = PID_86_PM_SENSOR,                  .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_86_PM_SENSOR",  },
{ .pid = PID_87_INTAKE_MANIFOLD_PRESSURE,   .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_87_INTAKE_MANIFOLD_PRESSURE",  },
{ .pid = PID_88_SCR_INDUCE_SYSTEM,          .pid_data_len = 13, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_88_SCR_INDUCE_SYSTEM",  },
{ .pid = PID_89_AECD_RUNTIME_11_15,         .pid_data_len = 41, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_89_AECD_RUNTIME_11_15",  },
{ .pid = PID_8A_AECD_RUNTIME_16_20,         .pid_data_len = 41, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_8A_AECD_RUNTIME_16_20",  },
{ .pid = PID_8B_DIESEL_AFTERTREATMENT,      .pid_data_len =  7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_8B_DIESEL_AFTERTREATMENT",  },
{ .pid = PID_8C_O2_SENSOR_WIDE_RANGE,       .pid_data_len = 17, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_8C_O2_SENSOR_WIDE_RANGE",  },
{ .pid = PID_8D_THROTTLE_POSITION_G,        .pid_data_len =  1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_8D_THROTTLE_POSITION_G",  },
{ .pid = PID_8E_ENGINE_FRICTION_TORQUE,     .pid_data_len =  1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_8E_ENGINE_FRICTION_TORQUE",  },
{ .pid = PID_8F_PM_SENSOR_BANK1_2,          .pid_data_len =  7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_8F_PM_SENSOR_BANK1_2",  },

/* 0x90 - 0x9F */
{ .pid = PID_90_WWH_OBD_SYSTEM_INFO_1,       .pid_data_len =  3, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_90_WWH_OBD_SYSTEM_INFO_1",  },
{ .pid = PID_91_WWH_OBD_SYSTEM_INFO_2,       .pid_data_len =  5, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_91_WWH_OBD_SYSTEM_INFO_2",  },
{ .pid = PID_92_FUEL_SYSTEM_CONTROL,         .pid_data_len =  2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_92_FUEL_SYSTEM_CONTROL",  },
{ .pid = PID_93_WWH_OBD_COUNTERS_SUPPORT,    .pid_data_len =  3, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_93_WWH_OBD_COUNTERS_SUPPORT",  },
{ .pid = PID_94_NOX_WARNING_INDUCEMENT,      .pid_data_len = 12, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_94_NOX_WARNING_INDUCEMENT",  },
{ .pid = 0x95,                               .pid_data_len =  0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0x96,                               .pid_data_len =  0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0x97,                               .pid_data_len =  0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = PID_98_EGT_SENSOR_1,                .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_98_EGT_SENSOR_1",  },
{ .pid = PID_99_EGT_SENSOR_2,                .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_99_EGT_SENSOR_2",  },
{ .pid = PID_9A_HYBRID_EV_BATTERY_VOLTAGE,   .pid_data_len =  6, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_9A_HYBRID_EV_BATTERY_VOLTAGE",  },
{ .pid = PID_9B_DEF_SENSOR_DATA,             .pid_data_len =  4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_9B_DEF_SENSOR_DATA",  },
{ .pid = PID_9C_O2_SENSOR_DATA,              .pid_data_len = 17, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_9C_O2_SENSOR_DATA",  },
{ .pid = PID_9D_ENGINE_FUEL_RATE_GPS,        .pid_data_len =  4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_9D_ENGINE_FUEL_RATE_GPS",  },
{ .pid = PID_9E_ENGINE_EXHAUST_FLOW_RATE,    .pid_data_len =  2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_9E_ENGINE_EXHAUST_FLOW_RATE",  },
{ .pid = PID_9F_FUEL_SYSTEM_PERCENTAGE_USE,  .pid_data_len =  9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_9F_FUEL_SYSTEM_PERCENTAGE_USE",  },

/* 0xA0 - 0xAF */
{ .pid = PID_A0_SUPPORTED_A1_C0,            .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_process_supported_pid, .description = "PID_A0_SUPPORTED_A1_C0",  },
{ .pid = PID_A1_NOX_SENSOR_CORRECTED,       .pid_data_len = 9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A1_NOX_SENSOR_CORRECTED",  },
{ .pid = PID_A2_CYLINDER_FUEL_RATE,         .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A2_CYLINDER_FUEL_RATE",  },
{ .pid = PID_A3_EVAP_PRESSURE,              .pid_data_len = 9, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A3_EVAP_PRESSURE",  },
{ .pid = PID_A4_TRANSMISSION_ACTUAL_GEAR,   .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A4_TRANSMISSION_ACTUAL_GEAR",  },
{ .pid = PID_A5_DEF_DOSING_COMMAND,         .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A5_DEF_DOSING_COMMAND",  },
{ .pid = PID_A6_ODOMETER,                   .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A6_ODOMETER",  },
{ .pid = PID_A7_NOX_SENSOR_3_4,             .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A7_NOX_SENSOR_3_4",  },
{ .pid = PID_A8_NOX_SENSOR_CORRECTED_3_4,   .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A8_NOX_SENSOR_CORRECTED_3_4",  },
{ .pid = PID_A9_ABS_DISABLE_SWITCH,         .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_A9_ABS_DISABLE_SWITCH",  },
{ .pid = 0xAA,                              .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xAB,                              .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xAC,                              .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xAD,                              .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xAE,                              .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xAF,                              .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },

/* 0xB0 - 0xBF */
{ .pid = 0xB0,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB1,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB2,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB3,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB4,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB5,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB6,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB7,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB8,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xB9,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xBA,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xBB,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xBC,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xBD,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xBE,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xBF,   .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },

/* 0xC0 - 0xCF */
{ .pid = PID_C0_SUPPORTED_C1_E0,                .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_process_supported_pid, .description = "PID_C0_SUPPORTED_C1_E0",  },
{ .pid = 0xC1,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xC2,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = PID_C3_FUEL_LEVEL_INPUT_AB,            .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_C3_FUEL_LEVEL_INPUT_AB",  },
{ .pid = PID_C4_EXHAUST_PARTICULATE_DIAG_TIME,  .pid_data_len = 8, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_C4_EXHAUST_PARTICULATE_DIAG_TIME",  },
{ .pid = PID_C5_FUEL_PRESSURE_AB,               .pid_data_len = 4, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_C5_FUEL_PRESSURE_AB",  },
{ .pid = PID_C6_PARTICULATE_CONTROL_STATUS,     .pid_data_len = 7, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_C6_PARTICULATE_CONTROL_STATUS",  },
{ .pid = PID_C7_DISTANCE_SINCE_REFLASH,         .pid_data_len = 2, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_C7_DISTANCE_SINCE_REFLASH",  },
{ .pid = PID_C8_NOX_PARTICULATE_WARNING_LAMP,   .pid_data_len = 1, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_C8_NOX_PARTICULATE_WARNING_LAMP",  },
{ .pid = 0xC9,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "PID_C9_UNASSIGNED",  },
{ .pid = 0xCA,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xCB,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xCC,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xCD,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xCE,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },
{ .pid = 0xCF,                                  .pid_data_len = 0, .control = NO_DATA, .timeStamp = 0, .handler = obd_handler_unhandled_pid, .description = "N/A",  },

};


static obd_handler_state_t      obd_handler_state; 
static obd_handler_pid_states_t obd_handler_pid_states;
static uint8_t cyclic_read_idx = 0; // last pid that was read

#define DISCOVERY_TIMEOUT_MS   500
#define DISCOVERY_MAX_RETRIES  3

static uint32_t discovery_req_timestamp = 0;
static uint8_t discovery_retries = 0;
static uint8_t current_discovery_pid = PID_00_SUPPORTED_01_20;

static void obd_handler_discover_supported_pids(uint16_t size, uint8_t next_pid);
static void obd_handler_discover_supported_pids_cyclic( void );
static void obd_handler_cyclic_read( void );

static uint32_t get_timestamp_ms ( void ) {
    TickType_t ticks = xTaskGetTickCount();
    uint32_t time_ms = ticks * portTICK_PERIOD_MS;
    return time_ms;
}

int get_nbr_of_all_services(void) {
    return sizeof(obd_pid_info)/sizeof(ObdPidServiceData);

}
void get_service_description(int idx, char *text)
{
    if ((text!=NULL) && (idx < MAX_PID_COUNT)) {
        strcpy(text, obd_pid_info[idx].description);
    }
}

bool is_row_checked(int idx) {
    if (idx < MAX_PID_COUNT) {
        return (obd_pid_info[idx].control & IS_ENABLED)?true:false;
    }
    return false;
}

bool is_row_enabled(int idx) {
    if (idx < MAX_PID_COUNT) {
        return (obd_pid_info[idx].control & IS_SUPPORTED)?true:false;
    }
    return false;
}

void set_row_enabled_value(int idx, bool val) {
    if (idx < MAX_PID_COUNT) {
        if (val) {
            obd_pid_info[idx].control |= IS_SUPPORTED;
        } else {
            obd_pid_info[idx].control &= ~IS_SUPPORTED;
        }
    }
}

void set_row_checked_value(int idx, bool val) {
    if (idx < MAX_PID_COUNT) {
        if (val) {
            obd_pid_info[idx].control |= (IS_ENABLED | IS_CYCLIC);
        } else {
            obd_pid_info[idx].control &= ~(IS_ENABLED | IS_CYCLIC);
        }
    }
}

void handle_pid_states( void ) {
    switch (obd_handler_pid_states) {
        case STANDBY:
            ESP_LOGI(TAG, "Resetting PID table and starting discovery at PID_00");
            for (int i = 1; i < MAX_PID_COUNT; i++) {
                obd_pid_info[i].control = NO_DATA;
            }
            obd_pid_info[PID_00_SUPPORTED_01_20].control = IS_SUPPORTED;

            current_discovery_pid = PID_00_SUPPORTED_01_20;
            discovery_retries = 0;
            discovery_req_timestamp = get_timestamp_ms();
            obd_handler_discover_supported_pids(SIZE_PARAM_NOT_USED, PID_00_SUPPORTED_01_20);
            obd_handler_pid_states = READ_SUPPORTED_PIDS;
        break;
        case READ_SUPPORTED_PIDS:
            obd_handler_discover_supported_pids_cyclic();
        break;
        case CYCLIC_READ:
            obd_handler_cyclic_read();
        break;
    }
}

void handle_obd_states( void ) {
    static uint8_t cnt = 0;
    switch(obd_handler_state) {
        case UNCONNECTED:
            //debug, startup delay
            cnt++;
            if (cnt >= 100) {
                obd_handler_state = CURRENT_DATA_READ;
            }
        break;
        case CURRENT_DATA_READ:
            handle_pid_states();
        break;
        case READ_DTCS:
        break;
        case CLEAR_DTC:
        break;
        case OBD_TEST_O2:
        break;
        case VEH_INFO:
        break;
    }
}

static void obd_handler_task( void *arg ) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    while (1) {
        handle_obd_states();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    vTaskDelete(NULL);
}

void init_obd_handler( void ) {
    ESP_LOGI(TAG, "Init OBD Handler");
    obd_handler_state = UNCONNECTED;
    obd_handler_pid_states = STANDBY;

    xTaskCreate(obd_handler_task, "OBD", 4096, NULL, 6, NULL);
}

const obd_live_data_t *obd_get_live_data(void) {
    return &obd_live_data;
}

bool obd_pid_is_supported(uint8_t pid) {
    if (pid >= MAX_PID_COUNT) {
        return false;
    }

    return (obd_pid_info[pid].control & IS_SUPPORTED) != 0;
}


/* Based on pid, we send out the discovery request */
static void obd_handler_discover_supported_pids(uint16_t size, uint8_t next_pid) {
    uint8_t tx_req[2];
    tx_req[0] = 0x01; // Service 0x01: Show current data
    tx_req[1] = next_pid;

    can_send_isotp_message(tx_req, sizeof(tx_req));
}

/* Cyclic function to get response for discovery request with timeout & retry */
static void obd_handler_discover_supported_pids_cyclic( void ) {
    uint8_t rx_resp[8];
    uint16_t size = sizeof(rx_resp);
    uint16_t out_size = 0;
    int ret = ISOTP_RET_NO_DATA;

    // Try to read response
    ret = can_get_isotp_message(rx_resp, size, &out_size);

    if (ret == ISOTP_RET_OK) {
        ESP_LOGI(TAG, "Rx: Discovery PID resp, outsize: %d", out_size);
        obd_handler_process_supported_pid(out_size, rx_resp);
    } else {
        // Check discovery timeout
        uint32_t now = get_timestamp_ms();
        if ((now - discovery_req_timestamp) > DISCOVERY_TIMEOUT_MS) {
            discovery_retries++;
            if (discovery_retries < DISCOVERY_MAX_RETRIES) {
                ESP_LOGW(TAG, "Discovery timeout for PID 0x%02X, retry %d/%d",
                         current_discovery_pid, discovery_retries, DISCOVERY_MAX_RETRIES);
                discovery_req_timestamp = now;
                obd_handler_discover_supported_pids(SIZE_PARAM_NOT_USED, current_discovery_pid);
            } else {
                ESP_LOGW(TAG, "Discovery timed out for PID 0x%02X after %d retries",
                         current_discovery_pid, DISCOVERY_MAX_RETRIES);
                if (current_discovery_pid == PID_00_SUPPORTED_01_20) {
                    // No ECU responded to PID 00 at all
                    ESP_LOGE(TAG, "ECU not responding to initial discovery. Resetting to STANDBY");
                    obd_handler_state = UNCONNECTED;
                    obd_handler_pid_states = STANDBY;
                } else {
                    // An extended discovery query (e.g. 0x20 or 0x40) didn't reply;
                    // proceed with the PIDs discovered so far rather than freezing.
                    ESP_LOGI(TAG, "Proceeding with discovered PIDs -> CYCLIC_READ");
                    obd_handler_pid_states = CYCLIC_READ;
                    cyclic_read_idx = 0;
                }
            }
        }
    }
}

/* Decodes the supported PIDs reported by the ECU and chains through subsequent PID ranges (0x20, 0x40, etc.) */
void obd_handler_process_supported_pid(uint16_t size, uint8_t *data) {
    if (data == NULL || size < 6) {
        ESP_LOGW(TAG, "Discovery response invalid or too short: size=%d", size);
        return;
    }

    uint8_t response = data[0]; // 0x41 is positive response (Service 0x01 + 0x40)
    uint8_t pid = data[1];

    ESP_LOGI(TAG, "Rx: Discovery Resp: 0x%02X PID: 0x%02X", response, pid);

    if (response != 0x41) {
        ESP_LOGW(TAG, "obd_handler_process_supported_pid: Non-positive response 0x%02X for PID 0x%02X", response, pid);
        if (pid == PID_00_SUPPORTED_01_20) {
            obd_handler_state = UNCONNECTED;
            obd_handler_pid_states = STANDBY;
        } else {
            // Extended PID discovery rejected, switch to CYCLIC_READ with existing PIDs
            ESP_LOGI(TAG, "Switching to CYCLIC_READ");
            obd_handler_pid_states = CYCLIC_READ;
            cyclic_read_idx = 0;
        }
        return;
    }

    // Must be a known discovery PID: 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0
    if ((pid % 0x20) != 0 || pid > 0xC0) {
        ESP_LOGW(TAG, "Unexpected discovery PID: 0x%02X", pid);
        return;
    }

    uint32_t bitcode = ((uint32_t)data[2] << 24) |
                       ((uint32_t)data[3] << 16) |
                       ((uint32_t)data[4] << 8)  |
                       (uint32_t)data[5];

    ESP_LOGI(TAG, "Rx: Supported PIDs for 0x%02X: 0x%08" PRIX32, pid, bitcode);

    uint8_t next_discovery_pid = pid + 0x20;

    // Decode 32 bits for PIDs (pid + 1) to (pid + 32)
    for (uint8_t itr = 0; itr < 32; itr++) {
        uint8_t target_pid = pid + 1 + itr;
        if (target_pid >= MAX_PID_COUNT) {
            break;
        }

        if ((bitcode >> (31 - itr)) & 0x01) {
            obd_pid_info[target_pid].control |= (IS_SUPPORTED | IS_ENABLED);
            // Only mark cyclic if it's an actual sensor PID, not a discovery marker PID
            if (target_pid != next_discovery_pid) {
                obd_pid_info[target_pid].control |= IS_CYCLIC;
            }
        }
    }

    // Check if the next discovery block (e.g. 0x20, 0x40, 0x60, etc.) is supported
    if (next_discovery_pid < MAX_PID_COUNT &&
        (obd_pid_info[next_discovery_pid].control & IS_SUPPORTED))
    {
        // Discovery PIDs should never be queried cyclically
        obd_pid_info[next_discovery_pid].control &= ~IS_CYCLIC;

        ESP_LOGI(TAG, "Next discovery PID 0x%02X is supported, querying...", next_discovery_pid);
        current_discovery_pid = next_discovery_pid;
        discovery_retries = 0;
        discovery_req_timestamp = get_timestamp_ms();
        obd_handler_discover_supported_pids(SIZE_PARAM_NOT_USED, next_discovery_pid);
    } else {
        ESP_LOGI(TAG, "Supported PID discovery complete. Switching to CYCLIC_READ");
        obd_handler_pid_states = CYCLIC_READ;
        cyclic_read_idx = 0;
    }
}

static void obd_handler_cyclic_read( void ) {
    uint8_t itr;
    uint8_t step;
    uint8_t tx_req[2];
    uint8_t rx_resp[8];
    uint16_t size = sizeof(rx_resp);
    uint16_t out_size = 0;
    int ret = ISOTP_RET_NO_DATA;
    uint32_t ts = get_timestamp_ms();

    if ((obd_pid_info[cyclic_read_idx].control & IS_BUSY) == IS_BUSY) {
        // We should wait for response
        ret = can_get_isotp_message(rx_resp, size, &out_size);

        if (ret == ISOTP_RET_OK) {
            uint16_t min_response_size = 2 + obd_pid_info[cyclic_read_idx].pid_data_len;
            if (min_response_size < 3) {
                min_response_size = 3;
            }

            // Validate the complete PID payload before any handler reads it.
            if (out_size <= size && out_size >= min_response_size &&
                rx_resp[0] == 0x41 && rx_resp[1] == cyclic_read_idx) {
                obd_pid_info[cyclic_read_idx].handler(out_size, rx_resp);
            } else if (out_size >= 3 && out_size <= size &&
                       rx_resp[0] == 0x7F && rx_resp[1] == CURENT_DATA) {
                uint8_t nrc = rx_resp[2];
                ESP_LOGW(TAG, "NRC 0x%02X for PID 0x%02X", nrc, cyclic_read_idx);
                if (nrc == 0x12) {
                    obd_pid_info[cyclic_read_idx].control &= ~(IS_SUPPORTED | IS_ENABLED | IS_CYCLIC);
                }
            } else {
                ESP_LOGW(TAG, "Mismatched or incomplete response for PID 0x%02X (size=%u)",
                         cyclic_read_idx, out_size);
            }
            obd_pid_info[cyclic_read_idx].control &= ~IS_BUSY;
        }
        // Check timeout
        if ((ts - obd_pid_info[cyclic_read_idx].timeStamp) > READ_TIMEOUT)
        {
            ESP_LOGW(TAG, "obd_handler_cyclic_read timeout PID: 0x%02X", cyclic_read_idx);
            obd_pid_info[cyclic_read_idx].control &= ~IS_BUSY;
        }

    } else {
        if (cyclic_read_idx == 0) {
            step = 1;
        } else {
            step = cyclic_read_idx;
        }

        for (itr = 1; itr < MAX_PID_COUNT; itr++) {
            step++;
            if (step >= MAX_PID_COUNT) {
                step = 1; // Wrap around
            }
            if ((obd_pid_info[step].control & IS_CYCLIC) == IS_CYCLIC) {
                cyclic_read_idx = step;
                obd_pid_info[step].control |= IS_BUSY;
                obd_pid_info[step].timeStamp = ts;

                // Send out request: Service 0x01, PID = step
                tx_req[0] = 0x01;
                tx_req[1] = step;
                can_send_isotp_message(tx_req, sizeof(tx_req));
                break;
            }
        }
    }
}


/* debug hook */
void obd_handler_unhandled_pid(uint16_t size, uint8_t *data) {
    //uint8_t datalen = data[0];
    uint8_t response = data[0]; //0x41 is positive, if different reset all states!
    uint8_t pid = data[1];

    ESP_LOGI(TAG, "Rx: RESP: %x PID: %x", response, pid);
}

/* data[0]:7 - Flag for MIL ON
   data[0]6:0 - Number of stored DTCs
   data[1]:7 - Reserved (0)
   data[1]6:4 - completeness of common tests
   data[1]:3 - 0-> Petrol 1-> Diesel 
   data[1]2:0 - completeness of common tests
   data[2] - engine type specific tests availability
   data[3] - engine type specific tests completness
   https://en.wikipedia.org/wiki/OBD-II_PIDs#Service_01_PID_01
*/
void obd_handler_monitor_sts(uint16_t size, uint8_t *data) {
    uint8_t response = data[0]; //0x41 is positive, if different reset all states!
    uint8_t pid = data[1];

    uint8_t MIL = (data[2] >> 7) & 0x01;
    uint8_t nbr_of_confirmed_DTCs = data[2] & 0x7F;


    ESP_LOGI(TAG, "Monitor Sts: MIL: %d DTCs: %d", MIL, nbr_of_confirmed_DTCs);
}

// PID_03
void obd_handler_fuel_system_sts(uint16_t size, uint8_t *data) {

    switch (data[2]) {
        case 0:
            ESP_LOGI(TAG, "Motor is Off");
            break;
        case 1:
            ESP_LOGI(TAG, "Open loop, warming up");
            break;
        case 2:
            ESP_LOGI(TAG, "Closed Loop with O2");
            break;
        case 4:
            ESP_LOGI(TAG, "Open loop, load");
            break;
        case 8:
            ESP_LOGI(TAG, "Open loop, failure");
            break;
        case 16:
            ESP_LOGI(TAG, "Closed loop with faults");
            break;
    }
}

// PID_04
void obd_handler_engine_load(uint16_t size, uint8_t *data) {
    obd_live_data.engine_load = (data[2] * 100 + 127) / 255;
    obd_live_data.valid_mask |= OBD_LIVE_LOAD;
    ESP_LOGI(TAG,"Calculated engine load %d", obd_live_data.engine_load);
}

// PID_05
void obd_handler_engine_coolant(uint16_t size, uint8_t *data) {
    obd_live_data.coolant_temp = data[2] - 40;
    obd_live_data.valid_mask |= OBD_LIVE_COOLANT;
    ESP_LOGI(TAG,"Engine Coolant %d", obd_live_data.coolant_temp);
}

// PID_06
void obd_handler_STFT_B1(uint16_t size, uint8_t *data) {
    ESP_LOGI(TAG,"STFT B1 %d", (((data[2] * 100 + 64) / 128) - 100));
}

// PID_07
void obd_handler_LTFT_B1(uint16_t size, uint8_t *data) {
    ESP_LOGI(TAG,"LTFT B1 %d", (((data[2] * 100 + 64) / 128) - 100));
}

// PID_08
void obd_handler_STFT_B2(uint16_t size, uint8_t *data) {
    ESP_LOGI(TAG,"STFT B2 %d", (((data[2] * 100 + 64) / 128) - 100));
}

// PID_09
void obd_handler_LTFT_B2(uint16_t size, uint8_t *data) {
    ESP_LOGI(TAG,"LTFT B2 %d", (((data[2] * 100 + 64) / 128) - 100));
}

// PID_0C
void obd_handler_rpm(uint16_t size, uint8_t *data) {
    obd_live_data.rpm = (data[2] << 8 | data[3]) / 4;
    obd_live_data.valid_mask |= OBD_LIVE_RPM;
    ESP_LOGI(TAG,"RPM %d", obd_live_data.rpm);
}

// PID_0D
void obd_handler_vss(uint16_t size, uint8_t *data) {
    obd_live_data.vehicle_speed = data[2];
    obd_live_data.valid_mask |= OBD_LIVE_SPEED;
    ESP_LOGI(TAG,"VSS %d", obd_live_data.vehicle_speed);
}

// PID_0E
void obd_handler_advance(uint16_t size, uint8_t *data) {
    obd_live_data.timing_advance = (data[2] / 2) - 64;
    obd_live_data.valid_mask |= OBD_LIVE_ADVANCE;
    ESP_LOGI(TAG,"Ign T: %d", obd_live_data.timing_advance);
}

// PID_0F
void obd_handler_iat(uint16_t size, uint8_t *data) {
    obd_live_data.intake_temp = data[2] - 40;
    obd_live_data.valid_mask |= OBD_LIVE_IAT;
    ESP_LOGI(TAG,"IAT: %d", obd_live_data.intake_temp);
}

// PID_10
void obd_handler_maf(uint16_t size, uint8_t *data) {
    obd_live_data.maf = ((data[2] << 8) | data[3]) / 100;
    obd_live_data.valid_mask |= OBD_LIVE_MAF;
    ESP_LOGI(TAG,"MAF: %d", obd_live_data.maf);
}

// PID 0x2F: fuel tank level in percent.
void obd_handler_fuel_level(uint16_t size, uint8_t *data) {
    obd_live_data.fuel_level = (data[2] * 100) / 255;
    obd_live_data.valid_mask |= OBD_LIVE_FUEL_LEVEL;
}

// PID 0x42: control module voltage in millivolts.
void obd_handler_battery_voltage(uint16_t size, uint8_t *data) {
    obd_live_data.battery_voltage_mv = ((data[2] << 8) | data[3]) * 10;
    obd_live_data.valid_mask |= OBD_LIVE_BATTERY;
}

// PID 0x5C: engine oil temperature in degrees Celsius.
void obd_handler_oil_temp(uint16_t size, uint8_t *data) {
    obd_live_data.oil_temp = data[2] - 40;
    obd_live_data.valid_mask |= OBD_LIVE_OIL_TEMP;
}

// PID 0x5E: engine fuel rate in millilitres per hour.
void obd_handler_fuel_rate(uint16_t size, uint8_t *data) {
    obd_live_data.fuel_rate_ml_per_hour = ((data[2] << 8) | data[3]) * 50;
    obd_live_data.valid_mask |= OBD_LIVE_FUEL_RATE;
}

// PID_11
void obd_handler_tps(uint16_t size, uint8_t *data) {
    ESP_LOGI(TAG,"TPS: %d", (data[2]*100)/255);
}
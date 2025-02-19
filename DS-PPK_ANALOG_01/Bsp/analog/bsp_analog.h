/*
 * bsp_analog.h
 *
 *  Created on: Jan 30, 2023
 *      Author: PSA
 */

#ifndef ANALOG_BSP_ANALOG_H_
#define ANALOG_BSP_ANALOG_H_

#include "common_inc.h"

void bsp_adc1Init();
void bsp_adc2Init();
void bsp_adc3Init();
void bsp_adc1GetValues();
void bsp_adc2GetValues();
void bsp_adc3GetValues();
void bsp_smu_set_en(bool enable);
void bsp_vdout_fet_en(bool enacle);

extern float vref;
const float gain = (1 + 40.0f /4.12f)*(20.0f / 10.0f);

struct res_val_sample_t{
		float rs_0uA_100uA;
		float rs_100uA_1mA;
		float rs_1mA_10mA;
		float rs_10mA_100mA;
		float rs_100mA_2A;
};

/* MOS 管导通内阻 */
#define RDS_ON_NMOS_CAL   0.0850  /* RDS(ON) NMOS calibration, 校准的SI2302 导通电阻取85mR */
#define RDS_ON_NMOS_SC    0.0140  /* RDS(ON) NMOS short circuit , 短路的WSD2018, 导通电阻取 14mR */
#define RDS_ON_NMOS_DUAL  0.0049  /* RDS(ON) NMOS dual, 电源开关的共漏NMOS CJAE2002,  导通电阻取 4.9mR */
#define RDS_ON_PMOS_SWX   0.0110  /* RDS(ON) PMOS sw1~sw4, 开关采样电阻的YJQ1216,  调整VLDO时，VGS 约-3~-7V, 导通电阻取 11mR */
#define RDS_ON_PMOS_RPP   0.0250  /* RDS(ON) PMOS reverse polarity protection, 反接保护DMP3028, 导通电阻取 25mR */

/* 各个档位采样电阻串联PMOS后的值 */
#define RS_SW0 1000.000 // 1KR  0.1%
#define RS_SW1 (110.000 + RDS_ON_PMOS_SWX)  // 110R 1%
#define RS_SW2 (11.000 + RDS_ON_PMOS_SWX)   // 11R  1%
#define RS_SW3 (1.100 + RDS_ON_PMOS_SWX)    // 1.1R 1%
#define RS_SW4 (0.050 + RDS_ON_PMOS_SWX)    // 50mR 1%

/* 计算不同档位下采样电阻的并联电阻 */
#define RS_0UA_100UA  RS_SW0
#define RS_100UA_1MA  1.0 / ( 1.0 / RS_SW0 + 1.0 / RS_SW1 )
#define RS_1MA_10MA   1.0 / ( 1.0 / RS_SW0 + 1.0 / RS_SW1 + 1.0 / RS_SW2)
#define RS_10MA_100MA 1.0 / ( 1.0 / RS_SW0 + 1.0 / RS_SW1 + 1.0 / RS_SW2 + 1.0 / RS_SW3 )
#define RS_100MA_2A   1.0 / ( 1.0 / RS_SW0 + 1.0 / RS_SW1 + 1.0 / RS_SW2 + 1.0 / RS_SW3 + 1.0 / RS_SW4 )


/* 自恢复保险丝阻值 */
#define R_PPTC_2016       0.0200  /* 自恢复保险丝 SMD1812B200TF/16 的电阻 0.020~0.075R */

/* 校准时仪放IN+IN1之间除了采样电阻的电阻值 */
#define R_CAL_500mR_EXC_RS
#define R_CAL_50R_EXC_RS
#define R_CAL_500R_EXC_RS
#define R_CAL_5KR_EXC_RS
#define R_CAL_50KR_EXC_RS
#define R_CAL_500KR_EXC_RS

//const float rs_offset = 0.200f;	//除了采样电阻还有200mR的测量电阻

// 采样电阻并联等效值，单位欧姆
//const struct res_val_sample_t res_val_sample = {
//		.rs_0uA_100uA = 1000.000f,
//		.rs_100uA_1mA = 99.09910f,
//		.rs_1mA_10mA = 9.900990f,
//		.rs_10mA_100mA = 0.990010f,
//		.rs_100mA_2A = 0.047596f,
//};

const struct res_val_sample_t res_val_sample = {
		.rs_0uA_100uA  = RS_0UA_100UA,
		.rs_100uA_1mA  = RS_100UA_1MA,
		.rs_1mA_10mA   = RS_1MA_10MA,
		.rs_10mA_100mA = RS_10MA_100MA,
		.rs_100mA_2A   = RS_100MA_2A,
};

const uint8_t adc2_chx_num_regular = 6;		//ADC2规则通道数
const uint8_t adc2_chx_num_inject = 0;		//ADC2注入通道数
typedef union {
	struct val_t{
		float val_vldo;		//INP4
		float val_vin;		//INP5
		float val_vntc;		//INP8
		float val_vbb;		//INP9
		float val_vlogic;	//INP10
		float val_vref_ia;	//INP11
	}float_el;
	float float_arr[adc2_chx_num_regular];
	struct data_t{
		uint16_t val_vldo;		//INP4
		uint16_t val_vin;		//INP5
		uint16_t val_vntc;		//INP8
		uint16_t val_vbb;		//INP9
		uint16_t val_vlogic;	//INP10
		uint16_t val_vref_ia;	//INP11
	}uint_el;
	uint16_t uint_arr[adc2_chx_num_regular];
}adc2_chx_values_t;

extern volatile adc2_chx_values_t adc2_values;

typedef struct
{
	union{
		/* 位域成员反映引脚电平，1：高，0：低*/
		struct {
			uint8_t sw1		:1;
			uint8_t sw2		:1;
			uint8_t sw3		:1;
			uint8_t sw4		:1;
			uint8_t unused 	:4;
		};	//匿名结构体成员
		uint8_t swx;
	};		//匿名union成员
	uint32_t timestamp;				//时间戳
}auto_sw_data_t;
extern volatile auto_sw_data_t auto_sw_data;

extern TickType_t xFrequency_adc1Task;

void bsp_auto_sw_init();

typedef enum {
	/* FUN			  		CHx */
	MUX_FUN_CAL_RES_500mR 	= 6,
	MUX_FUN_CAL_RES_50R 	= 1,
	MUX_FUN_CAL_RES_500R 	= 0,
	MUX_FUN_CAL_RES_5KR 	= 3,
	MUX_FUN_CAL_RES_50KR 	= 4,
	MUX_FUN_CAL_RES_500KR 	= 7,
	MUX_FUN_DPDT_7222_S 	= 2,
	MUX_FUN_NC 				= 5,
}mux_fun_t;
void mux_FunSet(mux_fun_t sLinesCode);
void mux_FunTest();
#endif /* ANALOG_BSP_ANALOG_H_ */

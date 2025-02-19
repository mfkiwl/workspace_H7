﻿/*
 * BSP.cpp
 *
 *  Created on: May 16, 2022
 *      Author: OldGerman
 */

#include "bsp.h"

void bsp_Init(){
	bsp_InitUart();
	bsp_tim6_Init();
	bsp_TIMx_PWM_En(&htim3, TIM_CHANNEL_4, 1);
	bsp_TIMx_PWM_En(&htim12, TIM_CHANNEL_2, 1);
	bsp_Button_Init();
}

bool firstPwrOffToRUN = true;
/* @brief 在main()执行完MX_XXX_Init()函数们，到执行setup()之前插入的函数
 * @param None
 * @return None
 */
void preSetupInit(void){
	;
}


/* 非阻塞下等待固定的时间
 * @param timeOld 必须传入局部静态变量或全局变量
 * @param 等待时间
 * @return bool
 */
bool waitTime(uint32_t *timeOld, uint32_t wait) {
	uint32_t time = HAL_GetTick();
	if ((time - *timeOld) > wait) {	//250决定按键长按的延迟步幅
		*timeOld = time;
		return true;
	}
	return false;
}


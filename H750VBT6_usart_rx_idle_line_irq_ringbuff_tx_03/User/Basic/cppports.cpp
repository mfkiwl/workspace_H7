﻿/*
 * cppports.cpp
 *
 *  Created on: May 16, 2022
 *  @Modified: OldGerman modified MaJerle's github repository example:
 *  	https://github.com/MaJerle/stm32-usart-uart-dma-rx-tx/blob/main/projects/usart_rx_idle_line_irq_ringbuff_tx_H7/Src/main.c
 *  @Notice: Fully use the HAL library API without modifying the IRQHandler function in stm32h7xx_it.c
 *  		完全使用HAL库API，无需修改stm32h7xx_it.c中的IRQHandler函数
 *
 */

#include "cppports.h"

#define UART_TO_BE_DETERMINED	0		// 我测试可以去掉的一些代码段, 0:去掉 1:保留

/* 是否在usart_start_tx_dma_transfer()函数内禁用中断 */
#define USART_TX_TRANS_DISABLE_IT   0  		// 仅当多个操作系统线程可以访问usart_start_tx_dma_transfer()函数，且未配置独占访问保护（互斥锁），
											// 或者，如果应用程序从多个中断调用此函数时，才建议在检查下一次传输之前禁用中断。

/*	Stm32CubeIDE 重定向printf
 */


/*
 * This example shows how application can implement RX and TX DMA for UART.
 * It uses simple packet example approach and 3 separate buffers:
 *
 * - Raw DMA RX buffer where DMA transfers data from UART to memory
 * - Ringbuff for RX data which are transfered from raw buffer and ready for application processin
 * - Ringbuff for TX data to be sent out by DMA TX
 */

/* Includes */
#include "main.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lwrb.h"
#include "usart.h"
/* Private function prototypes */


/* USART related functions */
void    usart_init(void);
void    dma_Init(void);
void    usart_rx_check(void);
void    usart_process_data(const void* data, size_t len);
void    usart_send_string(const char* str);
uint8_t usart_start_tx_dma_transfer(void);

/**
 * \brief           Calculate length of statically allocated array
 */
#define ARRAY_LEN(x)            (sizeof(x) / sizeof((x)[0]))

/**
 * \brief           USART RX buffer for DMA to transfer every received byte RX
 * \note            Contains raw data that are about to be processed by different events
 *
 * Special use case for STM32H7 series.
 * Default memory configuration in STM32H7 may put variables to DTCM RAM,
 * part of memory that is super fast, however DMA has no access to it.
 *
 * For this specific example, all variables are by default
 * configured in D1 RAM. This is configured in linker script
 */
uint8_t usart_rx_dma_buffer[64] __attribute__((section(".RAM_D2_Array")));

/**
 * \brief           Ring buffer instance for TX data
 */
lwrb_t usart_rx_rb;

/**
 * \brief           Ring buffer data array for RX DMA
 */
uint8_t usart_rx_rb_data[128] __attribute__((section(".RAM_D2_Array")));

/**
 * \brief           Ring buffer instance for TX data
 */
lwrb_t usart_tx_rb;

/**
 * \brief           Ring buffer data array for TX DMA
 */
uint8_t usart_tx_rb_data[128] __attribute__((section(".RAM_D2_Array")));

/**
 * \brief           Length of currently active TX DMA transfer
 */
volatile size_t usart_tx_dma_current_len;

/**
 * \brief           Check for new data received with DMA
 *
 * User must select context to call this function from:
 * - Only interrupts (DMA HT, DMA TC, UART IDLE) with same preemption priority level
 * - Only thread context (outside interrupts)
 *
 * If called from both context-es, exclusive access protection must be implemented
 * This mode is not advised as it usually means architecture design problems
 *
 * When IDLE interrupt is not present, application must rely only on thread context,
 * by manually calling function as quickly as possible, to make sure
 * data are read from raw buffer and processed.
 *
 * Not doing reads fast enough may cause DMA to overflow unread received bytes,
 * hence application will lost useful data.
 *
 * Solutions to this are:
 * - Improve architecture design to achieve faster reads
 * - Increase raw buffer size and allow DMA to write more data before this function is called
 */
void usart_rx_check(void) {
    static size_t old_pos;
    size_t pos;

    /* Calculate current position in buffer and check for new data available */
    pos = ARRAY_LEN(usart_rx_dma_buffer) - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    if (pos != old_pos) {                       /* Check change in received data */
        if (pos > old_pos) {                    /* Current position is over previous one */
            /*
             * Processing is done in "linear" mode.
             *
             * Application processing is fast with single data block,
             * length is simply calculated by subtracting pointers
             *
             * [   0   ]
             * [   1   ] <- old_pos |------------------------------------|
             * [   2   ]            |                                    |
             * [   3   ]            | Single block (len = pos - old_pos) |
             * [   4   ]            |                                    |
             * [   5   ]            |------------------------------------|
             * [   6   ] <- pos
             * [   7   ]
             * [ N - 1 ]
             */
            usart_process_data(&usart_rx_dma_buffer[old_pos], pos - old_pos);
        } else {
            /*
             * Processing is done in "overflow" mode..
             *
             * Application must process data twice,
             * since there are 2 linear memory blocks to handle
             *
             * [   0   ]            |---------------------------------|
             * [   1   ]            | Second block (len = pos)        |
             * [   2   ]            |---------------------------------|
             * [   3   ] <- pos
             * [   4   ] <- old_pos |---------------------------------|
             * [   5   ]            |                                 |
             * [   6   ]            | First block (len = N - old_pos) |
             * [   7   ]            |                                 |
             * [ N - 1 ]            |---------------------------------|
             */
            usart_process_data(&usart_rx_dma_buffer[old_pos], ARRAY_LEN(usart_rx_dma_buffer) - old_pos);
            if (pos > 0) {
                usart_process_data(&usart_rx_dma_buffer[0], pos);
            }
        }
        old_pos = pos;                          /* Save current position as old for next transfers */
    }
}

/**
 * \brief           Check if DMA is active and if not try to send data
 *
 * This function can be called either by application to start data transfer
 * or from DMA TX interrupt after previous transfer just finished
 *
 * \return          `1` if transfer just started, `0` if on-going or no data to transmit
 */
uint8_t usart_start_tx_dma_transfer(void) {
    uint32_t primask;
    uint8_t started = 0;

    /*
     * First check if transfer is currently in-active,
     * by examining the value of usart_tx_dma_current_len variable.
     *
     * This variable is set before DMA transfer is started and cleared in DMA TX complete interrupt.
     *
     * It is not necessary to disable the interrupts before checking the variable:
     *
     * When usart_tx_dma_current_len == 0
     *    - This function is called by either application or TX DMA interrupt
     *    - When called from interrupt, it was just reset before the call,
     *         indicating transfer just completed and ready for more
     *    - When called from an application, transfer was previously already in-active
     *         and immediate call from interrupt cannot happen at this moment
     *
     * When usart_tx_dma_current_len != 0
     *    - This function is called only by an application.
     *    - It will never be called from interrupt with usart_tx_dma_current_len != 0 condition
     *
     * Disabling interrupts before checking for next transfer is advised
     * only if multiple operating system threads can access to this function w/o
     * exclusive access protection (mutex) configured,
     * or if application calls this function from multiple interrupts.
     * 仅当多个操作系统线程可以访问此函数，且未配置独占访问保护（互斥锁），
     * 或者，如果应用程序从多个中断调用此函数时，才建议在检查下一次传输之前禁用中断。
     *
     * This example assumes worst use case scenario,
     * hence interrupts are disabled prior every check
     */
#if USART_TX_TRANS_DISABLE_IT
    primask = __get_PRIMASK();
    __disable_irq();
#endif
    if (usart_tx_dma_current_len == 0
            && (usart_tx_dma_current_len = lwrb_get_linear_block_read_length(&usart_tx_rb)) > 0) {
#if UART_TO_BE_DETERMINED
    	//HAL库的IRQ函数好像会解决
        /* Disable channel if enabled */
    	__HAL_DMA_DISABLE(&hdma_usart1_tx);

        /* Clear all flags */
        __HAL_DMA_CLEAR_FLAG(&hdma_usart1_tx, DMA_FLAG_TCIF1_5);
        __HAL_DMA_CLEAR_FLAG(&hdma_usart1_tx, DMA_FLAG_HTIF1_5);
        __HAL_DMA_CLEAR_FLAG(&hdma_usart1_tx, DMA_FLAG_TEIF1_5);
        __HAL_DMA_CLEAR_FLAG(&hdma_usart1_tx, DMA_FLAG_DMEIF1_5);
        __HAL_DMA_CLEAR_FLAG(&hdma_usart1_tx, DMA_FLAG_FEIF1_5);
#endif
        /* Prepare DMA data, length, and start transfer */
        HAL_UART_Transmit_DMA(&huart1, (uint8_t *)lwrb_get_linear_block_read_address(&usart_tx_rb), usart_tx_dma_current_len);

        started = 1;
    }
#if USART_TX_TRANS_DISABLE_IT
    __set_PRIMASK(primask);
#endif
    return started;
}

/**
 * \brief           Process received data over UART
 * Data are written to RX ringbuffer for application processing at latter stage
 * \param[in]       data: Data to process
 * \param[in]       len: Length in units of bytes
 */
void usart_process_data(const void* data, size_t len) {
    lwrb_write(&usart_rx_rb, data, len);  /* Write data to receive buffer */
}

/**
 * \brief           Send string over USART
 * \param[in]       str: String to send
 */
void usart_send_string(const char* str) {
    lwrb_write(&usart_tx_rb, str, strlen(str));   /* Write data to transmit buffer */
    usart_start_tx_dma_transfer();
}


/**
 * \brief           USART1 Initialization Function
 */
void usart_init(void) {
#if UART_TO_BE_DETERMINED
	/* Enable RX DMA HT & TC interrupts */
     __HAL_DMA_ENABLE_IT(&hdma_usart1_rx, DMA_IT_HT);	// hdma_usart1_rx.Instance = DMA1_Stream0;
     __HAL_DMA_ENABLE_IT(&hdma_usart1_rx, DMA_IT_TC);

    /* Enable TX DMA  TC interrupts */
    __HAL_DMA_ENABLE_IT(&hdma_usart1_tx, DMA_IT_TC);	// hdma_usart1_tx.Instance = DMA1_Stream1;
    __HAL_DMA_DISABLE_IT(&hdma_usart1_tx, DMA_IT_HT);

    /* Enable USART1 IDLE interrupts   */
	__HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
#endif

	// HAL库默认开启以上中断
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, usart_rx_dma_buffer, ARRAY_LEN(usart_rx_dma_buffer));
}

/* Interrupt handlers begin */
/**
  * @brief Tx Transfer completed callback.
  * @param huart UART handle.
  * @retval None
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart){
	if(huart->Instance == USART1) {
		lwrb_skip(&usart_tx_rb, usart_tx_dma_current_len);/* Skip sent data, mark as read */
		usart_tx_dma_current_len = 0;           /* Clear length variable */
		usart_start_tx_dma_transfer();          /* Start sending more data */
	}
}

/**
  * @brief  Reception Event Callback (Rx event notification called after use of advanced reception service).
  * 		串口接收事件回调函数
  * @param  huart UART handle
  * @param  Size  Number of data available in application reception buffer (indicates a position in
  *               reception buffer until which, data are available)
  * @retval None
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	UNUSED(Size);
	if(huart->Instance == USART1) {
		/**
		 * HAL_UART_RECEPTION_TOIDLE 说明是DMA接收完成，或者半传输完成
		 * MaJerle 的实现需要用到DMA半传输中断和传输完成中断
		 * 虽然这个回调函数没有写DMA，但调用顺序是:
		 * HAL_UART_IRQHandler() --> UART_DMARxHalfCplt() --> HAL_UARTEx_RxEventCallback()
		 * HAL_UART_IRQHandler() --> UART_DMAReceiveCplt() --> HAL_UARTEx_RxEventCallback()
		 */
		if (huart->ReceptionType == HAL_UART_RECEPTION_TOIDLE)
		{
			usart_rx_check();                       /* Check for data to process */
		}

		/* HAL_UART_RECEPTION_STANDARD 说明是空闲中断触发的接收事件 */
		if (huart->ReceptionType == HAL_UART_RECEPTION_STANDARD)
		{
			usart_rx_check();                       /* Check for data to process */
		}
	}
}
/* Interrupt handlers end */

void setup(){
    usart_init();
}

void loop(){
	    uint8_t state, cmd, len;
	    /* Initialize ringbuff for TX & RX */
	    lwrb_init(&usart_tx_rb, usart_tx_rb_data, sizeof(usart_tx_rb_data));
	    lwrb_init(&usart_rx_rb, usart_rx_rb_data, sizeof(usart_rx_rb_data));

	    usart_send_string("USART DMA example: DMA HT & TC + USART IDLE LINE interrupts\r\n");
	    usart_send_string("Start sending data to STM32\r\n");

	    /* After this point, do not use usart_send_string function anymore */
	    /* Send packet data over UART from PC (or other STM32 device) */

	    /* Infinite loop */
	    state = 0;
	    while (1) {
	        uint8_t b;

	        /* Process RX ringbuffer */
	        /* Packet format: START_BYTE, CMD, LEN[, DATA[0], DATA[len - 1]], STOP BYTE */
	        /* DATA bytes are included only if LEN > 0 */
	        /* An example, send sequence of these bytes: 0x55, 0x01, 0x01, 0xFF, 0xAA */
	        /* Read byte by byte */


	        /* 处理 RX 环形缓冲区 */
	        /* 数据包格式：START_BYTE, CMD, LEN[, DATA[0], DATA[len - 1]], STOP BYTE */
	        /* 仅当 LEN > 0 时才包含数据字节 */
	        /* 例如，发送这些字节的序列：0x55、0x01、0x01、0xFF、0xAA */
	        /* 逐字节读取 */

	        if (lwrb_read(&usart_rx_rb, &b, 1) == 1) {
	            lwrb_write(&usart_tx_rb, &b, 1);   /* Write data to transmit buffer */
	            usart_start_tx_dma_transfer();
	            switch (state) {
	                case 0: {           /* Wait for start byte */
	                    if (b == 0x55) {
	                        ++state;
	                    }
	                    break;
	                }
	                case 1: {           /* Check packet command */
	                    cmd = b;
	                    ++state;
	                    break;
	                }
	                case 2: {           /* Packet data length */
	                    len = b;
	                    ++state;
	                    if (len == 0) {
	                        ++state;    /* Ignore data part if len = 0 */
	                    }
	                    break;
	                }
	                case 3: {           /* Data for command */
	                    --len;          /* Decrease for received character */
	                    if (len == 0) {
	                        ++state;
	                    }
	                    break;
	                }
	                case 4: {           /* End of packet */
	                    if (b == 0xAA) {
	                        /* Packet is valid */

	                        /* Send out response with CMD = 0xFF */
	                        b = 0x55;   /* Start byte */
	                        lwrb_write(&usart_tx_rb, &b, 1);
	                        cmd = 0xFF; /* Command = 0xFF = OK response */
	                        lwrb_write(&usart_tx_rb, &cmd, 1);
	                        b = 0x00;   /* Len = 0 */
	                        lwrb_write(&usart_tx_rb, &b, 1);
	                        b = 0xAA;   /* Stop byte */
	                        lwrb_write(&usart_tx_rb, &b, 1);

	                        /* Flush everything */
	                        usart_start_tx_dma_transfer();
	                    }
	                    state = 0;
	                    break;
	                }
	            }
	        }

	        /* Do other tasks ... */
	    }
}

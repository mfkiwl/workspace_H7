/**
  ******************************************************************************
  * @file        tile_wave.h
  * @author      OldGerman
  * @created on  Mar 20, 2023
  * @brief       
  ******************************************************************************
  * @attention
  *
  * Copyright (C) 2022 OldGerman.
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will beuleful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see https://www.gnu.org/licenses/.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef TILE_WAVE_H_
#define TILE_WAVE_H_

#include "common_inc.h"
#include <list>
#include <algorithm>
#include <functional>
#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
/* Exported define -----------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/

#ifdef __cplusplus

/**
 * 将 IDMA 需要访问的 4K FIFO 作为每个类的实例成员添加RAM_D1修饰，这样声明该对象就不会编译到 SRAM1
 * 缓冲区不使用动态内存，缓冲区的最大大小在编译时已写死，运行时只可通过变量限制最大可用的缓冲区，但缓冲区的总大小是不变的
 * 如果使用动态内存，RTOS下就涉及到malloc的线程安全问题，得参照 heap_useNewlib_ST.c 实现 malloc 锁，比较麻烦
 * 如果FIFO中接收/发送的数据量超过/低于这个阈值时，FIFO就会触发相应的接收/发送
 * total buf[........................................................................................]
 * 			 ^		  ^        ^
 *      list.FIFO[0]  FIFO[1]  FIFO[2]  ...
 * FIFO 阈值是 4K，但FIFO大小不能刚好是4K，需要留8K，开双缓冲区
 *
 * PyrLayer 只关心每次被调用时处理数据的变化量，不关心带有时间单位的变量，比如采样率。
 *          数据的变化量受采样率变化影响， "率" 是数据变化量与时间变化量的比值，所以
 *          采样率计算结果带有时间单位的变量
 *
 * 4K FIFO也是可变的
 * NAND Flash  擦除以block为最小操作单位，读/写以page为最小操作单位。出厂默认浮栅不带电荷，为1状态。擦除就是全写1
 */
/* 瓦片波形类*/
class TileWave
{
public:
	/* 配置结构体 */
	typedef struct {
		/* IO Size */
		uint32_t ulIOSize;
		uint32_t ulIOSizeMin;	// 2KB
	    uint32_t ulIOSizeMax;	// 64KB
	    /* Layer */
	    uint32_t ulLayerNum;
	    uint32_t ulLayerNumMax;
	    uint32_t ulLayerTilesNumMax;
	    /* WaveForm */
	    uint32_t ulWaveFrameSize;
	    uint32_t ulWaveDispWidth;
	    uint32_t ulWaveDispTileBufferSize;
	}Config_t;

	/* 暂存初始化层链表时输出的信息 */
	char ucStrBuffer[15][64];

	/* 层结构体 */
	typedef struct {
		uint32_t ulLayerNum;			// 层编号
		uint32_t ulTileSize;			// 瓦片大小，单位B
		uint32_t ulTileBufferSize;		// RAM：瓦片缓冲区大小，单位B
		uint32_t ulTileBufferOffset;	// 向瓦片缓冲区写入地址的偏移，每次写入瓦片数据，都向后偏移一个瓦片大小
		void *pucTileBuffer;			// 瓦片缓冲区地址
		uint32_t ulBufferSize;			// ROM：缓冲区大小，单位B
		uint32_t ulTileBufferTxPeriod;	// 缓冲区发送周期，单位，调度周期的倍数
	}Layer_t;

 	/* 初始化瓦片缓冲区链表 */
	uint32_t createTileBufferList()
	{
		ulWaveDispDataSize = ulWaveDispWidth * ulWaveFrameSize;
		/* 确定波形显示区的瓦片缓冲区大小有2种情况
		 * 存储所有波形的介质IOsize在 ulWaveDispTileBufferSizeMin ~ ulIOSizeMax 范围
		 * 1. RAM：IOsize越大，访问速度差不多
		 * 2. ROM：IOsize越大，访问速度越快
		 */
		/* 大于波形显示区数据大小的最小2的幂 */
		ulWaveDispTileBufferSizeMin = ulCalculateSmallestPowerOf2GreaterThan(ulWaveDispDataSize);

		/* ulWaveDispTileBufferSize ≥ ulWaveDispTileBufferSizeMin 且
		 * ulWaveDispTileBufferSize ≥ ulIOSizeMin
		 */
		if(ulWaveDispTileBufferSize < ulWaveDispTileBufferSizeMin) {
			ulWaveDispTileBufferSize = ulWaveDispTileBufferSizeMin;
		}
		if(ulWaveDispBufferSize < ulIOSizeMin) {
			ulWaveDispBufferSize = ulIOSizeMin;
		}

		/* 创建层并申请每层的动态内存 */
		uint32_t ulLayerNum;
		uint32_t ulLayerTileBufferSize = ulWaveDispBufferSize;	//4KB
		uint32_t ulLayerTileSize = ulLayerTileBufferSize / ulLayerTilesNumMax; // 2B = 4K / 2048
		void *pucLayerTileBuffer;
		for(ulLayerNum = 0; ulLayerNum < ulLayerNumMax; ulLayerNum++)
		{
			/* 申请层瓦片缓冲区的动态内存 */
			if(ulLayerTileSize < ulIOSizeMin)
				ulLayerTileBufferSize = ulIOSizeMin;
			else
				ulLayerTileBufferSize = ulLayerTileSize;

			pucLayerTileBuffer = malloc(ulLayerTileBufferSize);

			sprintf(&(ucStrBuffer[ulLayerNum][0]), "| %15ld | %13ld | %17ld |\r\n",
					DRAM_SRAM1.getMemUsed(), DRAM_SRAM1.getMemFree(), DRAM_SRAM1.getMemFreeMin());

			Layer_t xLayer = {
					.ulLayerNum = ulLayerNum,
					.ulTileSize = ulLayerTileSize,
					.ulTileBufferSize = ulLayerTileBufferSize,
					.ulTileBufferOffset = 0,
					.pucTileBuffer = pucLayerTileBuffer,
					.ulBufferSize = ulLayerTileSize * ulLayerTilesNumMax,
					.ulTileBufferTxPeriod = ulLayerTileBufferSize / ulLayerTileSize
			};
			/* 尾插入，链表正向遍历越往后层编号越大 */
			xLayersList.push_back(xLayer);

			ulLayersTileBufferSize += ulLayerTileBufferSize;

			/* 倍增大小 */
			ulLayerTileBufferSize = ulLayerTileBufferSize << 1;
			ulLayerTileSize = ulLayerTileSize << 1;
		}

		// malloc未实现申请32字节对齐的内存
//		ucpTxBuffer = malloc(ulIOSizeMax);
//		ucpRxBuffer = malloc(5*ulIOSizeMin); //先随便给5个最小IO缓冲区

		return 0U;
	}

	/* 重置向瓦片缓冲区写入地址的偏移 */
	void resetTileBufferOffset() {
		xRit = xLayersList.rbegin();
		for(uint8_t i = 0; i < ulLayerNumMax; i++) {
			(*xRit).ulTileBufferOffset = 0;
			++xRit;
		}
	}

	/* 切片瓦片缓冲区并写入 */
	uint32_t writeTileBuffer(uint8_t* pulData) {
		/* FATFS 返回值 */
		uint32_t ret = 0;

		/* 用于计算本函数被调用的实时频率的单次和平均值 */
		static double fRealWrittenFreq = 0;
		static uint32_t ulWrittenCount = 0;
		static uint32_t ulTickCountOld =  xTaskGetTickCount();
		uint32_t ulTickCount;

		/* 用于瓦片切片 */
		uint32_t ulPeriodMax = (*xLayersList.begin()).ulTileBufferTxPeriod;
		uint32_t ulTxBufferOffset = 0;
		char cWrittenMark = 'a';

		/* 瓦片切片 */
		++ulPeriod;	// = 1、2、3...2048;
		xRit = xLayersList.rbegin();
		for(uint8_t i = 0; i < ulLayerNumMax; i++) {
			/** 从帧缓冲区中复制瓦片大小的数据到瓦片缓冲区
			  * TODO: 从帧缓冲区计算2幂缩放倍率的瓦片大小数据
			  */
			memcpy((uint8_t*)((*xRit).pucTileBuffer) + (*xRit).ulTileBufferOffset, 	// 注意加上偏移地址
					pulData, (*xRit).ulTileSize);

			/* 更新写入瓦片缓冲区的偏移地址 */
			(*xRit).ulTileBufferOffset += (*xRit).ulTileSize;

			/** 若 计数器周期 整除 层瓦片缓冲区周期
			  * 说明该层需要向缓冲区发送瓦片缓冲区的所有数据
			  */
			if(ulPeriod % (*xRit).ulTileBufferTxPeriod == 0 ) // 从缓冲区最大的层迭代到最小的
			{
				/* 归零瓦片缓冲区的偏移地址 */
				(*xRit).ulTileBufferOffset = 0;

				// 将DRAM中的 非连续储存 的 瓦片缓冲区数据 复制 到 发送缓冲区 以变为连续储存的
				memcpy((uint8_t*)ucpTxBuffer + ulTxBufferOffset, (*xRit).pucTileBuffer, (*xRit).ulTileBufferSize);
				// 更新向缓冲区写入的起始地址偏移
				ulTxBufferOffset += (*xRit).ulTileBufferSize;

				++cWrittenMark;
			}
			++xRit;
		}

		/* 向层缓冲区写入数据 */
		if(ulSliceButNotWrite == 0) {
			ret = write(ulTxBufferOffsetOld, ulTxBufferOffset, (uint8_t *)ucpTxBuffer);
		}

		/* 打印本次切片详情 */
		if(ulPrintSliceDetail) {
			printf("| writeTileBuffer | ulPeriod = %4ld | ret = %2ld | addr = %10ld | size = %9ld | mark = %c | \r\n",
				ulPeriod, ret, ulTxBufferOffsetOld, ulTxBufferOffset, cWrittenMark);
		}

		/* 计算实时频率的单次和平均值 */
		++ulWrittenCount;
		ulTickCount = xTaskGetTickCount();	/* 获取当前的系统时间 */
		uint32_t ulTickOffest = ulTickCount - ulTickCountOld;
		ulTickCountOld = ulTickCount;
		fRealWrittenFreq = (double)1000 / ((double)ulTickOffest / ulWrittenCount);

		// 第一次 fRealWrittenFreq 肯定是 inf，需要舍弃
		// fRealWrittenFreq 是一个正常的浮点数才会计算
		if(isnormal(fRealWrittenFreq))	{
			++fRealWrittenFreqNum;
			fRealWrittenFreqSum += fRealWrittenFreq;
			fRealWrittenFreqAvg = fRealWrittenFreqSum / fRealWrittenFreqNum;
		}
		ulWrittenCount = 0;
		printf("freq: %3.3f, %3.3f\r\n", fRealWrittenFreq, fRealWrittenFreqAvg);

		/* 下次瓦片切片前需要处理的变量 */
		ulPeriod %= ulPeriodMax;
		ulTxBufferOffsetOld += ulTxBufferOffset;

		return ret;
	}

	/* 构造函数 */
	TileWave(Config_t &xConfig) {
		ulIOSize = xConfig.ulIOSize;
		ulIOSizeMin = xConfig.ulIOSizeMin;
		ulIOSizeMax = xConfig.ulIOSizeMax;
		ulLayerNum = xConfig.ulLayerNum;
		ulLayerNumMax = xConfig.ulLayerNumMax;
		ulLayerTilesNumMax = xConfig.ulLayerTilesNumMax;
	    ulWaveFrameSize = xConfig.ulWaveFrameSize;
	    ulWaveDispWidth = xConfig.ulWaveDispWidth;
		ulWaveDispTileBufferSize = xConfig.ulWaveDispTileBufferSize;

		ulLayersTileBufferSize = 0;
	}

	/* 获取动态内存API */
	static uint32_t init(
		    std::function<void*(size_t)> 	Malloc,
		    std::function<void(void*)>		Free)
	{
		malloc = Malloc;
		free = Free;

		return 0U;
	}

	/* 打执行 createTileBufferList() 时记录层信息 */
	void vPrintLayerInfo()
	{
		printf("| 层编号 | 瓦片大小 | 瓦片缓冲区大小 | 瓦片缓冲区地址 | 缓冲区大小 | 缓冲区发送周期 | DRAM 当前共使用 | DRAM 当前剩余 | DRAM 历史最少可用 |\r\n");
		printf("| ------ | -------- | -------------- | -------------- | ---------- | -------------- | --------------- | ------------- | ----------------- |\r\n");
		xIt = xLayersList.begin();
		for(uint8_t i = 0; i < ulLayerNumMax; i++) {
			printf("| %6ld | %8ld | %14ld | %p | %10ld | %14ld %s",
					(*xIt).ulLayerNum,
					(*xIt).ulTileSize,
					(*xIt).ulTileBufferSize,
					(*xIt).pucTileBuffer,
					(*xIt).ulBufferSize,
					(*xIt).ulTileBufferTxPeriod,
					&(ucStrBuffer[i][0]));
			++xIt;
		}
	}

	/* 测试动态内存API */
	static void vTestMallocFree()
	{
		void  *SRAM1_Addr0,  *SRAM1_Addr1, *SRAM1_Addr2;

		/* 从SRAM1域的SRAM申请200字节空间，使用指针变量SRAM1_Addr0操作这些空间时不要超过200字节 */
		printf("=========================================================\r\n");
		SRAM1_Addr0 = DRAM_SRAM1.malloc(200, 0);
		printf("SRAM1域SRAM总 = %ld字节，申请 = 0200字节，当前共使用 = %ld字节，当前剩余 = %ld字节，历史最少可用 = %ld字节\r\n",
				DRAM_SRAM1.getMemSize(), DRAM_SRAM1.getMemUsed(), DRAM_SRAM1.getMemFree(), DRAM_SRAM1.getMemFreeMin());

		/* 从SRAM1域的SRAM申请96字节空间，使用指针变量SRAM1_Addr1操作这些空间时不要超过96字节 */
		SRAM1_Addr1 = DRAM_SRAM1.malloc(96, 0);
		printf("SRAM1域SRAM总 = %ld字节，申请 = 0096字节，当前共使用 = %ld字节，当前剩余 = %ld字节，历史最少可用 = %ld字节\r\n",
				DRAM_SRAM1.getMemSize(), DRAM_SRAM1.getMemUsed(), DRAM_SRAM1.getMemFree(), DRAM_SRAM1.getMemFreeMin());

		/* 从SRAM1域的SRAM申请4111字节空间，使用指针变量SRAM1_Addr2操作这些空间时不要超过4111字节 */
		SRAM1_Addr2 = DRAM_SRAM1.malloc(4111, 0);
		printf("SRAM1域SRAM总 = %ld字节，申请 = 4111字节，当前共使用 = %ld字节，当前剩余 = %ld字节，历史最少可用 = %ld字节\r\n",
				DRAM_SRAM1.getMemSize(), DRAM_SRAM1.getMemUsed(), DRAM_SRAM1.getMemFree(), DRAM_SRAM1.getMemFreeMin());

		/* 释放从SRAM1域的SRAM申请的200字节空间 */
		DRAM_SRAM1.free(SRAM1_Addr0);
		printf("释放SRAM1域SRAM动态内存区申请的0200字节，当前共使用 = %ld字节，当前剩余 = %ld字节，历史最少可用 = %ld字节\r\n",
				DRAM_SRAM1.getMemUsed(), DRAM_SRAM1.getMemFree(), DRAM_SRAM1.getMemFreeMin());

		/* 释放从SRAM1域的SRAM申请的96字节空间 */
		DRAM_SRAM1.free(SRAM1_Addr1);
		printf("释放SRAM1域SRAM动态内存区申请的0096字节，当前共使用 = %ld字节，当前剩余 = %ld字节，历史最少可用 = %ld字节\r\n",
				DRAM_SRAM1.getMemUsed(), DRAM_SRAM1.getMemFree(), DRAM_SRAM1.getMemFreeMin());

		/* 释放从SRAM1域的SRAM申请的4111字节空间 */
		DRAM_SRAM1.free(SRAM1_Addr2);
		printf("释放SRAM1域SRAM动态内存区申请的4111字节，当前共使用 = %ld字节，当前剩余 = %ld字节，历史最少可用 = %ld字节\r\n",
				DRAM_SRAM1.getMemUsed(), DRAM_SRAM1.getMemFree(), DRAM_SRAM1.getMemFreeMin());
	}

	/** @brief  一次可以读取或写入的最小数据块，单位B
	  * @notice 不要与储存介质的最小 IO/SIZE 混淆
	  *         此参数应根据诸多参数综合设置
	  *         比如：
				| 波形帧大小 | 波形区水平像素数 | 波形区覆盖的数据大小 | 存储器     | 存储器最小 IO/SIZE | 实际操作储存器的 IO/SZIE                            |ulMinIOSize  |
				| ---------- | ---------------- | -------------------- | ---------- | ------------------ | --------------------------------------------------- | ------------ |
				| 32bit      | 400              | 1600B                | NAND FLASH | 512B               | 4KB（2K > 1600B 就够，但4K比较快，且能减少读写次数）| 4KB          |
				| 16bit      | 400              | 800B                 | SDRAM      | 1B                 | 1KB（1K > 800B 就够）                               | 1KB          |
				| 8bit       | 400              | 400B                 | PSRAM      | 1B                 | 512B（512B > 400B 就够）                            | 512B         |
	  */
	/* IO SZIE， 必须为 2 的幂 */
	uint32_t ulIOSize;							// 当前读写的IO SZIE
	uint32_t ulIOSizeMin;						// = 2048;  /* 单位 B */
	uint32_t ulIOSizeMax;						// = 16384; /* 单位 B */

	/* 层参数 */
    uint32_t ulLayerNum;						// 当前层编号
	uint32_t ulLayerNumMax;						// 最大层数(总层数)

	/* 层缓冲区在FLASH */
    uint32_t ulLayerBufferSizeMax;				// 层缓冲区大小的最大值，单位B
    uint32_t ulLayersBufferSize;    			// 层缓冲区的总大小

    /* 瓦片缓冲区在RAM */
    uint32_t ulLayerTileBufferSizeMax;			// 层瓦片缓冲区大小的最大值，单位B
    uint32_t ulLayersTileBufferSize;    		// 层瓦片缓冲区的总大小
    uint32_t ulLayerTilesNumMax;				// 每层瓦片的最大个数，该值决定能记录多少时间，必须为 2的幂，本工程为 64MB/32K=2048

    /* 波形参数 */
	uint32_t ulWaveDispBufferSize; 				// 缓冲区大小：波形显示
    uint32_t ulWaveFrameSize;  					// 波形帧大小，单位B
    uint32_t ulWaveDispWidth;					// 波形显示宽度，单位Pixel
    uint32_t ulWaveDispDataSize; 				// 波形显示区数据的大小，单位B
    uint32_t ulWaveDispTileBufferSize;  		// 波形显示区的瓦片缓冲区大小，单位B
    uint32_t ulWaveDispTileBufferSizeMin;		// 波形显示区的瓦片缓冲区最小大小，单位B

    /* 层链表及其迭代器 */
	std::list<Layer_t> xLayersList; 			// 层链表(双向)
	std::list<Layer_t>::iterator xIt;			// 层链表的正向迭代器
	std::list<Layer_t>::reverse_iterator xRit; 	// 层链表的反向迭代器

	/* 层缓冲区的读写缓冲区 */
	static void *ucpTxBuffer;					// 给64KB
	static void *ucpRxBuffer; 					// 暂时随便给5个2KB

	/* 层缓冲区读写API */
    static std::function<uint32_t (uint32_t addr, uint32_t size, uint8_t* pData)> 	write;
    static std::function<uint32_t (uint32_t addr, uint32_t size, uint8_t* pData)>	read;

    /* 以下变量在停止切片后在下次切片前需要归 0 */
	static uint32_t ulPeriod;	//周期计数器
	static uint32_t ulTxBufferOffsetOld;
	static double	fRealWrittenFreqSum; 		// 写入频率的总和
	static double 	fRealWrittenFreqAvg;		// 写入频率的平均值
	static double 	fRealWrittenFreqNum;		// 写入频率的个数

	/* 以下是在协议解析程序中可更改的标志 */
	static uint32_t ulPrintSliceDetail;			// 打印实时切片信息
	static uint32_t ulSliceButNotWrite;			// 实时切片时不写入数据

private:
	/* 动态内存API */
    static std::function<void*(size_t size)> 	malloc;
    static std::function<void(void* ptr)>		free;

    /* 计算大于某数的最小2的幂 */
    uint32_t ulCalculateSmallestPowerOf2GreaterThan(uint32_t ulValue) {
    	uint32_t ulNth = 1;
    	for(uint8_t i = 0; i < 32; i++)
    	{
    		ulNth = ulNth << i;
    		if(ulNth > ulValue)
    			break;
    	}
    	return ulNth;
    }

};

}
#endif

#endif /* TILE_WAVE_H_ */

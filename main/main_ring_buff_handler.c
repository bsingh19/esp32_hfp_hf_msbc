/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/xtensa_api.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_app_core.h"

#include "freertos/ringbuf.h"

#include "sd_card.h"

#define RINGBUF_HIGHEST_WATER_LEVEL (42 * 1024)
#define RINGBUF_PREFETCH_WATER_LEVEL (30 * 1024)

enum
{
	RINGBUFFER_MODE_PROCESSING,	 /* ringbuffer is buffering incoming audio data, I2S is working */
	RINGBUFFER_MODE_PREFETCHING, /* ringbuffer is buffering incoming audio data, I2S is waiting */
	RINGBUFFER_MODE_DROPPING	 /* ringbuffer is not buffering (dropping) incoming audio data, I2S is working */
};

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/
/* handler for I2S task */
static void bt_i2s_task_handler(void *arg);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static TaskHandle_t s_bt_i2s_task_handle = NULL; /* handle of I2S task */
static RingbufHandle_t s_ringbuf_i2s = NULL;	 /* handle of ringbuffer for I2S */
static SemaphoreHandle_t s_i2s_write_semaphore = NULL;
static uint16_t ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;

/*********************************
 * EXTERNAL FUNCTION DECLARATIONS
 ********************************/

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/
static void bt_i2s_task_handler(void *arg)
{
	uint8_t *data = NULL;
	size_t item_size = 0;
	/**
	 * The total length of DMA buffer of I2S is:
	 * `dma_frame_num * dma_desc_num * i2s_channel_num * i2s_data_bit_width / 8`.
	 * Transmit `dma_frame_num * dma_desc_num` bytes to DMA is trade-off.
	 */
	const size_t item_size_upto = 240 * 6;
	size_t bytes_written = 0;

	for (;;)
	{
		if (pdTRUE == xSemaphoreTake(s_i2s_write_semaphore, portMAX_DELAY))
		{
			for (;;)
			{
				item_size = 0;
				/* receive data from ringbuffer and write it to I2S DMA transmit buffer */
				data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, (TickType_t)pdMS_TO_TICKS(20), item_size_upto);
				if (item_size == 0)
				{
					ESP_LOGI(BT_APP_CORE_TAG, "ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING");
					ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
					break;
				}

				sd_card_write_data(data, item_size, &bytes_written);

				vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
			}
		}
	}
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/
void bt_i2s_task_start_up(void)
{
	ESP_LOGI(BT_APP_CORE_TAG, "ringbuffer data empty! mode changed: RINGBUFFER_MODE_PREFETCHING");
	ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
	if ((s_i2s_write_semaphore = xSemaphoreCreateBinary()) == NULL)
	{
		ESP_LOGE(BT_APP_CORE_TAG, "%s, Semaphore create failed", __func__);
		return;
	}
	if ((s_ringbuf_i2s = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL)
	{
		ESP_LOGE(BT_APP_CORE_TAG, "%s, ringbuffer create failed", __func__);
		return;
	}
	xTaskCreate(bt_i2s_task_handler, "BtI2STask", 6 * 2048, NULL, configMAX_PRIORITIES - 3, &s_bt_i2s_task_handle);
}

void bt_i2s_task_shut_down(void)
{
	if (s_bt_i2s_task_handle)
	{
		vTaskDelete(s_bt_i2s_task_handle);
		s_bt_i2s_task_handle = NULL;
	}
	if (s_ringbuf_i2s)
	{
		vRingbufferDelete(s_ringbuf_i2s);
		s_ringbuf_i2s = NULL;
	}
	if (s_i2s_write_semaphore)
	{
		vSemaphoreDelete(s_i2s_write_semaphore);
		s_i2s_write_semaphore = NULL;
	}
}

size_t write_ringbuf(const uint8_t *data, size_t size)
{
	size_t item_size = 0;
	BaseType_t done = pdFALSE;

	if (ringbuffer_mode == RINGBUFFER_MODE_DROPPING)
	{
		ESP_LOGW(BT_APP_CORE_TAG, "ringbuffer is full, drop this packet!");
		vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
		if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL)
		{
			ESP_LOGI(BT_APP_CORE_TAG, "ringbuffer data decreased! mode changed: RINGBUFFER_MODE_PROCESSING");
			ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
		}
		return 0;
	}

	done = xRingbufferSend(s_ringbuf_i2s, (void *)data, size, (TickType_t)0);

	if (!done)
	{
		ESP_LOGW(BT_APP_CORE_TAG, "ringbuffer overflowed, ready to decrease data! mode changed: RINGBUFFER_MODE_DROPPING");
		ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
	}

	if (ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING)
	{
		vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
		if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL)
		{
			ESP_LOGI(BT_APP_CORE_TAG, "ringbuffer data increased! mode changed: RINGBUFFER_MODE_PROCESSING");
			ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
			if (pdFALSE == xSemaphoreGive(s_i2s_write_semaphore))
			{
				ESP_LOGE(BT_APP_CORE_TAG, "semphore give failed");
			}
		}
	}

	return done ? size : 0;
}

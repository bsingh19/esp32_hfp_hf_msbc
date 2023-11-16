/*
 */
#ifndef MAIN_RING_BUFF_HANDLER_H_
#define MAIN_RING_BUFF_HANDLER_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

void bt_i2s_task_start_up(void);
void bt_i2s_task_shut_down(void);

size_t write_ringbuf(const uint8_t *data, size_t size);

#endif /* MAIN_RING_BUFF_HANDLER_H_ */

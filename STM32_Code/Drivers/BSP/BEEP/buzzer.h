#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

void Buzzer_Beep(uint8_t times, uint16_t on_ms, uint16_t off_ms);
void Buzzer_Init(void);

#endif

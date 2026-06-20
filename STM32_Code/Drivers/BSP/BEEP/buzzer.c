#include "./BSP/BEEP/buzzer.h"
#include "FreeRTOS.h"
#include "task.h"

#define BUZZER_PORT     GPIOB
#define BUZZER_PIN      GPIO_PIN_8

void Buzzer_Beep(uint8_t times, uint16_t on_ms, uint16_t off_ms)
{
    for (uint8_t i = 0; i < times; i++)
    {
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);

        if (i < times - 1)
        {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = BUZZER_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &gpio);

    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
}


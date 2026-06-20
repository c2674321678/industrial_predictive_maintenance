#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/DS18B20/ds18b20.h"
#include "./BSP/MPU6050/mpu6050.h"
#include "./BSP/BEEP/buzzer.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* =========================
 * ??????
 * ========================= */
void Task_Sensor(void *pv);
void Task_ESP(void *pv);
void Task_Display(void *pv);



/* =========================
 * ????
 * ========================= */
QueueHandle_t sensorQueue;
QueueHandle_t resultQueue;

/* =========================
 * ????
 * ========================= */
typedef struct
{
    int16_t temperature_x10;   /* 0.1°C */
    int16_t acc_x, acc_y, acc_z;
    int16_t gyro_x, gyro_y, gyro_z;
} SensorData_t;

typedef struct
{
    char status[32];
    float confidence;
    char action[128];
} AI_Result_t;

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);

    /* DWT ??,? delay_us ? */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    delay_init(72);
    usart_init(115200);
    led_init();
    lcd_init();
    Buzzer_Init();

    lcd_show_string(30, 10, 240, 16, 16, "AIoT FreeRTOS MQTT", RED);
    lcd_show_string(30, 30, 240, 16, 16, "Init Sensors...", BLUE);

    /* =========================
     * ??????
     * ========================= */

    /* DS18B20 */
    while (ds18b20_init())
    {
        lcd_show_string(30, 50, 240, 16, 16, "DS18B20 Error!", RED);
        delay_ms(200);
    }
    lcd_show_string(30, 50, 240, 16, 16, "DS18B20 OK    ", BLUE);

    /* MPU6050 */
    MPU6050_Init();
    if (MPU6050_GetID() == 0x68)
    {
        lcd_show_string(30, 70, 240, 16, 16, "MPU6050 OK    ", BLUE);
    }
    else
    {
        lcd_show_string(30, 70, 240, 16, 16, "MPU6050 Error ", RED);
    }

    /* =========================
     * ????
     * =========================
     * ???? 1,??????
     */
		sensorQueue = xQueueCreate(1, sizeof(SensorData_t));
		resultQueue = xQueueCreate(1, sizeof(AI_Result_t));

		if (sensorQueue == NULL || resultQueue == NULL)
		{
				lcd_show_string(30, 90, 240, 16, 16, "Queue Error", RED);
				while(1);
		}

		if (xTaskCreate(Task_Sensor,  "Sensor",  256, NULL, 3, NULL) != pdPASS)
		{
				lcd_show_string(30, 130, 240, 16, 16, "Sensor Err", RED);
				while(1);
		}

		if (xTaskCreate(Task_ESP,     "ESP",     1024, NULL, 4, NULL) != pdPASS)
		{
				lcd_show_string(30, 150, 240, 16, 16, "ESP Err", RED);
				while(1);
		}

		if (xTaskCreate(Task_Display, "Display", 256, NULL, 1, NULL) != pdPASS)
		{
				lcd_show_string(30, 170, 240, 16, 16, "Display Err", RED);
				while(1);
		}

		lcd_show_string(30, 110, 240, 16, 16, "Start Scheduler", BLUE);
	  vTaskStartScheduler();

		lcd_show_string(30, 190, 240, 16, 16, "Scheduler Fail", RED);
		while(1);

}

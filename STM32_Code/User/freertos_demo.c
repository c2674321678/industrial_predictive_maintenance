#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/DS18B20/ds18b20.h"
#include "./BSP/MPU6050/mpu6050.h"
#include "./BSP/ESP8266/esp8266.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/LED/led.h"
#include "./BSP/BEEP/buzzer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================
 * WiFi / MQTT ??
 * ========================================================= */
#define WIFI_SSID          "105"
#define WIFI_PWD           "12345678"

#define MQTT_BROKER_IP     "ahslpcl.iot.gz.baidubce.com"
#define MQTT_BROKER_PORT   1883

#define MQTT_CLIENT_ID     "TEST"
#define MQTT_USERNAME      "thingidp@ahslpcl|TEST|0|MD5"
#define MQTT_PASSWORD      "3d6f3f2db5ead8680792ff1e3cd5d940"

#define MQTT_TOPIC_DATA    "$iot/TEST/events"
#define MQTT_TOPIC_RESULT  "$iot/TEST/msg"

/* =========================================================
 * ????
 * ========================================================= */
static char g_mqtt_pub_buf[256] = {0};
volatile uint8_t g_esp_tx_busy = 0;

/* ?????? - ???? */
static char g_pub_status[32] = "0";
static char g_conn_status[32] = "0";

/* =========================================================
 * ????
 * ========================================================= */
extern QueueHandle_t sensorQueue;
extern QueueHandle_t resultQueue;
extern volatile uint8_t g_usart_rx_line_ready;
extern volatile uint8_t g_usart_rx_line[USART_REC_LEN];
extern char g_esp_dbg_line[128];

/* =========================================================
 * ????
 * ========================================================= */
typedef struct
{
    int16_t temperature_x10;
    int16_t acc_x, acc_y, acc_z;
    int16_t gyro_x, gyro_y, gyro_z;
} SensorData_t;

typedef struct
{
    char status[32];
    char data[32];
    char trend[32];
    int health_score;
    char action[64];
} AI_Result_t;

/* =========================================================
 * ????
 * ========================================================= */
static SensorData_t g_latestSensor = {0};
static AI_Result_t  g_latestResult  = {"--", "--", "--", 0, "--"};

/* =========================================================
 * ??????
 * ========================================================= */
void Task_Sensor(void *pv);
void Task_ESP(void *pv);
void Task_Display(void *pv);

/* =========================================================
 * JSON ????
 * ========================================================= */
static uint8_t ParseAIResultFromJSON(const char *rx, AI_Result_t *result)
{
    const char *json, *p, *q;
    char buf[16];
    
    if (!rx || !result) return 0;
    
    json = strchr(rx, '{');
    if (!json) return 0;
    
    memset(result, 0, sizeof(*result));
    
    /* ?? status */
    p = strstr(json, "\"status\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ' || *p == '\"') p++;
            q = p; while (*q && *q != '\"' && *q != ',' && *q != '}') q++;
            if (q > p) {
                strncpy(result->status, p, q - p);
                result->status[q - p] = '\0';
            }
        }
    }
    
    /* ?? data */
    p = strstr(json, "\"data\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ' || *p == '\"') p++;
            q = p; while (*q && *q != '\"' && *q != ',' && *q != '}') q++;
            if (q > p) {
                strncpy(result->data, p, q - p);
                result->data[q - p] = '\0';
            }
        }
    }
    
    /* ?? trend */
    p = strstr(json, "\"trend\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ' || *p == '\"') p++;
            q = p; while (*q && *q != '\"' && *q != ',' && *q != '}') q++;
            if (q > p) {
                strncpy(result->trend, p, q - p);
                result->trend[q - p] = '\0';
            }
        }
    }
    
    /* ?? health_score */
    p = strstr(json, "\"health_score\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ') p++;
            q = p; while (*q >= '0' && *q <= '9') q++;
            if (q > p) {
                memset(buf, 0, sizeof(buf));
                strncpy(buf, p, q - p);
                result->health_score = atoi(buf);
            }
        }
    }
    
    /* ?? action */
    p = strstr(json, "\"action\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ' || *p == '\"') p++;
            q = p; while (*q && *q != '\"' && *q != ',' && *q != '}') q++;
            if (q > p) {
                strncpy(result->action, p, q - p);
                result->action[q - p] = '\0';
            }
        }
    }
    
    return (result->status[0] != '\0');
}

/* =========================================================
 * ?? AI ????
 * ========================================================= */
static void AI_Result_ApplyAction(const AI_Result_t *result)
{
    if (!result) return;
    
    LED0(1);
    LED1(1);
    
    if (strcmp(result->status, "normal") == 0) {
        LED0(1);
        LED1(0);
    }
    else if (strstr(result->status, "warning") != NULL) {
        LED0(0);
        LED1(1);
        Buzzer_Beep(1, 80, 80);
    }
    else if (strstr(result->status, "critical") != NULL) {
        LED0(0);
        LED1(1);
        Buzzer_Beep(3, 100, 100);
    }
    else {
        LED0(1);
        LED1(1);
    }
}

/* =========================================================
 * Task 1: ?????
 * ========================================================= */
void Task_Sensor(void *pv)
{
    SensorData_t data;
    int16_t ax_raw, ay_raw, az_raw;
    int16_t gx, gy, gz;
    uint8_t temp_cnt = 0;
    TickType_t last_wake = xTaskGetTickCount();
    
    /* MPU6050 ???? */
    const float ACCEL_SCALE = 2048.0f;   // ±16g ???:2048 LSB/g
    const int16_t STATIC_AZ = 2170;      // ???????? AZ
    const int16_t STATIC_AXY = 0;        // ?? AX/AY
    
    data.temperature_x10 = 250;
    
    for (;;)
    {
        if (temp_cnt >= 10)
        {
            data.temperature_x10 = ds18b20_get_temperature();
            temp_cnt = 0;
        }
        
        /* ????? */
        MPU6050_GetData(&ax_raw, &ay_raw, &az_raw, &gx, &gy, &gz);
        
        /* ===== ??????(g),?? 100 ?????? ===== */
        // ?? = (??? - ????) / ???
        // ?? 100 ?:×100,??????
        data.acc_x = (int16_t)((ax_raw - STATIC_AXY) * 100.0f / ACCEL_SCALE);
        data.acc_y = (int16_t)((ay_raw - STATIC_AXY) * 100.0f / ACCEL_SCALE);
        data.acc_z = (int16_t)((az_raw - STATIC_AZ)  * 100.0f / ACCEL_SCALE);
        
        data.gyro_x = gx;
        data.gyro_y = gy;
        data.gyro_z = gz;
        
        taskENTER_CRITICAL();
        g_latestSensor = data;
        taskEXIT_CRITICAL();
        
        xQueueOverwrite(sensorQueue, &data);
        
        temp_cnt++;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}
/* =========================================================
 * Task 2: ESP8266 / MQTT
 * ========================================================= */
void Task_ESP(void *pv)
{
    SensorData_t data;
    AI_Result_t result;
    char rx_buf[USART_REC_LEN];
    
    uint8_t esp_ok = 0;
    uint8_t wifi_ok = 0;
    uint8_t mqtt_ok = 0;
    uint8_t mqtt_inited = 0;
    
    for (;;) {
        /* ?? MQTT ???? */
        if (g_usart_rx_line_ready && !g_esp_tx_busy) {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();
            
            strncpy(g_esp_dbg_line, rx_buf, sizeof(g_esp_dbg_line) - 1);
            
            if (strstr(rx_buf, "+MQTTSUBRECV") != NULL) {
                char *json = strchr(rx_buf, '{');
                if (json && ParseAIResultFromJSON(json, &result)) {
                    xQueueOverwrite(resultQueue, &result);
                    taskENTER_CRITICAL();
                    g_latestResult = result;
                    taskEXIT_CRITICAL();
                    AI_Result_ApplyAction(&result);
                }
            }
        }
        
        /* Step 1: ESP ??? */
        if (!esp_ok) {
            snprintf(g_conn_status, sizeof(g_conn_status), "STEP1: ESP AT...");
            if (ESP_Init() == ESP_OK) {
                esp_ok = 1;
                snprintf(g_conn_status, sizeof(g_conn_status), "STEP1 OK");
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                esp_ok = 0;
                wifi_ok = 0;
                mqtt_ok = 0;
                mqtt_inited = 0;
                snprintf(g_conn_status, sizeof(g_conn_status), "STEP1 ERR");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        
        /* Step 2: WiFi ?? */
        if (!wifi_ok) {
            snprintf(g_conn_status, sizeof(g_conn_status), "STEP2: WiFi...");
            if (ESP_ConnectWiFi(WIFI_SSID, WIFI_PWD) == ESP_OK) {
                wifi_ok = 1;
                snprintf(g_conn_status, sizeof(g_conn_status), "STEP2 OK");
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                wifi_ok = 0;
                mqtt_ok = 0;
                mqtt_inited = 0;
                snprintf(g_conn_status, sizeof(g_conn_status), "STEP2 ERR");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        
        /* Step 3: MQTT ????? */
        if (!mqtt_ok) {
            if (!mqtt_inited) {
                snprintf(g_conn_status, sizeof(g_conn_status), "STEP3: USERCFG...");
                if (ESP_MQTT_UserCfg(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD) != ESP_OK) {
                    mqtt_ok = 0;
                    mqtt_inited = 0;
                    snprintf(g_conn_status, sizeof(g_conn_status), "USERCFG ERR");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    continue;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                snprintf(g_conn_status, sizeof(g_conn_status), "STEP3: CONNCFG...");
                if (ESP_MQTT_ConnCfg() != ESP_OK) {
                    mqtt_ok = 0;
                    mqtt_inited = 0;
                    snprintf(g_conn_status, sizeof(g_conn_status), "CONNCFG ERR");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    continue;
                }
                mqtt_inited = 1;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            
            snprintf(g_conn_status, sizeof(g_conn_status), "STEP3: MQTT CONN...");
            if (ESP_MQTT_Connect(MQTT_BROKER_IP, MQTT_BROKER_PORT) != ESP_OK) {
                mqtt_ok = 0;
                mqtt_inited = 0;
                snprintf(g_conn_status, sizeof(g_conn_status), "MQTT CONN ERR");
                vTaskDelay(pdMS_TO_TICKS(8000));
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            snprintf(g_conn_status, sizeof(g_conn_status), "STEP3: SUB...");
            if (ESP_MQTT_Subscribe(MQTT_TOPIC_RESULT) != ESP_OK) {
                mqtt_ok = 0;
                mqtt_inited = 0;
                snprintf(g_conn_status, sizeof(g_conn_status), "SUB ERR");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            
            mqtt_ok = 1;
            snprintf(g_conn_status, sizeof(g_conn_status), "CONNECT SUCCESSFUL");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        /* Step 4: ??????? */
        /* Step 4: ??????? - ????? */
				if (xQueueReceive(sensorQueue, &data, pdMS_TO_TICKS(20)) == pdPASS) {
						int temp_int = data.temperature_x10 / 10;
						int temp_dec = abs(data.temperature_x10 % 10);
						uint8_t retry = 0;
						uint8_t pub_success = 0;
						
						snprintf(g_mqtt_pub_buf, sizeof(g_mqtt_pub_buf),
										"{\"temperature\":%d.%d,"
										"\"acc_x\":%d.%02d,\"acc_y\":%d.%02d,\"acc_z\":%d.%02d,"
										"\"gyro_x\":%d,\"gyro_y\":%d,\"gyro_z\":%d}",
										temp_int, temp_dec,
										data.acc_x / 100, abs(data.acc_x % 100),
										data.acc_y / 100, abs(data.acc_y % 100),
										data.acc_z / 100, abs(data.acc_z % 100),
										data.gyro_x, data.gyro_y, data.gyro_z);
						
						/* ???? 3 ? */
						for (retry = 0; retry < 3; retry++) {
								snprintf(g_pub_status, sizeof(g_pub_status), "JSON...");
								
								if (ESP_MQTT_PublishRaw(MQTT_TOPIC_DATA, g_mqtt_pub_buf) == ESP_OK) {
										snprintf(g_pub_status, sizeof(g_pub_status), "OK");
										pub_success = 1;
										break;
								} else {
										snprintf(g_pub_status, sizeof(g_pub_status), "RETRY %d/3", retry + 1);
										vTaskDelay(pdMS_TO_TICKS(100));  /* ????? */
								}
						}
						
						if (!pub_success) 
						{
								snprintf(g_pub_status, sizeof(g_pub_status), "ERR");
						}
						
						vTaskDelay(pdMS_TO_TICKS(500));  /* ?????? 500ms */
				}
		}
}
/* =========================================================
 * Task 3: LCD ??
 * ========================================================= */
void Task_Display(void *pv)
{
    SensorData_t s;
    AI_Result_t r;
    char buf[64];
    uint16_t status_color = GREEN;
    uint16_t pub_color = BLUE;   /* PUB ???? */
    uint16_t conn_color = BLUE;  /* CONN ???? */
    
    lcd_clear(WHITE);
    lcd_show_string(10, 10, 240, 16, 16, "AIoT FreeRTOS MQTT", RED);
    
    /* ????? */
    lcd_show_string(10, 35, 240, 16, 16, "Temp:", BLUE);
    lcd_show_string(10, 55, 240, 16, 16, "AccX:", BLUE);
    lcd_show_string(10, 75, 240, 16, 16, "AccY:", BLUE);
    lcd_show_string(10, 95, 240, 16, 16, "AccZ:", BLUE);
    
    /* AI ???? */
    lcd_show_string(10, 125, 240, 16, 16, "Status:", BLUE);
    lcd_show_string(10, 145, 240, 16, 16, "Data:", BLUE);
    lcd_show_string(10, 165, 240, 16, 16, "Trend:", BLUE);
    lcd_show_string(10, 185, 240, 16, 16, "Health:", BLUE);
    lcd_show_string(10, 205, 240, 16, 16, "Action:", BLUE);
    
    /* ?????(????) */
    lcd_show_string(10, 225, 240, 16, 16, "PUB:", BLUE);
    lcd_show_string(10, 245, 240, 16, 16, "CONN:", BLUE);
    
    for (;;) {
        taskENTER_CRITICAL();
        s = g_latestSensor;
        r = g_latestResult;
        taskEXIT_CRITICAL();
        
        /* ???????? */
        if (strstr(r.status, "critical") != NULL) {
            status_color = RED;
        } else if (strstr(r.status, "warning") != NULL) {
            status_color = YELLOW;
        } else {
            status_color = GREEN;
        }
        
        /* ?? PUB ???? */
        if (strstr(g_pub_status, "OK") != NULL) {
            pub_color = GREEN;
        } else if (strstr(g_pub_status, "ERR") != NULL || strstr(g_pub_status, "BAD") != NULL) {
            pub_color = RED;
        } else if (strstr(g_pub_status, "...") != NULL) {
            pub_color = BLUE;
        } else {
            pub_color = BLUE;
        }
        
        /* ?? CONN ???? */
        if (strstr(g_conn_status, "SUCCESSFUL") != NULL || strstr(g_conn_status, "OK") != NULL) {
            conn_color = GREEN;
        } else if (strstr(g_conn_status, "ERR") != NULL) {
            conn_color = RED;
        } else if (strstr(g_conn_status, "...") != NULL) {
            conn_color = BLUE;
        } else if (strstr(g_conn_status, "STEP") != NULL) {
            conn_color = BLUE;  /* ??? */
        } else {
            conn_color = BLUE;
        }
        
        /* ?? */
        lcd_fill(70, 35, 239, 50, WHITE);
        snprintf(buf, sizeof(buf), "%d.%d C", s.temperature_x10 / 10, abs(s.temperature_x10 % 10));
        lcd_show_string(70, 35, 240, 16, 16, buf, DARKBLUE);
        
				/* ?????:?? 100 ???? g ? */
				lcd_fill(70, 55, 239, 70, WHITE);
				snprintf(buf, sizeof(buf), "%d.%02d g", s.acc_x / 100, abs(s.acc_x % 100));
				lcd_show_string(70, 55, 240, 16, 16, buf, DARKBLUE);

				lcd_fill(70, 75, 239, 90, WHITE);
				snprintf(buf, sizeof(buf), "%d.%02d g", s.acc_y / 100, abs(s.acc_y % 100));
				lcd_show_string(70, 75, 240, 16, 16, buf, DARKBLUE);

				lcd_fill(70, 95, 239, 110, WHITE);
				snprintf(buf, sizeof(buf), "%d.%02d g", s.acc_z / 100, abs(s.acc_z % 100));
				lcd_show_string(70, 95, 240, 16, 16, buf, DARKBLUE);
        
        /* AI ?? */
        lcd_fill(70, 125, 239, 140, WHITE);
        lcd_show_string(70, 125, 240, 16, 16, r.status, status_color);
        
        lcd_fill(70, 145, 239, 160, WHITE);
        lcd_show_string(70, 145, 240, 16, 16, r.data, BLUE);
        
        lcd_fill(70, 165, 239, 180, WHITE);
        lcd_show_string(70, 165, 240, 16, 16, r.trend, MAGENTA);
        
        lcd_fill(70, 185, 239, 200, WHITE);
        snprintf(buf, sizeof(buf), "%d", r.health_score);
        lcd_show_string(70, 185, 240, 16, 16, buf, BROWN);
        
        lcd_fill(70, 205, 239, 220, WHITE);
        lcd_show_string(70, 205, 240, 16, 16, r.action, GREEN);
        
        /* ??? - ???????? */
        lcd_fill(70, 225, 239, 240, WHITE);
        lcd_show_string(70, 225, 240, 16, 16, g_pub_status, pub_color);
        
        lcd_fill(70, 245, 239, 260, WHITE);
        lcd_show_string(70, 245, 240, 16, 16, g_conn_status, conn_color);
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

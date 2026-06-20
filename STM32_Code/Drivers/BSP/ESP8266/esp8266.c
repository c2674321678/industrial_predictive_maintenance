#include "./BSP/ESP8266/esp8266.h"
#include "./SYSTEM/usart/usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* =========================================================
 * ??????
 * ========================================================= */
char g_esp_dbg_line[128] = {0};
uint8_t g_esp_dbg_flag = 0;
extern volatile uint8_t g_esp_tx_busy;

/* =========================================================
 * ????:??????????
 * ========================================================= */
static void ESP_SaveDbgLine(const char *line)
{
    taskENTER_CRITICAL();
    memset(g_esp_dbg_line, 0, sizeof(g_esp_dbg_line));
    strncpy(g_esp_dbg_line, line, sizeof(g_esp_dbg_line) - 1);
    g_esp_dbg_flag = 1;
    taskEXIT_CRITICAL();
}

/* =========================================================
 * ????:??????
 * ========================================================= */
/* =========================================================
 * ???: ESP_WaitAck
 * ??  : ??ESP8266??,???AT????
 * ========================================================= */
static uint8_t ESP_WaitAck(const char *ack, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    char rx_buf[USART_REC_LEN];

    while ((HAL_GetTick() - start) < timeout)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strlen(rx_buf) == 0)
                continue;

            /* ??:?????"AT"?????? */
            if (strncmp(rx_buf, "AT", 2) == 0)
                continue;

            if (strstr(rx_buf, "busy p") != NULL)
                continue;

            if (strstr(rx_buf, ack) != NULL)
                return ESP_OK;

            if (strstr(rx_buf, "FAIL") != NULL ||
                strstr(rx_buf, "ERROR") != NULL)
                return ESP_ERROR;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_SaveDbgLine("TIMEOUT");
    return ESP_TIMEOUT;
}


/* =========================================================
 * ??????
 * ========================================================= */
void ESP_ClearRxBuf(void)
{
    taskENTER_CRITICAL();
    g_usart_rx_sta = 0;
    g_usart_rx_line_ready = 0;
    memset((void *)g_usart_rx_buf, 0, USART_REC_LEN);
    memset((void *)g_usart_rx_line, 0, USART_REC_LEN);
    taskEXIT_CRITICAL();
}


uint8_t ESP_SendCmd(const char *cmd, const char *ack, uint32_t timeout)
{
    ESP_ClearRxBuf();

    if (cmd != NULL && strlen(cmd) > 0)
    {
        HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);
    }

    return ESP_WaitAck(ack, timeout);
}

/* =========================================================
 * ESP ???
 * ========================================================= */
uint8_t ESP_Init(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* ?? AT */
    if (ESP_SendCmd("AT\r\n", "OK", 5000) != ESP_OK)
        return ESP_ERROR;

    /* ?????? - ??3????? */
    for(int i = 0; i < 3; i++) {
        ESP_SendCmd("ATE0\r\n", "OK", 2000);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /* ???????? - ?? AT ??,????????? */
    ESP_SendCmd("AT\r\n", "OK", 2000);
    
    /* STA ?? */
    if (ESP_SendCmd("AT+CWMODE=1\r\n", "OK", 3000) != ESP_OK)
        return ESP_ERROR;

    /* ?? WiFi */
    ESP_SendCmd("AT+CWQAP\r\n", "OK", 3000);

    return ESP_OK;
}

/* =========================================================
 * ?? WiFi
 * ========================================================= */
uint8_t ESP_ConnectWiFi(const char *ssid, const char *pwd)
{
    char cmd[128];
    uint32_t start = HAL_GetTick();
    char rx_buf[USART_REC_LEN];

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);

    ESP_ClearRxBuf();
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);

    while ((HAL_GetTick() - start) < ESP_LINK_TIMEOUT)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strstr(rx_buf, "WIFI GOT IP") != NULL ||
                strstr(rx_buf, "WIFI CONNECTED") != NULL ||
                strstr(rx_buf, "OK") != NULL)
            {
                return ESP_OK;
            }

            if (strstr(rx_buf, "ERROR") != NULL ||
                strstr(rx_buf, "FAIL") != NULL)
            {
                continue;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_SaveDbgLine("WIFI TIMEOUT");
    return ESP_TIMEOUT;
}

/* =========================================================
 * MQTT ????
 * ========================================================= */
uint8_t ESP_MQTT_UserCfg(const char *client_id, const char *username, const char *password)
{
    char cmd[256];
    uint32_t start = HAL_GetTick();
    char rx_buf[USART_REC_LEN];

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"/\"\r\n",
             client_id, username, password);

    ESP_ClearRxBuf();
    vTaskDelay(pdMS_TO_TICKS(300));  
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);

    while ((HAL_GetTick() - start) < 15000)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strlen(rx_buf) == 0)
                continue;

            if (strstr(rx_buf, "busy p") != NULL)
                continue;

            if (strstr(rx_buf, "OK") != NULL)
                return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_SaveDbgLine("USERCFG TIMEOUT");
    return ESP_TIMEOUT;
}





/* =========================================================
 * MQTT ????
 * ========================================================= */
uint8_t ESP_MQTT_ConnCfg(void)
{
    char cmd[] = "AT+MQTTCONNCFG=0,120,0,\"\",\"\",0,0\r\n";

    ESP_ClearRxBuf();
    vTaskDelay(pdMS_TO_TICKS(200));

    if (ESP_SendCmd(cmd, "OK", 15000) != ESP_OK)
        return ESP_ERROR;

    return ESP_OK;
}




/* =========================================================
 * MQTT ?? Broker
 * ========================================================= */
uint8_t ESP_MQTT_Connect(const char *broker_ip, uint16_t port)
{
    char cmd[256];
    uint32_t start = HAL_GetTick();
    char rx_buf[USART_REC_LEN];

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONN=0,\"%s\",%d,0\r\n",
             broker_ip, port);

    ESP_ClearRxBuf();
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);

    while ((HAL_GetTick() - start) < 30000)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strlen(rx_buf) == 0)
                continue;

            if (strstr(rx_buf, "busy p") != NULL)
                continue;

            if (strstr(rx_buf, "+MQTTCONNECTED") != NULL ||
                strstr(rx_buf, "OK") != NULL)
            {
                return ESP_OK;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_SaveDbgLine("MQTTCONN TIMEOUT");
    return ESP_TIMEOUT;
}





/* =========================================================
 * MQTT ??
 * ========================================================= */
uint8_t ESP_MQTT_Subscribe(const char *topic)
{
    char cmd[256];
    uint32_t start = HAL_GetTick();
    char rx_buf[USART_REC_LEN];

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTSUB=0,\"%s\",0\r\n",
             topic);

    ESP_ClearRxBuf();
    vTaskDelay(pdMS_TO_TICKS(200));
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);

    while ((HAL_GetTick() - start) < 15000)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strlen(rx_buf) == 0)
                continue;

            if (strstr(rx_buf, "busy p") != NULL)
                continue;

            /* ???? OK ???? */
            if (strstr(rx_buf, "OK") != NULL ||
                strstr(rx_buf, "+MQTTSUB:OK") != NULL)
            {
                return ESP_OK;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_SaveDbgLine("SUB TIMEOUT");
    return ESP_TIMEOUT;
}


/* =========================================================
 * MQTT ?? JSON(RAW)
 * ========================================================= */
uint8_t ESP_MQTT_PublishRaw(const char *topic, const char *payload)
{
    char cmd[256];
    uint32_t start;
    char rx_buf[USART_REC_LEN];
    uint16_t len = strlen(payload);
    
    if (len == 0) return ESP_ERROR;
    
    g_esp_tx_busy = 1;

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTPUBRAW=0,\"%s\",%d,0,0\r\n",
             topic, len);

    ESP_ClearRxBuf();
    vTaskDelay(pdMS_TO_TICKS(200));

    /* ???? */
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);

    /* ??? '>' ???,???? payload */
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_ClearRxBuf();
    
    /* ??payload????? */
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)payload, len, 2000);
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)"\r\n", 2, 2000);

    /* ???????? */
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 8000)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strstr(rx_buf, "+MQTTPUB:OK") != NULL ||
                strstr(rx_buf, "SEND OK") != NULL ||
                strstr(rx_buf, "OK") != NULL)
            {
                g_esp_tx_busy = 0;
                return ESP_OK;
            }

            if (strstr(rx_buf, "ERROR") != NULL ||
                strstr(rx_buf, "FAIL") != NULL)
            {
                g_esp_tx_busy = 0;
                return ESP_ERROR;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_SaveDbgLine("PUBRAW TIMEOUT");
    g_esp_tx_busy = 0;
    return ESP_TIMEOUT;
}




uint8_t ESP_MQTT_Publish(const char *topic, const char *payload)
{
    char cmd[512];
    uint32_t start = HAL_GetTick();
    char rx_buf[USART_REC_LEN];

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n",
             topic, payload);

    ESP_ClearRxBuf();
    vTaskDelay(pdMS_TO_TICKS(200));
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);

    while ((HAL_GetTick() - start) < 10000)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strlen(rx_buf) == 0)
                continue;

            if (strstr(rx_buf, "busy p") != NULL)
                continue;

            if (strstr(rx_buf, "OK") != NULL ||
                strstr(rx_buf, "+MQTTPUB:OK") != NULL)
            {
                return ESP_OK;
            }

            if (strstr(rx_buf, "ERROR") != NULL ||
                strstr(rx_buf, "FAIL") != NULL)
            {
                return ESP_ERROR;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_SaveDbgLine("PUB TIMEOUT");
    return ESP_TIMEOUT;
}


/* =========================================================
 * ?? TCP ??
 * ========================================================= */
uint8_t ESP_ConnectServer(const char *ip, uint16_t port)
{
    char cmd[128];
    uint32_t start = HAL_GetTick();
    char rx_buf[USART_REC_LEN];

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ip, port);

    ESP_ClearRxBuf();
    HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)cmd, strlen(cmd), 2000);

    while ((HAL_GetTick() - start) < ESP_LINK_TIMEOUT)
    {
        if (g_usart_rx_line_ready)
        {
            taskENTER_CRITICAL();
            strcpy(rx_buf, (char *)g_usart_rx_line);
            g_usart_rx_line_ready = 0;
            taskEXIT_CRITICAL();

            ESP_SaveDbgLine(rx_buf);

            if (strstr(rx_buf, "CONNECT") != NULL)
            {
                return ESP_OK;
            }

            if (strstr(rx_buf, "ALREADY CONNECTED") != NULL)
            {
                return ESP_OK;
            }

            if (strstr(rx_buf, "ERROR") != NULL ||
                strstr(rx_buf, "FAIL") != NULL)
            {
                return ESP_ERROR;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_SaveDbgLine("TCP TIMEOUT");
    return ESP_TIMEOUT;
}

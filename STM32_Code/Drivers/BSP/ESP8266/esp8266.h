#ifndef __ESP8266_H
#define __ESP8266_H

#include <stdint.h>

#define ESP_OK      0
#define ESP_ERROR   1
#define ESP_TIMEOUT 2

#ifndef ESP_LINK_TIMEOUT
#define ESP_LINK_TIMEOUT 15000
#endif

extern char g_esp_dbg_line[128];
extern uint8_t g_esp_dbg_flag;
extern volatile uint8_t g_esp_tx_busy;

uint8_t ESP_Init(void);
uint8_t ESP_ConnectWiFi(const char *ssid, const char *pwd);

uint8_t ESP_MQTT_UserCfg(const char *client_id, const char *username, const char *password);
uint8_t ESP_MQTT_ConnCfg(void);
uint8_t ESP_MQTT_Connect(const char *broker_ip, uint16_t port);
uint8_t ESP_MQTT_Subscribe(const char *topic);
uint8_t ESP_MQTT_PublishRaw(const char *topic, const char *payload);
uint8_t ESP_MQTT_Publish(const char *topic, const char *payload);

uint8_t ESP_ConnectServer(const char *ip, uint16_t port);

void ESP_ClearRxBuf(void);
uint8_t ESP_SendCmd(const char *cmd, const char *ack, uint32_t timeout);

#endif

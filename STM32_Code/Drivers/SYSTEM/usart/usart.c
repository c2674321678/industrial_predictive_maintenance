#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include <string.h>

/* =========================
 * printf ???
 * ========================= */
#if 1

#if (__ARMCC_VERSION >= 6010050)
__asm(".global __use_no_semihosting\n\t");
__asm(".global __ARM_use_no_argv \n\t");
#else
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};
#endif

int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

FILE __stdout;

int fputc(int ch, FILE *f)
{
    while ((USART_UX->SR & 0X40) == 0);
    USART_UX->DR = (uint8_t)ch;
    return ch;
}
#endif

/* =========================
 * ????
 * ========================= */
#if USART_EN_RX

volatile uint8_t g_usart_rx_buf[USART_REC_LEN];
volatile uint16_t g_usart_rx_sta = 0;
volatile uint8_t g_rx_buffer[RXBUFFERSIZE];

/* ????? */
volatile uint8_t g_usart_rx_line[USART_REC_LEN];
volatile uint8_t g_usart_rx_line_ready = 0;

UART_HandleTypeDef g_uart1_handle;

/**
 * @brief       ?????
 */
void usart_init(uint32_t baudrate)
{
    g_uart1_handle.Instance = USART_UX;
    g_uart1_handle.Init.BaudRate = baudrate;
    g_uart1_handle.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart1_handle.Init.StopBits = UART_STOPBITS_1;
    g_uart1_handle.Init.Parity = UART_PARITY_NONE;
    g_uart1_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart1_handle.Init.Mode = UART_MODE_TX_RX;
    HAL_UART_Init(&g_uart1_handle);

    HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
}

/**
 * @brief       UART?????
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init_struct;

    if (huart->Instance == USART_UX)
    {
        USART_TX_GPIO_CLK_ENABLE();
        USART_RX_GPIO_CLK_ENABLE();
        USART_UX_CLK_ENABLE();

        gpio_init_struct.Pin = USART_TX_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(USART_TX_GPIO_PORT, &gpio_init_struct);

        gpio_init_struct.Pin = USART_RX_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_AF_INPUT;
        HAL_GPIO_Init(USART_RX_GPIO_PORT, &gpio_init_struct);

#if USART_EN_RX
        HAL_NVIC_SetPriority(USART_UX_IRQn, 3, 3);
        HAL_NVIC_EnableIRQ(USART_UX_IRQn);
#endif
    }
}

/**
 * @brief       ??????????
 *
 * ??:
 *  - ? CRLF ??????
 *  - ?????,????? g_usart_rx_line
 *  - ?????,???????
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART_UX)
    {
        uint8_t ch = g_rx_buffer[0];

        if (ch == '\r')
        {
            g_usart_rx_sta |= 0x4000;   /* ?? CR */
        }
        else if (ch == '\n')
        {
            if (g_usart_rx_sta & 0x4000) 
            {
                /* ???? */
                uint16_t len = g_usart_rx_sta & 0x3FFF;
                if (len >= USART_REC_LEN)
                    len = USART_REC_LEN - 1;

                g_usart_rx_buf[len] = '\0';

                /* ????????,?????? */
                memcpy((void *)g_usart_rx_line, (const void *)g_usart_rx_buf, len + 1);
                g_usart_rx_line_ready = 1;

                /* ??????,????? */
                g_usart_rx_sta = 0;
                memset((void *)g_usart_rx_buf, 0, USART_REC_LEN);
            }
            else
            {
                /* ??? \n,?? */
                g_usart_rx_sta = 0;
            }
        }
        else
        {
            /* ???? */
            if ((g_usart_rx_sta & 0x8000) == 0)
            {
                uint16_t idx = g_usart_rx_sta & 0x3FFF;

                if (idx < (USART_REC_LEN - 1))
                {
                    g_usart_rx_buf[idx] = ch;
                    g_usart_rx_sta++;
                }
                else
                {
                    /* ??,???? */
                    g_usart_rx_sta = 0;
                    memset((void *)g_usart_rx_buf, 0, USART_REC_LEN);
                }
            }
        }

        HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
    }
}

/**
 * @brief       USART1??????
 */
void USART_UX_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_uart1_handle);
}

#endif

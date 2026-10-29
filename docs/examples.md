# Usage Examples & Integration {#examples_page}

This guide demonstrates how to integrate **libsimcom** into a microcontroller project (such as STM32, ESP32, NXP LPC/Kinetis, or Nordic nRF) using a cooperative superloop and UART interrupts.

---

## 1. Hardware Abstraction Layer (HAL) Binding

`libsimcom` is completely hardware-agnostic. The application provides two hardware hooks:
- `uart_tx`: Function to transmit bytes over UART.
- `get_ms`: Function returning a millisecond timestamp (e.g., `HAL_GetTick()` or `SysTick`).

```c
#include "simcom.h"
#include "stm32f4xx_hal.h"  /* Or your target MCU HAL */

extern UART_HandleTypeDef huart2;

/* Transmit bytes to the modem over UART */
static simcom_status_t hal_uart_tx(const uint8_t *data, size_t len)
{
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, 100);
    return (status == HAL_OK) ? SIMCOM_STATUS_OK : SIMCOM_STATUS_ERROR;
}

/* Return system uptime in milliseconds */
static uint32_t hal_get_ms(void)
{
    return HAL_GetTick();
}
```

---

## 2. UART Interrupt Service Routine (ISR) Hook

Feed incoming bytes directly to `libsimcom` inside your UART RX interrupt. `Simcom_FeedBytes()` is non-blocking and lock-free:

```c
static simcom_handle_t g_modem;

void USART2_IRQHandler(void)
{
    /* Check for Received Data Ready flag */
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE) != RESET) {
        uint8_t byte = (uint8_t)(huart2.Instance->DR & 0xFF);
        
        /* Fast ISR-safe byte ingestion */
        Simcom_FeedBytes(&g_modem, &byte, 1, NULL);
    }
    
    HAL_UART_IRQHandler(&huart2);
}
```

---

## 3. Modem Initialization & Asynchronous Callbacks

Initialize the driver handle, register callbacks for events and asynchronous URCs, and trigger `Simcom_StartInit()`:

```c
#include <stdio.h>

/* Callback triggered when asynchronous workflows complete */
static void on_workflow_complete(simcom_handle_t *me, simcom_workflow_t wf, simcom_status_t status, void *user_ctx)
{
    (void)me;
    (void)user_ctx;
    
    if (wf == SIMCOM_WORKFLOW_INIT) {
        if (status == SIMCOM_STATUS_OK) {
            printf("[APP] Modem initialized successfully!\n");
        } else {
            printf("[APP] Modem init failed with code %d\n", (int)status);
        }
    }
}

/* Callback triggered when the modem pushes an unsolicited result code (URC) */
static void on_urc_received(simcom_handle_t *me, const simcom_urc_event_t *event, void *user_ctx)
{
    (void)me;
    (void)user_ctx;
    
    switch (event->type) {
    case SIMCOM_URC_TYPE_CREG:
    case SIMCOM_URC_TYPE_CGREG:
    case SIMCOM_URC_TYPE_CEREG:
        printf("[URC] Network registration changed: state=%d\n", event->data.reg.state);
        break;
        
    case SIMCOM_URC_TYPE_CSQ:
        printf("[URC] Signal quality: RSSI=%u, BER=%u\n", 
               event->data.csq.rssi, event->data.csq.ber);
        break;
        
    default:
        break;
    }
}

void App_Init(void)
{
    simcom_init_t init_cfg = {
        .hal = {
            .uart_tx = hal_uart_tx,
            .get_ms  = hal_get_ms,
        },
        .profile = &g_simcom_profile_a76xx,  /* Predefined A76XX profile */
        .callbacks = {
            .workflow_cb = on_workflow_complete,
            .urc_cb      = on_urc_received,
        }
    };

    simcom_status_t status = Simcom_Init(&g_modem, &init_cfg);
    if (status != SIMCOM_STATUS_OK) {
        printf("Simcom_Init failed: %d\n", (int)status);
        return;
    }

    /* Start asynchronous handshake & initialization */
    Simcom_StartInit(&g_modem);
}
```

---

## 4. Main Superloop Execution

Step the state machine continuously inside your main loop:

```c
int main(void)
{
    /* Initialize MCU hardware, clocks, and peripherals */
    HAL_Init();
    SystemClock_Config();
    MX_USART2_UART_Init();

    /* Initialize libsimcom */
    App_Init();

    while (1) {
        /* Run libsimcom non-blocking parser and FSM */
        Simcom_Poll(&g_modem);

        /* Application task logic */
        App_ProcessTasks();
    }
}
```

---

## 5. SIM Status and PIN Unlocking

If the SIM card requires a PIN, you can check its status and submit the PIN asynchronously:

```c
static void check_and_unlock_sim(void)
{
    simcom_sim_state_t sim_state;
    if (Simcom_GetCachedSimState(&g_modem, &sim_state) == SIMCOM_STATUS_OK) {
        if (sim_state == SIMCOM_SIM_PIN_REQUIRED) {
            printf("[APP] SIM PIN required. Submitting PIN...\n");
            Simcom_SubmitPin(&g_modem, "1234");
        } else if (sim_state == SIMCOM_SIM_READY) {
            printf("[APP] SIM is unlocked and ready.\n");
        }
    }
}
```

---

## 6. Querying Device Information

Once the modem reaches `SIMCOM_STATE_READY`, query device identifiers (IMEI, Model, Firmware version):

```c
static void query_device_info(void)
{
    simcom_dev_info_t info;
    if (Simcom_GetCachedDevInfo(&g_modem, &info) == SIMCOM_STATUS_OK) {
        printf("Manufacturer: %s\n", info.manufacturer);
        printf("Model:        %s\n", info.model);
        printf("Revision:     %s\n", info.revision);
        printf("IMEI:         %s\n", info.imei);
    }
}
```

/**
 * @file io_manager.c
 * @brief I/O Manager module implementing button debouncing, ADC mapping, and UART status logging.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-13
 */

#include "io_manager.h"
#include "bsp_io.h"
#include "bsp_adc.h"
#include "can_manager.h"
#include "can_msg_types.h"
#include "tasks_config.h"
#include "safe_printf.h"
#include "main.h"

#include "task.h"

#include <stdint.h>
#include <string.h>

/** @brief Logging tag for the I/O Manager module. */
#define TAG "IO_MGR"

/** @brief Button and I/O sampling period in milliseconds. */
#define IO_SAMPLE_PERIOD_MS     10U

/** @brief Number of consecutive stable samples required for button debouncing (4 * 10 ms = 40 ms). */
#define IO_DEBOUNCE_SAMPLES     4U

/** @brief Periodic UART status print interval in milliseconds. */
#define IO_PRINT_PERIOD_MS      (50U * IO_SAMPLE_PERIOD_MS) // 500ms

/** @brief Periodic CAN BUS send message interval in milliseconds. */
#define IO_CAN_MSG_PERIOD_MS    (10U * IO_SAMPLE_PERIOD_MS) // 100ms

/** @brief Can ID*/
#define IO_CAN_ID (0x01F2F3F4)

/** @brief FreeRTOS task handle for the io_manager task. */
static TaskHandle_t io_manager_task_handle = NULL;

/** @brief Can message*/
static CAN_msg_t can_msg = {0};


/**
 * @brief Debouncer state structure for 8 parallel button inputs.
 */
typedef struct
{
    uint8_t history[IO_DEBOUNCE_SAMPLES]; /**< Circular buffer of recent raw button samples. */
    uint8_t index;                        /**< Current write index in the history buffer. */
    uint8_t stable_state;                 /**< Current debounced 8-bit button state. */
} io_debounce_t;

/**
 * @brief Initializes the button debouncer structure with a given initial state.
 * @param[out] db Pointer to the debouncer structure.
 * @param[in] init_raw Initial raw button state read from hardware.
 */
static void io_debounce_init(io_debounce_t *db, uint8_t init_raw)
{
    for (uint8_t i = 0U; i < IO_DEBOUNCE_SAMPLES; i++)
    {
        db->history[i] = init_raw;
    }
    db->index = 0U;
    db->stable_state = init_raw;
}

/**
 * @brief Updates the parallel 8-bit button debouncer with a new raw sample.
 *        A bit transitions to 1 only if it is 1 across all history samples,
 *        and transitions to 0 only if it is 0 across all history samples.
 * @param[in,out] db Pointer to the debouncer structure.
 * @param[in] raw_sample New raw 8-bit button state.
 * @return Updated debounced 8-bit button state.
 */
static uint8_t io_debounce_update(io_debounce_t *db, uint8_t raw_sample)
{
    db->history[db->index] = raw_sample;
    db->index = (uint8_t)((db->index + 1U) % IO_DEBOUNCE_SAMPLES);

    uint8_t all_ones = 0xFFU;
    uint8_t any_ones = 0x00U;

    for (uint8_t i = 0U; i < IO_DEBOUNCE_SAMPLES; i++)
    {
        all_ones &= db->history[i];
        any_ones |= db->history[i];
    }

    uint8_t all_zeros = (uint8_t)(~any_ones);

    /* Set bits that have been stably HIGH, clear bits that have been stably LOW */
    db->stable_state = (uint8_t)((db->stable_state & (~all_zeros)) | all_ones);

    return db->stable_state;
}

/**
 * @brief Reads ADC value from hardware and maps it linearly to 0..100 range.
 * @return Mapped value in range 0..100.
 */
static uint8_t io_manager_adc_read_mapped(void)
{
    uint32_t raw = bsp_adc_read_raw();
    uint32_t mapped = (raw * 100U + 2047U) / 4095U;
    if (mapped > 100U)
    {
        mapped = 100U;
    }
    return mapped;
}


static CAN_msg_t *io_manager_canCreateFrame(uint8_t btnState, uint8_t adcState)
{
    static uint32_t canCnt = 0;

    memset(&can_msg, 0, sizeof(CAN_msg_t));

    can_msg.format = CAN_MSG_FRAME_FORMAT_CAN;
    can_msg.ident = CAN_msg_set_extended_id(IO_CAN_ID); // extended ID
    can_msg.len = 8;

    can_msg.data[0] = btnState;
    can_msg.data[1] = adcState;
    // can_msg.data[2] = 0;
    // can_msg.data[3] = 0;    
    can_msg.data[4] = (uint8_t)(canCnt >> 24);
    can_msg.data[5] = (uint8_t)(canCnt >> 16);
    can_msg.data[6] = (uint8_t)(canCnt >> 8);
    can_msg.data[7] = (uint8_t)(canCnt & 0xFF);
    
    canCnt ++;

    return &can_msg;
}
/**
 * @brief FreeRTOS task function for periodic I/O sampling, button debouncing, and UART logging.
 * @param[in] pvParameters Unused task parameter.
 */
static void io_manager_task(void *pvParameters)
{
    (void)pvParameters;

    io_debounce_t btn_db;
    uint8_t initial_btn = bsp_io_buttons_read();
    io_debounce_init(&btn_db, initial_btn);

    uint8_t prev_btn_state = initial_btn;
    uint32_t elapsed_ms = 0U;
    uint32_t elapsed_can_ms = 0U;

    for (;;)
    {
        /* Sample and debounce 8 button inputs */
        uint8_t raw_btn = bsp_io_buttons_read();
        uint8_t btn = io_debounce_update(&btn_db, raw_btn);

        elapsed_ms += IO_SAMPLE_PERIOD_MS;
        elapsed_can_ms += IO_SAMPLE_PERIOD_MS;

        /* send data to can bus */
        if(elapsed_can_ms >= IO_CAN_MSG_PERIOD_MS)
        {
            elapsed_can_ms = 0U;

            if (!can_manager_send(&can1_inst, io_manager_canCreateFrame(btn, io_manager_adc_read_mapped()) ) )
            {
                safe_printf(TAG, "Failed to enqueue CAN message! Queue full?\r\n");
            }
        }

        /* Print status immediately on debounced button change or periodically */
        if ((btn != prev_btn_state) || (elapsed_ms >= IO_PRINT_PERIOD_MS))
        {
            bool btn_changed = (btn != prev_btn_state);
            prev_btn_state = btn;
            elapsed_ms = 0U;

            bool out_state = bsp_io_output_get_state();
            uint32_t adc_raw = bsp_adc_read_raw();
            uint8_t adc_mapped = io_manager_adc_read_mapped();

            

            safe_printf(TAG,
                        "BTN: 0x%02X [SW7..0: %d%d%d%d %d%d%d%d]%s | PA8: %-4s | ADC: %4lu (%3lu%%)\r\n",
                        btn,
                        (btn >> 7) & 1U, (btn >> 6) & 1U,
                        (btn >> 5) & 1U, (btn >> 4) & 1U,
                        (btn >> 3) & 1U, (btn >> 2) & 1U,
                        (btn >> 1) & 1U, (btn & 1U),
                        btn_changed ? " *" : "  ",
                        out_state ? "HIGH" : "LOW",
                        (unsigned long)adc_raw,
                        (unsigned long)adc_mapped);
        }

        vTaskDelay(pdMS_TO_TICKS(IO_SAMPLE_PERIOD_MS));
    }
}

/**
 * @brief Initializes the I/O Manager hardware (GPIO and ADC) and creates the io_manager task.
 * @return true if initialization and task creation succeeded, false otherwise.
 */
bool io_manager_init(void)
{
    if (false == bsp_io_init())
    {
        return false;
    }

    if (false == bsp_adc_init())
    {
        return false;
    }

    BaseType_t ret = xTaskCreate(io_manager_task,
                                 "io_manager",
                                 TASK_STACK_IO_MANAGER,
                                 NULL,
                                 TASK_PRIO_IO_MANAGER,
                                 &io_manager_task_handle);
    if (ret != pdPASS)
    {
        return false;
    }

    return true;
}

/**
 * @file io_manager.c
 * @brief I/O Manager module implementing button debouncing, ADC mapping, and UART status logging.
 *         ACC stacyjka    - SW0 -> bit 0 -> PB4  (CN10 pin 27)
 *         Drzwi INNE      - SW1 -> bit 1 -> PC8  (CN10 pin 31)
 *         Drzwi KIER      - SW2 -> bit 2 -> PC7  (CN10 pin 19)
 *         Światła         - SW3 -> bit 3 -> PB7  (CN10 pin 5)
 *         Zamki ZAMKNIĘTE - SW4 -> bit 4 -> PB12 (CN10 pin 16)
 *         Awaryjne        - SW5 -> bit 5 -> PB13 (CN10 pin 30)
 *         Maska           - SW6 -> bit 6 -> PB14 (CN10 pin 28)
 *         Hamulec RĘCZNY  - SW7 -> bit 7 -> PB15 (CN10 pin 26)
 *         Obroty silnika  - SW8 -> bit 8 -> PB2  (CN10 pin 22)
 *
 *        Input source (physical IO or UART simulation): io_sim.c, UART commands: io_cmd.c.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-14
 */

#include "io_manager.h"
#include "io_sim.h"
#include "bsp_io.h"
#include "bsp_adc.h"
#include "can_manager.h"
#include "can_msg_types.h"
#include "tasks_config.h"
#include "safe_printf.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#include <stdint.h>
#include <string.h>

/** @brief Logging tag for the I/O Manager module. */
#define TAG "IO_MGR"

/** @brief Duration of preparatory pulse in milliseconds before the active edge transition. */
#define RELAY_PULSE_DURATION_MS 40U

/** @brief Maximum timeout in milliseconds to wait for debounced relay feedback. */
#define RELAY_FB_TIMEOUT_MS     200U

/** @brief Maximum number of retry attempts to switch the relay per command. */
#define RELAY_MAX_ATTEMPTS      2U

/** @brief Mutex protecting concurrent relay switching operations. */
static SemaphoreHandle_t s_relay_mutex = NULL;

/** @brief Queue for incoming asynchronous relay commands. */
static QueueHandle_t s_relay_cmd_queue = NULL;

/** @brief FreeRTOS task handle for the background relay worker task. */
static TaskHandle_t s_relay_worker_task_handle = NULL;

/** @brief Last commanded target state (0xFF = uninitialized, 0 = open, 1 = closed). */
static uint8_t s_relay_last_target = 0xFFU;

/** @brief Flag indicating if the last relay actuation attempt failed. */
static bool s_relay_last_attempt_failed = false;

/** @brief Flag indicating if the relay state has been successfully confirmed at least once. */
static bool s_relay_confirmed = false;

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

static uint16_t btn = 0;

/** @brief Physical fuel level 0..100 % (potentiometer), refreshed by the io_manager task. */
static volatile uint8_t s_fuel_phys = 0U;

/**
 * @brief Debouncer state structure for 9 parallel button inputs.
 */
typedef struct
{
    uint16_t history[IO_DEBOUNCE_SAMPLES]; /**< Circular buffer of recent raw button samples. */
    uint8_t index;                         /**< Current write index in the history buffer. */
    uint16_t stable_state;                 /**< Current debounced 16-bit button state. */
} io_debounce_t;

// getters
uint16_t io_manager_getAllBtnState(void)
{
    return btn;
}

void io_manager_getPhysInputs(uint16_t *btn_state, uint8_t *fuel)
{
    *btn_state = btn;
    *fuel = s_fuel_phys;
}

uint8_t io_manager_getRelayState(void)
{
    return (uint8_t)((btn & (1U << 4U)) != 0U);
}

/**
 * @brief Polls the debounced relay state until it matches expected state or timeout expires.
 * @param[in] expected_state Target state (0 or 1).
 * @param[in] timeout_ms Maximum time to wait in milliseconds.
 * @return true if feedback matches expected state within timeout, false otherwise.
 */
static bool io_manager_wait_for_relay_feedback(uint8_t expected_state, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0U;
    const uint32_t step_ms = 10U;

    while (elapsed_ms < timeout_ms)
    {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;

        if (io_manager_getRelayState() == expected_state)
        {
            return true;
        }
    }

    return (io_manager_getRelayState() == expected_state);
}

bool io_manager_setRelayState(uint8_t target_state)
{
    target_state = (target_state != 0U) ? 1U : 0U;

    if (s_relay_mutex != NULL)
    {
        (void)xSemaphoreTake(s_relay_mutex, portMAX_DELAY);
    }

    uint8_t current_fb = io_manager_getRelayState();

    /* If the relay is already physically in the target state, do not cycle it!
     * Toggling the pin to the opposite state when already in the desired state
     * causes an unnecessary edge that momentarily turns the relay ON then OFF (or OFF then ON). */
    if (current_fb == target_state)
    {
        /* Ensure PA8 matches desired level without generating opposing edges */
        bsp_io_output_setPin(target_state != 0U);
        s_relay_last_target = target_state;
        s_relay_confirmed = true;
        s_relay_last_attempt_failed = false;

        if (s_relay_mutex != NULL)
        {
            (void)xSemaphoreGive(s_relay_mutex);
        }
        return true;
    }

    s_relay_last_target = target_state;
    bool success = false;

    for (uint8_t attempt = 1U; attempt <= RELAY_MAX_ATTEMPTS; attempt++)
    {
        if (target_state == 1U)
        {
            /* ZAMKNIJ (1 / ON):
             * Edge: LOW -> HIGH.
             * Force LOW state first, wait, then drive HIGH (rising edge). */
            bsp_io_output_setPin(false);
            vTaskDelay(pdMS_TO_TICKS(RELAY_PULSE_DURATION_MS));
            bsp_io_output_setPin(true);
        }
        else
        {
            /* OTWÓRZ (0 / OFF):
             * Edge: HIGH -> LOW.
             * Force HIGH state first, wait, then drive LOW (falling edge). */
            bsp_io_output_setPin(true);
            vTaskDelay(pdMS_TO_TICKS(RELAY_PULSE_DURATION_MS));
            bsp_io_output_setPin(false);
        }

        /* Wait for physical movement and debounced input feedback */
        if (io_manager_wait_for_relay_feedback(target_state, RELAY_FB_TIMEOUT_MS))
        {
            success = true;
            safe_printf(TAG, "Relay switched to %u (attempt %u)\r\n",
                        (unsigned int)target_state, (unsigned int)attempt);
            break;
        }

        safe_printf(TAG, "Relay attempt %u failed (target %u, fb %u), retrying...\r\n",
                    (unsigned int)attempt, (unsigned int)target_state,
                    (unsigned int)io_manager_getRelayState());
    }

    if (success)
    {
        s_relay_confirmed = true;
        s_relay_last_attempt_failed = false;
    }
    else
    {
        s_relay_confirmed = false;
        s_relay_last_attempt_failed = true;

        /* Reset output pin to match current confirmed feedback to avoid
         * leaving the control line continuously asserted on an unpowered circuit */
        bsp_io_output_setPin(io_manager_getRelayState() != 0U);

        safe_printf(TAG, "Relay ERROR: failed to reach target %u after %u attempts! fb=%u\r\n",
                    (unsigned int)target_state, (unsigned int)RELAY_MAX_ATTEMPTS,
                    (unsigned int)io_manager_getRelayState());
    }

    if (s_relay_mutex != NULL)
    {
        (void)xSemaphoreGive(s_relay_mutex);
    }

    return success;
}

bool io_manager_setRelayStateAsync(uint8_t target_state)
{
    target_state = (target_state != 0U) ? 1U : 0U;

    if (s_relay_cmd_queue == NULL)
    {
        return false;
    }

    uint8_t current_fb = io_manager_getRelayState();

    /* If relay is already confirmed and physically in the target state,
     * drop redundant cyclic frames immediately without waking up the worker task */
    if ((target_state == s_relay_last_target) &&
        (s_relay_confirmed) &&
        (!s_relay_last_attempt_failed) &&
        (current_fb == target_state))
    {
        return true;
    }

    /* Send command to relay worker queue without blocking (timeout 0) */
    if (xQueueSend(s_relay_cmd_queue, &target_state, 0) == pdPASS)
    {
        return true;
    }

    safe_printf(TAG, "RELAY QUEUE FULL: failed to enqueue target %u\r\n", (unsigned int)target_state);
    return false;
}

/**
 * @brief FreeRTOS worker task for asynchronous relay actuation and feedback verification.
 * @param[in] pvParameters Unused task parameter.
 */
static void relay_worker_task(void *pvParameters)
{
    (void)pvParameters;
    uint8_t target_cmd = 0U;

    for (;;)
    {
        /* Block indefinitely until a relay command is enqueued */
        if (xQueueReceive(s_relay_cmd_queue, &target_cmd, portMAX_DELAY) == pdTRUE)
        {
            /* Execute the physical edge-pulsing and feedback verification sequence */
            (void)io_manager_setRelayState(target_cmd);
        }
    }
}

/**
 * @brief Initializes the button debouncer structure with a given initial state.
 * @param[out] db Pointer to the debouncer structure.
 * @param[in] init_raw Initial raw button state read from hardware.
 */
static void io_debounce_init(io_debounce_t *db, uint16_t init_raw)
{
    for (uint8_t i = 0U; i < IO_DEBOUNCE_SAMPLES; i++)
    {
        db->history[i] = init_raw;
    }
    db->index = 0U;
    db->stable_state = init_raw;
}

/**
 * @brief Updates the parallel 16-bit button debouncer with a new raw sample.
 *        A bit transitions to 1 only if it is 1 across all history samples,
 *        and transitions to 0 only if it is 0 across all history samples.
 * @param[in,out] db Pointer to the debouncer structure.
 * @param[in] raw_sample New raw 16-bit button state.
 * @return Updated debounced 16-bit button state.
 */
static uint16_t io_debounce_update(io_debounce_t *db, uint16_t raw_sample)
{
    db->history[db->index] = raw_sample;
    db->index = (uint8_t)((db->index + 1U) % IO_DEBOUNCE_SAMPLES);

    uint16_t all_ones = 0xFFFFU;
    uint16_t any_ones = 0x0000U;

    for (uint8_t i = 0U; i < IO_DEBOUNCE_SAMPLES; i++)
    {
        all_ones &= db->history[i];
        any_ones |= db->history[i];
    }

    uint16_t all_zeros = (uint16_t)(~any_ones);

    /* Set bits that have been stably HIGH, clear bits that have been stably LOW */
    db->stable_state = (uint16_t)((db->stable_state & (~all_zeros)) | all_ones);

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


/**
 * @brief Builds the telemetry frame IO_CAN_ID.
 * @param[in] btnLow SW0..SW7.
 * @param[in] fuel Fuel level 0..100 %.
 * @param[in] rpm Engine RPM 0..750, sent as RPM / IO_RPM_PER_BIT.
 * @return Pointer to the static frame.
 */
static CAN_msg_t *io_manager_canCreateFrame(uint8_t btnLow, uint8_t fuel, uint16_t rpm)
{
    static uint32_t canCnt = 0;

    memset(&can_msg, 0, sizeof(CAN_msg_t));

    can_msg.format = CAN_MSG_FRAME_FORMAT_CAN;
    can_msg.ident = CAN_msg_set_extended_id(IO_CAN_ID); // extended ID
    can_msg.len = 8;

    can_msg.data[0] = btnLow;
    can_msg.data[1] = fuel;
    can_msg.data[2] = io_sim_rpmToByte(rpm);
    can_msg.data[3] = 0U;    
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
    uint16_t initial_btn = bsp_io_buttons_read();
    io_debounce_init(&btn_db, initial_btn);

    uint16_t prev_btn_state = initial_btn;
    uint16_t prev_eff_btn = initial_btn;
    io_sim_values_t eff;
    uint32_t elapsed_ms = 0U;
    uint32_t elapsed_can_ms = 0U;

    for (;;)
    {
        /* Sample and debounce 9 button inputs */
        uint16_t raw_btn = bsp_io_buttons_read();
        btn = io_debounce_update(&btn_db, raw_btn);
        io_sim_resolve(btn, s_fuel_phys, &eff);

        elapsed_ms += IO_SAMPLE_PERIOD_MS;
        elapsed_can_ms += IO_SAMPLE_PERIOD_MS;

        /* send data to can bus */
        if(elapsed_can_ms >= IO_CAN_MSG_PERIOD_MS)
        {
            elapsed_can_ms = 0U;

            s_fuel_phys = io_manager_adc_read_mapped();
            io_sim_resolve(btn, s_fuel_phys, &eff);

            if (!can_manager_send(&can1_inst, io_manager_canCreateFrame((uint8_t)(eff.btn & 0xFFU), eff.fuel, eff.rpm)))
            {
                safe_printf(TAG, "Failed to enqueue CAN message! Queue full?\r\n");
            }
        }

        /* Print status immediately on physical or effective button change, or periodically.
         * BTN / FUEL / RPM = effective values (sent on CAN), PHYS / ADC = physical inputs. */
        if ((btn != prev_btn_state) || (eff.btn != prev_eff_btn) || (elapsed_ms >= IO_PRINT_PERIOD_MS))
        {
            bool btn_changed = (btn != prev_btn_state) || (eff.btn != prev_eff_btn);
            prev_btn_state = btn;
            prev_eff_btn = eff.btn;
            elapsed_ms = 0U;

            bool out_state = bsp_io_output_get_state();
            uint32_t adc_raw = bsp_adc_read_raw();
            uint8_t adc_mapped = io_manager_adc_read_mapped();
            uint16_t e = eff.btn;

            safe_printf(TAG,
                        "SRC: %-4s | BTN: 0x%03X [SW8..0: %d %d%d%d%d %d%d%d%d]%s | PHYS: 0x%03X | PA8: %-4s | ADC: %4lu (%3lu%%) | FUEL: %3u%% | RPM: %3u\r\n",
                        (eff.src == IO_SRC_UART) ? "UART" : "IO",
                        (unsigned int)e,
                        (int)((e >> 8) & 1U),
                        (int)((e >> 7) & 1U), (int)((e >> 6) & 1U),
                        (int)((e >> 5) & 1U), (int)((e >> 4) & 1U),
                        (int)((e >> 3) & 1U), (int)((e >> 2) & 1U),
                        (int)((e >> 1) & 1U), (int)(e & 1U),
                        btn_changed ? " *" : "  ",
                        (unsigned int)btn,
                        out_state ? "HIGH" : "LOW",
                        (unsigned long)adc_raw,
                        (unsigned long)adc_mapped,
                        (unsigned int)eff.fuel,
                        (unsigned int)eff.rpm);
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

    /* Initialize button cache with initial hardware state */
    btn = bsp_io_buttons_read();
    s_fuel_phys = io_manager_adc_read_mapped();

    uint8_t boot_fb = io_manager_getRelayState();

    /* Initialize relay tracking state with boot physical state */
    s_relay_last_target = boot_fb;
    s_relay_last_attempt_failed = false;
    s_relay_confirmed = true;

    /* Ensure output pin matches current physical state on boot */
    bsp_io_output_setPin(boot_fb != 0U);

    if (s_relay_mutex == NULL)
    {
        s_relay_mutex = xSemaphoreCreateMutex();
        if (s_relay_mutex == NULL)
        {
            return false;
        }
    }

    if (s_relay_cmd_queue == NULL)
    {
        s_relay_cmd_queue = xQueueCreate(4U, sizeof(uint8_t));
        if (s_relay_cmd_queue == NULL)
        {
            return false;
        }
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

    ret = xTaskCreate(relay_worker_task,
                      "relay_worker",
                      TASK_STACK_RELAY_WORKER,
                      NULL,
                      TASK_PRIO_RELAY_WORKER,
                      &s_relay_worker_task_handle);
    if (ret != pdPASS)
    {
        return false;
    }

    return true;
}

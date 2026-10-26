# V-BOX Project Technical Documentation

**Target Hardware:** STM32C542RCT6 (ARM Cortex-M33 with TrustZone & FPU)  
**Development Board:** NUCLEO-C542RC (MB2213, Rev. B-02)  
**Operating System:** FreeRTOS Kernel V10.x (Preemptive, Tickless-capable)  
**Build Toolchain:** CMake + Ninja + GNU Arm Embedded Toolchain (`arm-none-eabi-gcc`) via STM32CubeCLT  

---

## Table of Contents
1. [Architecture Overview & Layer Separation](#1-architecture-overview--layer-separation)
2. [Hardware Peripherals Utilized](#2-hardware-peripherals-utilized)
3. [GPIO Pin Mapping & Hardware Specifications](#3-gpio-pin-mapping--hardware-specifications)
4. [Visual Nucleo-64 Board Pinout Diagram](#4-visual-nucleo-64-board-pinout-diagram)
5. [CAN Bus Subsystem: Architecture, Profiles, and Parsers](#5-can-bus-subsystem-architecture-profiles-and-parsers)
6. [Bistable Edge-Triggered Relay Control Engine](#6-bistable-edge-triggered-relay-control-engine)
7. [FreeRTOS Concurrency & Real-Time Task Model](#7-freertos-concurrency--real-time-task-model)
8. [Firmware & Hardware Versioning Subsystem](#8-firmware--hardware-versioning-subsystem)
9. [CAN Bootloader & OTA Dual-Target Subsystem](#9-can-bootloader--ota-dual-target-subsystem)

---

## 1. Architecture Overview & Layer Separation

The software architecture strictly decouples **application business logic** from **MCU-specific hardware abstraction (HAL)** and **third-party middleware**. This allows the entire application logic to be ported to different microcontrollers (e.g. STM32H5, STM32G4, or POSIX simulators) without modifying business code.

```text
+-------------------------------------------------------------------------------+
|                             APPLICATION LAYER                                 |
|                                                                               |
|   +-----------------------+   +-------------------+   +-------------------+   |
|   |   CAN Manager         |   |   CAN Parsers     |   |   I/O Manager     |   |
|   |   (can_manager.c/h)   |   |   (cp_v_box.c/h)  |   |   (io_manager.c/h)|   |
|   +-----------+-----------+   +---------+---------+   +---------+---------+   |
|               |                         |                       |             |
+---------------+-------------------------+-----------------------+-------------+
|                     HARDWARE ABSTRACTION LAYER (PORT CONTRACTS)               |
|                                                                               |
|   +-----------------------+   +-------------------+   +-------------------+   |
|   |   can_port.h          |   |   bsp_io.h        |   |   bsp_adc.h       |   |
|   |   (can_interface_t)   |   |   (I/O Contract)  |   |   (ADC Contract)  |   |
|   +-----------+-----------+   +---------+---------+   +---------+---------+   |
|               |                         |                       |             |
+---------------+-------------------------+-----------------------+-------------+
|                     MCU HARDWARE ADAPTERS (STM32C5 SPECIFIC)                  |
|                                                                               |
|   +-----------------------+   +-------------------+   +-------------------+   |
|   | stm32c5_can_adapter.c |   | bsp_io.c          |   | bsp_adc.c         |   |
|   | (Getter Pattern)      |   | (GPIOB/C IDR)     |   | (ADC1 Polling/DMA)|   |
|   +-----------+-----------+   +---------+---------+   +---------+---------+   |
|               |                         |                       |             |
+---------------+-------------------------+-----------------------+-------------+
|                          HARDWARE LAYER & MIDDLEWARE                          |
|                                                                               |
|   +-----------------------+   +-------------------+   +-------------------+   |
|   | STM32C5 HAL Drivers   |   | FreeRTOS Kernel   |   | STM32C542RC MCU   |   |
|   | (FDCAN, GPIO, ADC)    |   | (Tasks, Queues)   |   | (Nucleo-64 PCB)   |   |
|   +-----------------------+   +-------------------+   +-------------------+   |
+-------------------------------------------------------------------------------+
```

### Directory Structure & Governance Rules
* **`app/`**: Contains all proprietary business code and hardware contract interfaces.
  * `app/can_bus/`: CAN manager, reception dispatcher, and parser profiles (`app/can_bus/can_parser/parsers/`).
  * `app/io/`: I/O manager, 16-bit debouncing engine, and asynchronous relay worker.
  * `app/port/`: Hardware contracts (`can_port.h`, `bsp_io.h`, `bsp_adc.h`). Pure C types only; no HAL headers or FreeRTOS headers allowed.
  * `app/port/stm32c5/`: Concrete hardware implementations for STM32C5 using HAL.
* **`generated/`**: Auto-generated hardware initialization code (CubeMX / HAL initialization).
* **`middleware/`**: FreeRTOS real-time kernel. Kept clean and unedited.
* **`main.c`**: Minimal bootstrap entry point. Performs clock/HAL initialization, initializes RTOS instances, injects hardware adapters via getter functions (`stm32c5_can1_interfaceGet()`), and launches the scheduler.

### The Hardware Interface Getter Pattern
Hardware adapters never expose global structures via `extern`. All interface structures are declared `static const` inside their implementation file, and external modules access them solely through getter functions:
```c
/* In app/port/stm32c5/stm32c5_can_adapter.h */
const can_interface_t *stm32c5_can1_interfaceGet(void);

/* In main.c - Dependency Injection */
can_manager_init(&can1_inst, stm32c5_can1_interfaceGet(), "CAN1");
```

---

## 2. Hardware Peripherals Utilized

| Peripheral | Instance | Clock Source / Speed | Purpose & Configuration |
| :--- | :--- | :--- | :--- |
| **FDCAN** | `FDCAN1` | PLL1Q (144 MHz) | Primary automotive communication bus. Configured for **Classic CAN 2.0B** at **500 kbps** nominal bitrate (Bit Time: 36 tq Prop/Seg1, 36 tq Seg2, SJW 36 tq). Hardware filtering with classic dual-filter ID masking. Connected to onboard Microchip MCP2562FD transceiver (U7). |
| **FDCAN** | `FDCAN2` | PLL1Q (144 MHz) | Secondary communication bus dedicated for Bootloader, ISO 15765-2 (ISO-TP), and OTA firmware updates. Configured for **Classic CAN 2.0B** at **500 kbps** (Prescaler 1, Seg1 251, Seg2 36, SJW 36). |
| **ADC** | `ADC1` | HCLK / Synchronous (48 MHz) | Single-channel 12-bit analog input on pin **PA0**. Raw resolution: 0..4095. Linearly mapped in software to 0..100% vehicle fuel level representation. |
| **USART** | `USART2` | PCLK1 (144 MHz) | Serial debug / logging console at **115200 baud, 8N1**. Connected to ST-LINK Virtual COM Port (`VCP_TX` on PA2, `VCP_RX` on PA3). Protected by FreeRTOS mutex `printf_mutex`. |
| **GPIO** | `GPIOA`, `GPIOB`, `GPIOC`, `GPIOE` | AHB4 (144 MHz) | 9 discrete inputs with internal pull-downs, 1 push-pull relay output, 1 heartbeat LED, 1 CAN transceiver standby output. |
| **Cortex-M NVIC** | System | Core (144 MHz) | Preemption priority grouping configured with 4 bits for preemption priority (0..15). Syscall boundary: **priority 5**. Peripheral ISRs interacting with FreeRTOS (`FromISR`) have priority $\ge 5$. |
| **SysTick** | System | Core (144 MHz) | FreeRTOS timebase (1000 Hz / 1 ms tick rate). |

---

## 3. GPIO Pin Mapping & Hardware Specifications

### 3.1. Complete Pinout Table

| Pin Name | Direction | Mode / Pull | Connector & Pin | Arduino Alias | Function in V-BOX | Description / Circuit Notes |
| :--- | :---: | :---: | :---: | :---: | :--- | :--- |
| **PA0** | Input | Analog | **CN7 pin 28** | `A0` | **Analog fuel level Measurement** | 12-bit ADC1 Channel 0. Reads analog sensor / potentiometer voltage. |
| **PA8** | Output | Push-Pull, Low-Speed | **CN10 pin 23** | `D7` | **Bistable Relay Actuator** | Generates edges ($0 \rightarrow 1$ to CLOSE, $1 \rightarrow 0$ to OPEN) for bistable latching relay. |
| **PB2** | Input | Input, Pull-Down | **CN10 pin 22** | - | **SW8 (RPM engine)** | Discrete input (Bit 8). Active HIGH (+3.3V), idle LOW via pull-down. |
| **PB4** | Input | Input, Pull-Down | **CN10 pin 27** | `D5` | **SW0 (Ignition / ACC)** | Discrete input (Bit 0). Reads vehicle ignition key status. |
| **PB5** | Alt (AF9) | High-Speed AF | **CN10 pin 6** | `D4` | **FDCAN2_RX** | CAN2 bus receive line (Bootloader / OTA update). |
| **PB6** | Alt (AF9) | High-Speed AF | **CN10 pin 3** | `D10` | **FDCAN2_TX** | CAN2 bus transmit line (Bootloader / OTA update). |
| **PB7** | Input | Input, Pull-Down | **CN10 pin 5** | - | **SW3 (Lights / Headlamps)** | Discrete input (Bit 3). Reads headlights activation state. |
| **PB8** | Alt (AF9) | High-Speed AF | **CN10 pin 35** | `D15` | **FDCAN1_RX** | CAN bus receive line. Connected to MCP2562FD transceiver RXD. |
| **PB9** | Alt (AF9) | High-Speed AF | **CN10 pin 33** | `D14` | **FDCAN1_TX** | CAN bus transmit line. Connected to MCP2562FD transceiver TXD. |
| **PB12** | Input | Input, Pull-Down | **CN10 pin 16** | - | **SW4 (Locks / Relay Feedback)** | Discrete input (Bit 4). Auxiliary contact feedback indicating actual physical relay state. |
| **PB13** | Input | Input, Pull-Down | **CN10 pin 30** | - | **SW5 (Engine Hood)** | Discrete input (Bit 5). Hood limit switch / sensor. |
| **PB14** | Input | Input, Pull-Down | **CN10 pin 28** | `D1` | **SW6 (Hazard Lights)** | Discrete input (Bit 6). Hazard switch status. |
| **PB15** | Input | Input, Pull-Down | **CN10 pin 26** | `D0` | **SW7 (Handbrake)** | Discrete input (Bit 7). Parking brake engagement switch. |
| **PC7** | Input | Input, Pull-Down | **CN10 pin 19** | `D8` | **SW2 (Other Doors)** | Discrete input (Bit 2). Passenger / rear doors switch. *(Reallocated to free CAN2)* |
| **PC8** | Input | Input, Pull-Down | **CN10 pin 31** | `D2` | **SW1 (Driver Door)** | Discrete input (Bit 1). Driver door latch switch. *(Reallocated to free CAN2)* |
| **PE2** | Output | Push-Pull | **CN7 pin 21** | - | **FDCAN1_STBY** | Onboard CAN transceiver standby control. Driven LOW to enable transceiver. |
| **PA5** | Output | Push-Pull | **CN10 pin 11** | `D13` | **Heartbeat LED (LD1)** | Onboard green LED toggling at 1 Hz from `heartbeat_task`. |
| **PA2** | Alt (AF7) | High-Speed AF | **CN7 pin 9** | `D1 (TX)` | **USART2_TX (VCP Log)** | UART output to ST-LINK Virtual COM Port at 115200 baud. |
| **PA3** | Alt (AF7) | High-Speed AF | **CN7 pin 11** | `D0 (RX)` | **USART2_RX (VCP Log)** | UART input from ST-LINK Virtual COM Port. |

### 3.2. Secondary CAN Bus (FDCAN2) Active Configuration
The secondary CAN interface (`FDCAN2`) is fully implemented and configured for Classic CAN 2.0 @ 500 kbps:
* **`PB5` (CN10 pin 6 / Arduino D4):** Connected to external CAN transceiver **RXD** (`FDCAN2_RX`, AF9).
* **`PB6` (CN10 pin 3 / Arduino D10):** Connected to external CAN transceiver **TXD** (`FDCAN2_TX`, AF9).

---

## 4. Visual Nucleo-64 Board Pinout Diagram

The NUCLEO-C542RC board exposes two 38-pin double-row ST-Morpho headers: **CN7** (left side) and **CN10** (right side).

```text

========================================================================================
                    NUCLEO-C542RC: ST-MORPHO RIGHT CONNECTOR (CN10)
========================================================================================

           [Odd: Inner]                                            [Even: Outer]
           ------------                                            -------------
                   [ 1] : SDIO_DAT1 / LINA_RX            [ 2] : PC8      : DRZWI KIER
             PB6   [ 3] : FDCAN2_TX                      [ 4] : PC7      : DRZWI INNE
             PB7   [ 5] : swiatla                        [ 6] : PB5      : FDCAN2_RX
            AREF   [ 7] :                                [ 8] : 5V_STLK
             GND   [ 9] :                                [10] : NC
             PA5   [11] : led HEARTBEAT                  [12] : PA12
             PA6   [13] :                                [14] : PA11
             PA7   [15] :                                [16] : PB12    : zamki_zamkniete (relay feetback)
             PC9   [17] :                                [18] : CN10_18_ADC
             PC6   [19] :                                [20] : GND
             PA9   [21] : [wolny]                        [22] : PB2     : engine RPM
             PA8   [23] : Przekaznik                     [24] : NC
            PB10   [25] :                                [26] : PB15    : Hamulec reczny
             PB4   [27] : stacyjka                       [28] : PB14    : Awaryjne
             PC1   [29] :                                [30] : PB13    : Maska
             PB3   [31] :                                [32] : AGND
            PA10   [33] : [wolny]                        [34] : PC4
             PB9   [35] : FDCAN1_TX                      [36] : CN10_36_GPIO
             PB8   [37] : FDCAN1_RX                      [38] : NC
========================================================================================

========================================================================================
                    NUCLEO-C542RC: ST-MORPHO LEFT CONNECTOR (CN7)
========================================================================================

           [Odd: Outer]                                            [Even: Inner]
           ------------                                            -------------
             PC10  [ 1] :                                [ 2] : PC11
             PC12  [ 3] :                                [ 4] : PD2
             3V3   [ 5] :                                [ 6] : 5V_EXT      
       PH2_BOOT0   [ 7] :                                [ 8] : GND         
              PA2  [ 9] :                                [10] : NC
              PA3  [11] :                                [12] : IOREF      
             PA13  [13] :                                [14] : NRST
             PA14  [15] :                                [16] : 3V3         
             PA15  [17] :                                [18] : 5V          
              GND  [19] :                                [20] : GND         
              PE2  [21] :                                [22] : GND         
             PC13  [23] :                                [24] : VIN         
             PC14  [25] :                                [26] : NC
             PC15  [27] :                                [28] : PA0
              PH0  [29] :                                [30] : PA1
              PH1  [31] :                                [32] : PA4
             VBAT  [33] :                                [34] : PB0   : ADC fuel level
              PC2  [35] :                                [36] : PC5
              PC3  [37] :                                [38] : PC0
========================================================================================
```


---

## 5. CAN Bus Subsystem: Architecture, Profiles, and Parsers

### 5.1. CAN Processing Pipeline
The CAN subsystem is built entirely around an **event-driven, non-polling** model to maximize real-time performance and prevent CPU starvation:

```text
[ Physical CAN Bus ]
        │
        ▼
[ FDCAN1 Hardware FIFO 0 ]  ──► Hardware Classic Filter Masking (Accept / Reject)
        │
        ▼ (Interrupt Trigger)
[ HAL_FDCAN_RxFifo0Callback ] (ISR Context)
        │
        ▼ xSemaphoreGiveFromISR(rx_sem)
[ can_rx_task ] (High Priority Task) ◄── Awoken instantly
        │
        ▼ hal_fdcan_get_rx_message()
[ can_manager Dispatcher ]  ──► Matches ID against registered profile filter table
        │
        ▼ Calls profile callback (e.g., on_v_box_relay_cmd_rx)
[ Callback Handler ]  ──► Parses payload, pushes to FreeRTOS queue (< 1 µs execution)
        │
        ▼ Returns immediately (Zero delay, can_rx_task keeps draining FIFO)
```

### 5.2. Profile Architecture
Each parser profile is completely self-contained in `app/can_bus/can_parser/parsers/cp_<name>.c`:
* **Zero Global Externs:** The filter table `s_filters` and the descriptor structure `s_profile` are declared `static const` in the `.c` file.
* **Profile Getter:** The corresponding `.h` file exposes exclusively a getter function:
  ```c
  const can_parser_profile_t *cp_v_box_profile_get(void);
  ```
* **Filter Rule Calculation:** Filter count is automatically calculated at compile time:
  ```c
  .num_filters = (uint8_t)(sizeof(s_filters) / sizeof(s_filters[0]))
  ```
* **Runtime Dynamic Switching & Composite Boot/OTA Attachment:** Profiles can be activated or swapped on the fly without stopping tasks, and `CAN_PARSER_ID_BOOT_OTA` can be attached alongside the primary profile on `CAN1`:
  ```c
  can_parser_select(&can1_inst, CAN_PARSER_ID_V_BOX);
  can_parser_attach_boot_ota(&can1_inst);
  ```

### 5.3. Active CAN Messages in V-BOX Profile (`cp_v_box`)

#### 1. Inbound Command: Relay Control (`0x01F2F3F0`, Extended 29-bit ID)
* **DLC:** 8 bytes (minimum validated length: 3 bytes)
* **Payload Format:**
  * `Byte [0..1]`: Reserved / Command prefix.
  * `Byte [2]`: Control byte $\rightarrow$ **Bit 0**: `1` = CLOSE (ZAMKNIJ), `0` = OPEN (OTWÓRZ).
  * `Byte [3..7]`: Unused.
* **Behavior:** Callback extracts target state and calls `io_manager_setRelayStateAsync()`. Execution time is under 1 microsecond.

#### 2. Outbound Telemetry: Discrete Inputs & ADC Status (`0x01F2F3F4`, Extended 29-bit ID)
* **Transmission Interval:** 100 ms periodic (broadcast from `io_manager_task`).
* **DLC:** 8 bytes.
* **Payload Format:**
  * `Byte [0]`: Discrete Inputs Low Byte (`SW0..SW7`):
    * Bit 0: ACC Stacyjka
    * Bit 1: Drzwi Kierowcy (PC8)
    * Bit 2: Drzwi Inne (PC7)
    * Bit 3: Światła (PB7)
    * Bit 4: Zamki / Relay Feedback (PB12)
    * Bit 5: Maska (PB13)
    * Bit 6: Awaryjne (PB14)
    * Bit 7: Hamulec Ręczny (PB15)
  * `Byte [1]`: ADC Mapped Percentage (0..100%) – fuel level.
  * `Byte [2]`: Engine RPM / 3 (`0..250` = `0..750` RPM, the receiver multiplies by 3). Physical SW8: `0xFA` (750 RPM) or `0`.
  * All inputs can be simulated over UART instead of the physical IO: [io_uart_simulation.md](io/io_uart_simulation.md).
  * `Byte [3]`: Reserved (`0x00`).
  * `Byte [4..7]`: Big-Endian 32-bit Transmission Rolling Counter.

---

## 6. Bistable Edge-Triggered Relay Control Engine

The project controls a specialized **pulse/edge-driven bistable locking actuator/relay** that maintains its mechanical contact position even after total system power loss.

### 6.1. Actuation Principle & Waveform Generation
The physical relay does not respond to static levels; it requires voltage edge transitions:
* **CLOSE Command ($0 \rightarrow 1$ / ON):**
  1. Pin **PA8** is forced LOW for a preparatory pulse of **40 ms** (`RELAY_PULSE_DURATION_MS`).
  2. Pin **PA8** is driven HIGH (generating a sharp **rising edge**).
* **OPEN Command ($1 \rightarrow 0$ / OFF):**
  1. Pin **PA8** is forced HIGH for a preparatory pulse of **40 ms**.
  2. Pin **PA8** is driven LOW (generating a sharp **falling edge**).

```text
CLOSE COMMAND (Target = 1):
PA8: ───[Force LOW 40ms]───┐▲ Rising Edge (Activates Coil)
                           └──────────────────────────────────────► Continues HIGH

OPEN COMMAND (Target = 0):
PA8: ───[Force HIGH 40ms]──┐▼ Falling Edge (Activates Release)
                           └──────────────────────────────────────► Continues LOW
```

### 6.2. Closed-Loop Verification & State Machine
Actuation is verified by reading auxiliary feedback contact on **PB12** (Bit 4 of debounced inputs):
* **Verification Window:** After edge generation, the software polls the debounced state every 10 ms up to a maximum timeout of **200 ms** (`RELAY_FB_TIMEOUT_MS`).
* **Retry Mechanism:** If feedback does not match within 200 ms, the actuation cycle is repeated up to **2 attempts** (`RELAY_MAX_ATTEMPTS`).

### 6.3. Fault Tolerance & Power-Cut Recovery
To guarantee rock-solid reliability in automotive environments where power to the actuator might be cut or mechanical locks may jam:
* **Failure State Tracking:** If feedback fails after all retries, the state machine sets `s_relay_last_attempt_failed = true` and `s_relay_confirmed = false`.
* **Safe Output De-assertion:** In failure mode, pin PA8 is immediately reset to match the *actual measured physical feedback* instead of remaining asserted, preventing continuous +3.3V bias on unpowered or damaged circuitry.
* **Recovery on Power Restoration:** When power is restored and the user re-sends the command, the engine detects that the last attempt failed. It bypasses any short-circuit checks, resets the pin, and executes the full edge sequence from scratch until confirmed.

### 6.4. Boot Synchronization (Anti-Clicking Protection)
Upon MCU startup, `io_manager_init()` reads the physical feedback contact before configuring default states:
```c
uint8_t boot_fb = io_manager_getRelayState();
s_relay_last_target = boot_fb;
s_relay_confirmed   = true;
bsp_io_output_setPin(boot_fb != 0U);
```
If the MCU reboots while the relay was closed, PA8 is initialized to match the closed state without toggling. When the first CAN command arrives demanding the current state (`target_state == current_fb`), the function aligns the pin statically without generating opposing edges, preventing wear and redundant clicking.

### 6.5. Asynchronous Worker Architecture
Because relay actuation involves `vTaskDelay` pauses (40 ms pulse + up to 200 ms timeout), executing this inside `can_rx_task` would block the CAN receiver for hundreds of milliseconds, resulting in hardware buffer overflow (`RF0L` – Message Lost).

The problem is resolved using an **asynchronous event-driven worker**:
1. When CAN message arrives, `on_v_box_relay_cmd_rx()` calls `io_manager_setRelayStateAsync(target)`.
2. `io_manager_setRelayStateAsync()` performs a non-blocking queue push:
   ```c
   xQueueSend(s_relay_cmd_queue, &target_state, 0);
   ```
3. The callback terminates in **< 1 microsecond**.
4. The background `relay_worker_task` (priority `tskIDLE_PRIORITY + 2`) pops the request and executes `io_manager_setRelayState()` with full retry and verification logic in background.

---

## 7. FreeRTOS Concurrency & Real-Time Task Model

### 7.1. Task Configuration Table

| Task Name | Priority | Stack Size (Words / Bytes) | Execution Nature | Primary Function |
| :--- | :---: | :---: | :--- | :--- |
| **`can_rx_task`** | `configMAX_PRIORITIES - 1` (15) | 512 words (2048 B) | Event-Driven (Blocked on `rx_sem`) | Drains FDCAN1 hardware FIFO 0 immediately upon interrupt notification and dispatches to parser profile callbacks. |
| **`can_tx_task`** | `configMAX_PRIORITIES - 2` (14) | 256 words (1024 B) | Event-Driven (Blocked on `tx_queue`) | Dequeues outgoing CAN frames and writes them to FDCAN1 transmission mailboxes. |
| **`io_manager`** | `tskIDLE_PRIORITY + 2` (2) | 512 words (2048 B) | Periodic (10 ms tick rate) | Samples 9 inputs, executes 16-bit parallel debouncing (40 ms window), reads ADC1, broadcasts CAN telemetry (100 ms), logs UART (500 ms). |
| **`relay_worker`**| `tskIDLE_PRIORITY + 2` (2) | 256 words (1024 B) | Event-Driven (Blocked on `s_relay_cmd_queue`) | Executes long-duration bistable relay edge actuation, timeout delays, and retries in background. |
| **`main_task`** | `tskIDLE_PRIORITY + 2` (2) | 512 words (2048 B) | Periodic (1200 ms) | System supervisory task. |
| **`heartbeat`** | `tskIDLE_PRIORITY + 1` (1) | 128 words (512 B) | Periodic (1000 ms) | Toggles onboard green LED (LD1 on PA5) to verify OS kernel liveness. |

### 7.2. Inter-Task Communication & Synchronization Objects

```text
[ ISR: FDCAN_RxFifo0 ] ──► (Give) ──► [ rx_sem: Binary Semaphore ] ──► (Take) ──► [ can_rx_task ]
                                                                                         │
[ can_rx_task ] ───────► (Send, 0 timeout) ──► [ s_relay_cmd_queue ] ───────────────────┼──► (Receive) ──► [ relay_worker_task ]
                                                 (Depth: 4, uint8_t)                     │
                                                                                         ▼
[ Any Task ] ──────────► (Lock / Unlock) ────► [ printf_mutex: Mutex ] ────────► [ UART2 safe_printf ]
[ relay_worker ] ──────► (Lock / Unlock) ────► [ s_relay_mutex: Mutex ] ────────► [ HW Actuation PA8 ]
[ io_manager_task ] ───► (Send) ─────────────► [ tx_queue: Queue ] ────────────► (Receive) ──► [ can_tx_task ]
```

* **`rx_sem` (Binary Semaphore):** Synchronizes hardware FDCAN interrupt with `can_rx_task`. Released in `FromISR` context.
* **`tx_queue` (Queue):** Thread-safe message queue holding outgoing `CAN_msg_t` structures (depth: 16 frames).
* **`s_relay_cmd_queue` (Queue):** Decouples CAN RX callbacks from relay hardware actuation (depth: 4 items, `uint8_t`).
* **`s_relay_mutex` (Mutex):** Prevents concurrent access to relay actuation hardware across multiple threads.
* **`printf_mutex` (Mutex):** Ensures atomic, thread-safe transmission of UART log strings in `safe_printf()`, preventing interleaved character output.

### 7.3. NVIC Interrupt Priority Governance
To adhere strictly to ARM Cortex-M and FreeRTOS kernel rules:
* All 4 priority bits are assigned to Preemption Priority (`NVIC_PRIORITYGROUP_4`).
* The FreeRTOS syscall limit is **Priority 5** (`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`).
* **Critical Rule:** Every peripheral ISR invoking FreeRTOS APIs ending with `FromISR` (such as `FDCAN1_IT0_IRQn`) **must have an NVIC priority $\ge 5$**. If configured with priority 0..4, the processor immediately triggers a unrecoverable `configASSERT()`.
* In `mx_fdcan1.c`, the FDCAN interrupt priority is explicitly set:
  ```c
  HAL_CORTEX_NVIC_SetPriority(FDCAN1_IT0_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, 0);
  ```

---

## 8. Firmware & Hardware Versioning Subsystem

The project employs an automated versioning architecture adhering to **Semantic Versioning 2.0 (SemVer)** and automated Git build metadata injection.

### 8.1. Versioning Architecture & Data Structure
The public API in [app/version/version.h](file:///c:/WORK/5_v-box/app/version/version.h) exposes immutable build descriptors:

```c
typedef struct {
    uint8_t     fw_major;           /**< Firmware Semantic Version Major */
    uint8_t     fw_minor;           /**< Firmware Semantic Version Minor */
    uint8_t     fw_patch;           /**< Firmware Semantic Version Patch */
    const char *fw_version_str;     /**< Full semantic version string (e.g. "v1.0.0") */
    const char *git_commit;         /**< Git short commit hash (e.g. "d258caa") */
    const char *git_describe;       /**< Git describe tag or commit string */
    const char *build_timestamp;    /**< Build timestamp string (UTC) */
    const char *build_type;         /**< Build type (e.g. "Debug", "Release") */
    const char *hw_target_name;     /**< Target hardware board name */
    const char *hw_target_mcu;      /**< Target MCU part number */
    uint8_t     hw_major;           /**< Hardware Revision Major */
    uint8_t     hw_minor;           /**< Hardware Revision Minor */
} app_version_info_t;
```

### 8.2. CMake Build Automation (Single Source of Truth: Git Tag)
The firmware version is defined **exclusively by the latest annotated or lightweight git tag matching `vX.Y.Z`**. There are no manually maintained version numbers in `CMakeLists.txt`.

* The `generate_version` custom target runs [cmake/version.cmake](file:///c:/WORK/5_v-box/cmake/version.cmake) on **every build** (not only at configure time), so new tags, commits and the `-dirty` state are always reflected without re-running CMake.
* `git describe --tags --match "v[0-9]*" --abbrev=0` $
ightarrow$ latest release tag, parsed into `FW_VERSION_MAJOR/MINOR/PATCH` and `FW_VERSION_STRING` (e.g. `v2.0.0`).
* `git describe --tags --match "v[0-9]*" --always --dirty` $
ightarrow$ `FW_GIT_DESCRIBE` with distance from the tag, commit and dirty flag (e.g. `v2.0.0-6-g52797a0-dirty`).
* `git rev-parse --short HEAD` $
ightarrow$ `FW_GIT_COMMIT`.
* `FW_BUILD_TIMESTAMP` $
ightarrow$ UTC time of the build that last changed the version metadata.
* The header is rewritten only when the git state or build configuration changes, so incremental builds recompile only [app/version/version.c](file:///c:/WORK/5_v-box/app/version/version.c) after a commit or tag.
* Without a git repository or a matching tag the build falls back to `v0.0.0` and prints a CMake warning.
* `FW_VERSION_MAJOR/MINOR` are reported over CAN (OTA discovery packs them into one byte as `major << 4 | minor`, so each must stay in the range 0–15).

**Release procedure:** commit the changes, then `git tag v2.1.0` (and `git push origin v2.1.0`) and build. No source file needs to be edited.

**Bootloader release line:** the Bootloader (`v_box_boot`) is versioned independently from tags `boot-vX.Y.Z` (e.g. `git tag boot-v1.3.0`). The `generate_boot_version` target runs the same script with `TAG_PREFIX=boot-v` and produces `boot_version_config.h` (`BOOT_VERSION_MAJOR/MINOR/PATCH`, `BOOT_VERSION_STRING`, `BOOT_GIT_DESCRIBE`), used by the UART banner, the packed discovery version byte and the `CMD_SELECT_TARGET` response.

### 8.3. Diagnostic Boot Banner (UART)
During early boot (before FreeRTOS scheduler initialization), `main()` invokes `version_printBootBanner()`. The output is transmitted over USART2 at 115200 baud:

```text
======================================================================
  V-BOX TESTER - Automotive Telematics Test Bench & Signal Converter  
======================================================================
  Firmware   : v1.0.0 (d258caa) [debug_GCC_NUCLEO-C542RC]
  Commit     : d258caa
  Target MCU : STM32C542RCT6 (ARM Cortex-M33 @ 144 MHz)
  Hardware   : NUCLEO-C542RC (Rev 1.0)
  Build Time : 2026-10-15 12:28:34 UTC
======================================================================
```

---

## 9. CAN Bootloader & OTA Dual-Target Subsystem

> **Full Guide & Operating Manual:** See [docs/bootloader/can_bootloader_and_ota_guide.md](file:///c:/WORK/5_v-box/docs/bootloader/can_bootloader_and_ota_guide.md) for complete program flow diagrams, hardware wiring (`CANable 2.0` + `FDCAN2`), and CLI usage examples.

The system implements a fully integrated, dual-target and dual-bus firmware architecture consisting of an autonomous bare-metal **CAN Bootloader** (`v_box_boot`) and a preemptive FreeRTOS **Main Application** (`v_box`), communicating over either **FDCAN1** (`PB8` RX / `PB9` TX) or **FDCAN2** (`PB5` RX / `PB6` TX) at 500 kbps Classic CAN 2.0B using 29-bit Extended CAN IDs in the proprietary V-Box namespace (`0x01F2F3xx`).

### 9.1. Flash & SRAM Memory Map

| Region | Physical Address Range | Size | Description |
| :--- | :--- | :---: | :--- |
| **Bootloader Slot** | `0x08000000` – `0x08007FFF` | 32 KB (Pages 0..15) | Bare-metal OpenBootloader core, Fast-Boot engine, Silent Recovery on-demand beacon |
| **Main Application** | `0x08008000` – `0x08037FFF` | 192 KB (Pages 16..111) | FreeRTOS application firmware, tasks, CAN managers, telemetry |
| **Bootloader Staging Slot** | `0x08038000` – `0x0803FFFF` | 32 KB (Pages 112..127) | Safe isolated staging area for remote Bootloader updates via MainApp |
| **SRAM (Kernel/App)** | `0x20000000` – `0x2000FFBF` | 63.9 KB | Zero-initialized data, BSS, FreeRTOS ucHeap pool, system stacks |
| **SHARED_RAM (IPC Mailbox)** | `0x2000FFC0` – `0x2000FFFF` | 64 B (`.noinit`) | Non-volatile IPC structure (`boot_shared_ram_t`, including `active_can_bus`) preserved across resets |

### 9.2. Application Firmware OTA Update Flow (App -> Bootloader -> App)

1. **Target Discovery & Selection:** PC scans nodes via broadcast `CMD_DISCOVER_NODES (0xA0)` + 4-byte `"VBOX"` magic token on 29-bit Extended ID `0x01F2F3DF`. Devices reply with unique 96-bit UID on `0x01F2F3E9` (and an on-demand `0xEE` diagnostic frame if in `BOOT_MODE_RECOVERY`). The target node is locked with `CMD_SELECT_TARGET (0xA1)` on `0x01F2F3E0` $\rightarrow$ `0x01F2F3E8`.
2. **Reboot to Bootloader:** PC issues `CMD_ENTER_BOOTLOADER (0xA2)`. The Main Application writes `magic = BOOT_SHARED_MAGIC` and `active_can_bus` (`1` = CAN1, `2` = CAN2) into `SHARED_RAM` and executes `NVIC_SystemReset()`.
3. **Flash Erase, Deferred Vector Write & Verification:** Bootloader starts in < 2 ms, validates IPC mailbox, erases Application pages (`0x08008000`), and streams binary blocks (up to 1024 B, `OTA_MAX_BLOCK_SIZE`) via ISO 15765-2 transport with block-level CRC32 (Flow Control: Bootloader BS=0/STmin=0 with a hardware filter accepting only OTA IDs; Application BS=16/STmin=0). **Block 0 (`0x08008000`) is buffered in RAM (`s_deferred_vector_block`) and committed to Flash only inside `CMD_VERIFY_FIRMWARE (0xA3)`** after full-image CRC32 validation succeeds, preventing power-loss bricking.
4. **Fast-Boot Jump:** On `CMD_GO_EXECUTE (0x21)`, Bootloader deinitializes peripherals and RCC clocks (`mx_rcc_deinit()`), resets VTOR to `0x08008000`, clears Cortex-M33 `MSPLIM`/`PSPLIM`, and branches to the new Application `Reset_Handler`.

### 9.3. Remote Bootloader Update Flow via Staging Slot (Task 6.1)

To prevent bricking the microcontroller in case of CAN bus disconnection or power failure during a bootloader update:
1. **Target Verification:** The PC updater ensures the target is executing in **Main Application mode** (`0x08008000`), where the Bootloader Flash (`0x08000000`) is completely idle.
2. **Staging Upload:** The new Bootloader image is streamed over ISO-TP into the dedicated top 32 KB **Staging Slot** (`0x08038000`). If transmission fails at any point, the existing Bootloader at `0x08000000` remains 100% intact.
3. **Integrity & Vector Table Validation:** On `CMD_UPDATE_BOOTLOADER (0xA4)`, the Main Application:
   * Validates full image CRC32 of the staged binary against the expected checksum.
   * Validates the Cortex-M33 vector table (ensuring `MSP` resides in SRAM `0x20000000`..`0x20010000` and `Reset_Handler` points into `0x08000001`..`0x08008000` with Thumb bit 1).
4. **Atomic Installation:** In a critical section (`taskENTER_CRITICAL()`), the Main Application erases `0x08000000`, copies the verified image from Staging, confirms byte-by-byte with `memcmp`, erases the Staging Slot, and sends positive `ACK`.
5. **Reboot:** On `CMD_GO_EXECUTE`, `NVIC_SystemReset()` executes the newly installed Bootloader.



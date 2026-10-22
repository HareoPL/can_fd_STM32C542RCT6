# Przewodnik Implementacji i Portowania Magistrali CAN

Dokument opisuje architekturę warstwową, proces implementacji oraz procedurę portowania modułu magistrali CAN dla mikrokontrolerów z rodziny **STM32C5** (oraz innych układów STM32) w systemie czasu rzeczywistego **FreeRTOS**.

---

## Spis Treści
1. [Wstęp i Architektura Warstwowa](#1-wstęp-i-architektura-warstwowa)
2. [Struktura Katalogów i Plików](#2-struktura-katalogów-i-plików)
3. [Krok 1: Kontrakt Abstrakcji Sprzętowej (`can_port.h`)](#3-krok-1-kontrakt-abstrakcji-sprzętowej-can_porth)
4. [Krok 2: Adapter Sprzętowy (`stm32c5_can_adapter.h` i `.c`)](#4-krok-2-adapter-sprzętowy-stm32c5_can_adapterh-i-c)
5. [Krok 3: Uniwersalny Menedżer CAN (`can_manager.h` i `.c`)](#5-krok-3-uniwersalny-menedżer-can-can_managerh-i-c)
6. [Krok 4: Konfiguracja FreeRTOS i Przerwań NVIC](#6-krok-4-konfiguracja-freertos-i-przerwań-nvic)
7. [Krok 5: Rejestracja w `CMakeLists.txt`](#7-krok-5-rejestracja-w-cmakeliststxt)
8. [Krok 6: Inicjalizacja i Uruchomienie w `main.c`](#8-krok-6-inicjalizacja-i-uruchomienie-w-mainc)
9. [Krok 7: Dodawanie kolejnej szyny (np. CAN2) lub portowanie na inny MCU](#9-krok-7-dodawanie-kolejnej-szyny-np-can2-lub-portowanie-na-inny-mcu)

---

## 1. Wstęp i Architektura Warstwowa

### Dlaczego stosujemy architekturę warstwową?
Zgodnie z zasadami projektu (plik `GEMINI.md`) obowiązuje ścisła separacja:
* **Logika biznesowa i kolejkowanie:** Zależna wyłącznie od FreeRTOS, w 100% niezależna od układu mikrokontrolera.
* **Warstwa sprzętowa:** Dedykowany adapter tłumaczący operacje na wywołania HAL/LL mikrokontrolera.

Dzięki temu zmiana rodziny mikrokontrolera (np. z STM32C5 na STM32H5 lub STM32G4) albo dodanie drugiego kontrolera (CAN2) **nie wymaga dotykania ani jednej linijki kodu w menedżerze CAN i aplikacji**.

### Model Dependency Injection z funkcją Getter
Zamiast eksportować globalną strukturę jako `extern const can_interface_t stm32c5_can1_interface;`, stosujemy hermetyzację poprzez funkcję pobierającą wskaźnik do interfejsu (getter):
```c
const can_interface_t *stm32c5_can1_interfaceGet(void);
```
Sama struktura adaptera w pliku `.c` oznaczona jest jako `static`, co zapobiega zanieczyszczaniu przestrzeni nazw symboli globalnych i uniemożliwia przypadkową modyfikację.

### Architektura Zdarzeniowa (Event-Driven vs Polling)
W systemach czasu rzeczywistego odpytywanie w pętli (polling) marnuje cykle procesora i opóźnia odbiór ramek. Wdrożona architektura wykorzystuje **przerwania i semafor binarny**:
1. Ramka trafia do sprzętowego Rx FIFO 0 kontrolera FDCAN.
2. Kontroler zgłasza przerwanie NVIC -> wywoływany jest callback `HAL_FDCAN_RxFifo0Callback()`.
3. Callback wywołuje zarejestrowaną funkcję powiadomienia, która oddaje semafor binarny (`xSemaphoreGiveFromISR`).
4. Dedykowany task odbiorczy FreeRTOS (`can_rx_task`), który oczekiwał na semaforze (`xSemaphoreTake`), zostaje natychmiast wybudzony, pobiera ramkę ze sprzętowego FIFO i przekazuje do aplikacji.

```mermaid
flowchart TD
    subgraph AppLayer ["Warstwa Aplikacji"]
        A[main_task / aplikacja]
    end

    subgraph CanManagerLayer ["Uniwersalna Warstwa Logiki (app/can_bus)"]
        CM[can_manager]
        TxQ[(Kolejka TX FreeRTOS)]
        RxSem((Semafor RX FreeRTOS))
        TxTask[can_tx_task]
        RxTask[can_rx_task]
    end

    subgraph PortLayer ["Abstrakcja Sprzętowa (app/port)"]
        CP[can_port.h - can_interface_t]
    end

    subgraph AdapterLayer ["Adapter Sprzętowy (app/port/stm32c5)"]
        Getter["stm32c5_can1_interfaceGet()"]
        Adapter[stm32c5_can_adapter.c]
        ISR[HAL_FDCAN_RxFifo0Callback]
    end

    subgraph HardwareLayer ["Sprzęt STM32C5"]
        HAL[HAL_FDCAN / Rejestry]
        FIFO[Hardware FDCAN FIFO 0]
    end

    A -->|can_manager_send| TxQ
    TxQ -->|xQueueReceive| TxTask
    TxTask -->|transmit| CP
    CP --> Adapter
    Adapter -->|HAL_FDCAN_ReqTransmitMsgFromFIFOQ| HAL

    FIFO -->|Przerwanie IT0| ISR
    ISR -->|s_can1_rx_cb| RxSem
    RxSem -->|xSemaphoreTake| RxTask
    RxTask -->|receive| CP
    CP --> Adapter
    Adapter -->|HAL_FDCAN_GetReceivedMessage| FIFO
    RxTask -->|Obsługa/Kolejka| A

    Getter -.->|Zwraca wskaźnik| CP
```

---

## 2. Struktura Katalogów i Plików

Wszystkie nowe pliki tworzone są w katalogu `app/` zgodnie z regułami projektu:

```text
c:\WORK\5_v-box\
├── app\
│   ├── port\
│   │   ├── can_port.h                 <-- Abstrakcja interfejsu (CZYSTY C, bez HAL/FreeRTOS)
│   │   └── stm32c5\
│   │       ├── stm32c5_can_adapter.h  <-- Nagłówek adaptera (deklaracja stm32c5_can1_interfaceGet)
│   │       └── stm32c5_can_adapter.c  <-- Implementacja adaptera (HAL_FDCAN, przerwania ISR)
│   ├── can_bus\
│   │   ├── can_manager.h              <-- Publiczne API (can_instance_t, can_manager_init, send, read)
│   │   └── can_manager.c              <-- Logika FreeRTOS (taski, kolejki, obsługa semafora)
│   └── tasks_config.h                 <-- Konfiguracja priorytetów i rozmiarów stosów FreeRTOS
├── generated\
│   ├── hal\
│   │   └── mx_fdcan1.c                <-- Konfiguracja sprzętowa CubeMX (priorytet przerwania NVIC!)
│   └── middleware\
│       └── mx_freertos_app.c          <-- Oczyszczone z logiki biznesowej taski startowe
├── main.c                             <-- Punkt wejścia: inicjalizacja i wstrzyknięcie adaptera
└── CMakeLists.txt                     <-- Rejestracja źródeł C i katalogów include
```

---

## 3. Krok 1: Kontrakt Abstrakcji Sprzętowej (`can_port.h`)

Plik: `app/port/can_port.h`

### Zasady:
1. **Całkowity zakaz** dołączania jakichkolwiek nagłówków STM32 (`#include "stm32c5xx_hal.h"` itp.) oraz FreeRTOS.
2. Definiuje wskaźniki na funkcje operujące na standardowych typach (`uint32_t`, `uint8_t*`, `bool`).
3. Definiuje sygnaturę callbacku powiadomienia RX wywoływanego z poziomu przerwania.

### Zawartość pliku:
```c
#ifndef CAN_PORT_H
#define CAN_PORT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Dozwolone prędkości transmisji magistrali CAN / fazy danych CAN FD.
 */
typedef enum {
    CAN_BAUDRATE_CUSTOM = 0,       /**< Domyślna konfiguracja sprzętowa (np. z CubeMX) */
    CAN_BAUDRATE_125K   = 125000,  /**< 125 kbps */
    CAN_BAUDRATE_250K   = 250000,  /**< 250 kbps */
    CAN_BAUDRATE_500K   = 500000,  /**< 500 kbps */
    CAN_BAUDRATE_1000K  = 1000000, /**< 1 Mbps */
    CAN_BAUDRATE_2000K  = 2000000, /**< 2 Mbps (faza danych CAN FD) */
    CAN_BAUDRATE_4000K  = 4000000, /**< 4 Mbps (faza danych CAN FD) */
    CAN_BAUDRATE_8000K  = 8000000  /**< 8 Mbps (faza danych CAN FD) */
} can_baudrate_t;

/**
 * @brief Tryb pracy szyny.
 */
typedef enum {
    CAN_MODE_CLASSIC = 0, /**< Tylko Classic CAN 2.0 (max 8 B) */
    CAN_MODE_FD_NO_BRS,   /**< Ramki CAN FD, faza danych z prędkością nominalną */
    CAN_MODE_FD_BRS       /**< Ramki CAN FD, faza danych przełączana na baudrate_FD */
} can_frame_mode_t;

/**
 * @brief Struktura konfiguracyjna parametrów pracy szyny CAN/FDCAN.
 */
typedef struct {
    can_frame_mode_t mode;          /**< Tryb pracy szyny */
    can_baudrate_t baudrate_normal; /**< Prędkość fazy arbitrażu (nominalna) */
    can_baudrate_t baudrate_FD;     /**< Prędkość fazy danych (tylko CAN_MODE_FD_BRS) */
} can_config_t;

/**
 * @brief Typ wskaźnika na funkcję callback powiadamiającą o odebraniu danych w przerwaniu (ISR).
 * @param context Kontekst powiązany z instancją CAN (np. wskaźnik na can_instance_t).
 */
typedef void (*can_rx_notify_fn)(void* context);

/**
 * @brief Generyczna struktura interfejsu sprzętowego magistrali CAN.
 */
typedef struct {
    /** (Re)konfiguracja i start kontrolera o parametrach z can_config_t */
    bool (*init)(const can_config_t *config);

    /** Nadanie ramki; msg->format wybiera Classic / FD (ramka FD na szynie Classic jest odrzucana) */
    bool (*transmit)(const CAN_msg_t *msg);

    /** Odbiór ramki ze sprzętowego FIFO; msg->format odpowiada faktycznie odebranej ramce */
    bool (*receive)(CAN_msg_t *msg);

    /** Podpięcie callbacku powiadomienia z przerwania ISR */
    void (*attach_rx_callback)(can_rx_notify_fn cb, void* context);

    /** Konfiguracja sprzętowych filtrów akceptacji */
    bool (*set_filter)(const can_filter_t *filters, uint32_t num_filters);

    /** Stan magistrali (opcjonalny, może być NULL): TEC/REC, error passive / bus-off, LEC/DLEC,
     *  TDCV, licznik błędów protokołu narastający od startu oraz błędy per typ (fazy arbitrażu / danych) */
    bool (*get_status)(can_bus_status_t *status);
} can_interface_t;

#endif // CAN_PORT_H
```

---

## 4. Krok 2: Adapter Sprzętowy (`stm32c5_can_adapter.h` i `.c`)

Pliki: `app/port/stm32c5/stm32c5_can_adapter.h` oraz `app/port/stm32c5/stm32c5_can_adapter.c`

### Rola adaptera:
* Łączy abstrakcję `can_interface_t` z biblioteką STM32 HAL dla konkretnej rodziny (w tym przypadku STM32C5xx).
* Mapuje bity ID (Standard 11-bit / Extended 29-bit) oraz konwertuje długość w bajtach na kody DLC kontrolera FDCAN.
* Obsługuje sprzętowy callback przerwania `HAL_FDCAN_RxFifo0Callback()` i powiadamia menedżera.
* **Udostępnia interfejs poprzez funkcję getter `stm32c5_can1_interfaceGet()`**.

### 4.1 Plik nagłówkowy `stm32c5_can_adapter.h`:
```c
#ifndef STM32C5_CAN_ADAPTER_H
#define STM32C5_CAN_ADAPTER_H

#include "can_port.h"

/**
 * @brief Pobiera wskaźnik do stałego interfejsu sprzętowego CAN1 dla STM32C5.
 * @return Wskaźnik do struktury can_interface_t.
 */
const can_interface_t *stm32c5_can1_interfaceGet(void);

#endif /* STM32C5_CAN_ADAPTER_H */
```

### 4.2 Plik implementacji `stm32c5_can_adapter.c`:
Kluczowe fragmenty implementacji:

1. **Kontekst callbacku:**
```c
static can_rx_notify_fn s_can1_rx_cb = NULL;
static void* s_can1_rx_context = NULL;
```

2. **Inicjalizacja i konfiguracja trybu (`init`):**

`stm32c5_can_init_generic()` zatrzymuje peryferium, wylicza bit timing, ustawia format ramki
(`CAN_MODE_*` → `HAL_FDCAN_FRAME_FORMAT_*`) i ponownie startuje FDCAN. Konfiguracja jest
zapamiętywana w adapterze dopiero po poprawnym zastosowaniu.

**Kalkulator bit timingu (`stm32c5_calc_bit_timing`)** zamiast tablic:
* Zegar kwantu czasu: `HAL_FDCAN_GetClockFreq()` / dzielnik `CKDIV` (obecnie PCLK1 = 144 MHz, `/1`).
* Szuka najmniejszego prescalera (najwyższa rozdzielczość), dla którego prędkość jest **dokładna**,
  liczba tq mieści się w limitach rejestru (NBTP: presc ≤ 512, TSEG1 ≤ 256, TSEG2 ≤ 128;
  DBTP: presc ≤ 32, TSEG1 ≤ 32, TSEG2 ≤ 16), a sample point odbiega o ≤ 3% od zadanego
  (nominal 87.5%, data 75%). SJW = TSEG2.
* W trybie BRS faza nominalna preferuje ten sam prescaler co faza danych (identyczny tq, CiA 601-3).
* `CAN_BAUDRATE_CUSTOM` w `baudrate_normal` zachowuje timing z CubeMX. `CAN_MODE_FD_BRS` wymaga
  jawnego `baudrate_FD`.

Wyniki dla 144 MHz:

| Konfiguracja | Nominal (presc / TSEG1 / TSEG2) | Data (presc / TSEG1 / TSEG2) | TDCO |
|---|---|---|---|
| Classic 125k / 250k / 500k / 1M | 4/251/36, 2/251/36, 1/251/36, 1/125/18 | – | – |
| 500k + 1M | 4 / 62 / 9 | 4 / 26 / 9 (75%) | – |
| 500k + 2M | 2 / 125 / 18 | 2 / 26 / 9 (75%) | 52 |
| 500k + 4M | 1 / 251 / 36 | 1 / 26 / 9 (75%) | 26 |
| 500k + 8M | 1 / 251 / 36 | 1 / 12 / 5 (72.2%) | 12 |
| 5M | niedostępne przy 144 MHz (144/5 nie jest całkowite) | | |

**Transmitter Delay Compensation (TDC):** przy BRS i prescalerze danych ≤ 2 włączana jest
kompensacja opóźnienia transceivera (`TDCO = presc × TSEG1`, filtr 0). Bez niej przy 2–8 Mbps
opóźnienie pętli TX→RX transceivera (MCP2562FD: do ~120 ns) powoduje błędy bitu nadajnika.
TDC musi być włączane **po** `HAL_FDCAN_SetDataBitTiming()`, bo zapis rejestru DBTP kasuje bit `TDC`.

3. **Obsługa nadawania (`transmit`):**
* Format ramki pochodzi z `msg->format`, a nie z konfiguracji szyny. Dzięki temu na szynie FD
  ramki Classic (np. odpowiedzi OTA / ISO-TP) nadal wychodzą jako Classic.
* Ramka FD na szynie `CAN_MODE_CLASSIC` jest odrzucana (`false`).
* BRS jest ustawiany tylko dla ramek FD w trybie `CAN_MODE_FD_BRS`.
* Długości FD > 8 B są kwantowane (np. 10 → 12 B); niewykorzystany ogon jest dopełniany zerami.

4. **Diagnostyka (`get_status`) i przerwanie błędów protokołu:**
* `ECR.CEL` (licznik wzrostów TEC/REC) zeruje się przy odczycie, więc adapter akumuluje go w
  `can_bus_status_t::error_log_cnt` – licznik nie maleje, więc widać także błędy ukryte przez
  automatyczną retransmisję.
* Przerwania `PEA`/`PED` (grupa `HAL_FDCAN_IT_GROUP_PROTOCOL_ERROR`, linia 0) wywołują
  `HAL_FDCAN_ErrorCallback()`, który od razu odczytuje LEC/DLEC (zanim nadpisze je następna ramka) i
  zlicza błędy per typ w `arb_errors[]` / `data_errors[]`.
* `can_manager` co 100 ms (oraz po błędzie TX) sprawdza `bus_off` i restartuje kontroler – M_CAN po
  bus-off pozostaje w trybie INIT, a stan HAL nadal wskazuje ACTIVE.

5. **Obsługa odbioru (`receive`):**
* Wypełnia `ident`, `len`, `data` (do 64 B) oraz `format` na podstawie bitu FDF odebranej ramki.

4. **Callback przerwania z obsługą powiadomienia:**
```c
void HAL_FDCAN_RxFifo0Callback(hal_fdcan_handle_t *hfdcan, uint32_t rx_fifo0_interrupts)
{
    if (hfdcan == mx_fdcan1_gethandle())
    {
        if ((rx_fifo0_interrupts & (FDCAN_IR_RF0N | FDCAN_IR_RF0F)) != 0U)
        {
            if (s_can1_rx_cb != NULL)
            {
                s_can1_rx_cb(s_can1_rx_context);
            }
        }
    }
}
```

5. **Hermetyzacja struktury i funkcja getter:**
```c
// Struktura statyczna, niewidoczna poza tym plikiem jednostki kompilacji
static const can_interface_t s_stm32c5_can1_interface = {
    .init = stm32c5_can1_init,
    .transmit = stm32c5_can1_transmit,
    .receive = stm32c5_can1_receive,
    .attach_rx_callback = stm32c5_can1_attach_rx_callback
};

// Publiczna funkcja getter
const can_interface_t *stm32c5_can1_interfaceGet(void)
{
    return &s_stm32c5_can1_interface;
}
```

---

## 5. Krok 3: Uniwersalny Menedżer CAN (`can_manager.h` i `.c`)

Pliki: `app/can_bus/can_manager.h` oraz `app/can_bus/can_manager.c`

### Rola menedżera:
* Zarządza zadaniami FreeRTOS: zadanie nadawcze (`can_tx_task`) i zdarzeniowe zadanie odbiorcze (`can_rx_task`).
* Zarządza kolejką TX (`tx_queue`) oraz semaforem binarnym RX (`rx_sem`).
* Nie zawiera żadnych odwołań do STM32 HAL! Cała komunikacja ze sprzętem odbywa się przez przekazany wskaźnik `const can_interface_t*`.

### 5.1 Struktura instancji (`can_manager.h`):
```c
typedef struct {
    QueueHandle_t tx_queue;             /**< Kolejka ramek do wysłania */
    SemaphoreHandle_t rx_sem;           /**< Semafor binarny wyzwalany z przerwania */
    TaskHandle_t tx_task_handle;        /**< Uchwyt zadania TX */
    TaskHandle_t rx_task_handle;        /**< Uchwyt zadania RX */
    const can_interface_t *hw_interface;/**< Wstrzyknięty interfejs sprzętowy */
    char name[16];                      /**< Czytelna nazwa szyny, np. "CAN1" */
    can_baudrate_t baudrate;            /**< Wybrana prędkość transmisji */
} can_instance_t;
```

### 5.2 Callback z przerwania ISR:
```c
static void on_rx_isr(void *context)
{
    can_instance_t *inst = (can_instance_t *)context;
    if (inst != NULL && inst->rx_sem != NULL)
    {
        BaseType_t higher_priority_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(inst->rx_sem, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}
```

### 5.3 Zdarzeniowe zadanie odbiorcze (`can_rx_task`):
Task nie zużywa czasu CPU, dopóki przerwanie sprzętowe nie odda semafora:
```c
static void can_rx_task(void *arg)
{
    can_instance_t *inst = (can_instance_t *)arg;

    while (1)
    {
        // Oczekiwanie na semafor odblokowywany przez przerwanie sprzętowe
        if (xSemaphoreTake(inst->rx_sem, portMAX_DELAY) == pdTRUE)
        {
            can_msg_t rx_msg;
            // Opróżniamy wszystkie oczekujące ramki z FIFO
            while (inst->iface->receive(&rx_msg.id, rx_msg.data, &rx_msg.len))
            {
                // Przekazanie ramki do kolejki aplikacji lub dispatchera
                if (inst->rx_queue != NULL)
                {
                    (void)xQueueSend(inst->rx_queue, &rx_msg, 0);
                }
            }
        }
    }
}
```

---

## 6. Krok 4: Konfiguracja FreeRTOS i Przerwań NVIC

> [!CRITICAL]
> **Priorytety Przerwań NVIC w FreeRTOS (Cortex-M):**
> Wszystkie przerwania peryferyjne, które wywołują jakiekolwiek funkcje API FreeRTOS (np. `xSemaphoreGiveFromISR`, `xQueueSendFromISR`), **MUSZĄ mieć priorytet numerycznie większy lub równy `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`**!
>
> W naszym projekcie w `FreeRTOSConfig.h`:
> `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` wynosi **5**.

Jeżeli przerwanie ma priorytet 0 (najwyższy priorytet sprzętowy), próba wywołania `xSemaphoreGiveFromISR()` spowoduje natychmiastowe zablokowanie programu w `configASSERT`!

### Konfiguracja w `generated/hal/mx_fdcan1.c`:
Upewnij się, że przerwanie `FDCAN1_IT0_IRQn` ma ustawiony właściwy priorytet:
```c
HAL_CORTEX_NVIC_SetPriority(FDCAN1_IT0_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, 0);
HAL_CORTEX_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
```

### Oczyszczenie `mx_freertos_app.c`:
Z wygenerowanych przez CubeMX funkcji zadań `can1_tx_task` i `can1_rx_task` w pliku `generated/middleware/mx_freertos_app.c` usuwamy całą dawną logikę testową/biznesową. Zadania są teraz tworzone i zarządzane dynamicznie przez `can_manager_init()`.

---

## 7. Krok 5: Rejestracja w `CMakeLists.txt`

Dodając nowy moduł portu lub managera do projektu, należy zaktualizować główny plik `CMakeLists.txt`:

1. **Rejestracja plików źródłowych `.c`:**
```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # ...
    app/can_bus/can_manager.c
    app/port/stm32c5/stm32c5_can_adapter.c
)
```

2. **Rejestracja ścieżek include:**
```cmake
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # ...
    app/can_bus
    app/port
    app/port/stm32c5
)
```

---

## 8. Krok 6: Inicjalizacja i Uruchomienie w `main.c`

W pliku `main.c`:
1. Dołączamy nagłówek menedżera `can_manager.h` oraz nagłówek adaptera `stm32c5_can_adapter.h`.
2. Alokujemy globalną instancję `can_instance_t can1_inst;`.
3. Przed uruchomieniem schedulera FreeRTOS (`vTaskStartScheduler()`) wywołujemy `can_manager_init()`, przekazując wynik wywołania gettera `stm32c5_can1_interfaceGet()`.

```c
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

#include "can_manager.h"
#include "stm32c5_can_adapter.h"

/* Alokacja instancji dla magistrali CAN1 */
can_instance_t can1_inst;

int main(void)
{
    /* 1. Inicjalizacja sprzętowa HAL i zegarów */
    if (mx_system_init() != SYSTEM_OK)
    {
        return (-1);
    }

    /* 2. Inicjalizacja instancji CAN1 ze wstrzyknięciem adaptera (Classic 500k; docelowy tryb ustawia profil parsera) */
    if (!can_manager_init(&can1_inst, stm32c5_can1_interfaceGet(), "CAN1"))
    {
        return (-1);
    }

    /* 3. Inicjalizacja pozostałych zadań FreeRTOS */
    if (app_synctasks_init() != 0)
    {
        return (-1);
    }

    /* 4. Start planisty FreeRTOS */
    vTaskStartScheduler();

    while (1) {
    }
}
```

### Wysyłanie i odbieranie ramek w aplikacji:
W dowolnym wątku FreeRTOS (np. w `main_task`):
```c
// Przygotowanie ramki
can_msg_t msg = {
    .id = CAN_msg_set_standard_id(0x123),
    .len = 8,
    .data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}
};

// Wysłanie do kolejki z timeoutem
if (can_manager_send(&can1_inst, &msg, pdMS_TO_TICKS(100)))
{
    // Ramka zakolejkowana pomyślnie
}

// Odbiór ramki z kolejki RX z timeoutem
can_msg_t rx_msg;
if (can_manager_read(&can1_inst, &rx_msg, pdMS_TO_TICKS(500)))
{
    // Odebrano nową ramkę
}
```

---

## 9. Krok 7: Dodawanie kolejnej szyny (np. CAN2) lub portowanie na inny MCU

Dzięki zastosowanej architekturze rozszerzenie systemu o drugą szynę CAN jest proste i powtarzalne:

### Przykład: Dodanie szyny CAN2 (dla STM32C5 lub STM32H5)
1. **Utwórz plik adaptera:** `app/port/stm32c5/stm32c5_can2_adapter.c` oraz `.h`.
2. Zaimplementuj funkcje `stm32c5_can2_transmit()`, `stm32c5_can2_receive()` oraz powiąż je z uchwytem `hfdcan2`.
3. Udostępnij getter:
   ```c
   const can_interface_t *stm32c5_can2_interfaceGet(void);
   ```
4. W pliku `main.c`:
   ```c
   can_instance_t can2_inst;
   can_manager_init(&can2_inst, stm32c5_can2_interfaceGet(), "CAN2");
   ```
5. **Żadna linijka w `can_manager.c` nie ulega zmianie!**

### Przykład: Portowanie na inną rodzinę (np. STM32H5)
1. Tworzysz nowy folder: `app/port/stm32h5/`.
2. Tworzysz `stm32h5_can_adapter.c` implementujący `can_interface_t` przy użyciu HAL dla H5.
3. Eksportujesz `stm32h5_can1_interfaceGet()`.
4. W pliku `main.c` wstrzykujesz ten interfejs do `can_manager_init()`.


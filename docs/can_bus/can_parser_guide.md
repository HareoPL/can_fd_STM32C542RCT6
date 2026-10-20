# Przewodnik Tworzenia Nowego Parsera CAN (CAN Parser Creation Guide)

Ten dokument opisuje krok po kroku procedurę tworzenia, implementacji oraz rejestracji nowego profilu parsera wiadomości CAN w architekturze projektu `v-box`.

---

## 1. Architektura i Koncepcja Profilu

System parserów CAN opiera się na **wzorcu profili z funkcją getter** oraz **dynamicznym przełączaniu w locie**:
* **Brak słowa kluczowego `extern`:** Parsery nie eksportują globalnych zmiennych ani tablic. Wszystkie callbacki i tablice filtrów są hermetyzowane jako `static const` w pliku implementacji parsera.
* **Automatyczne zliczanie reguł:** Liczba filtrów jest wyliczana w czasie kompilacji (`sizeof(s_filters) / sizeof(s_filters[0])`), co eliminuje konieczność ręcznego utrzymywania stałych `#define CP_*_COUNT`.
* **Identyfikatory numeryczne (`can_parser_id_t`):** Wybór parsera odbywa się po liczbowym ID, co zapobiega alokacji pamięci na nazwy tekstowe i narzutowi `strcmp`.
* **Kompaktowy dyspozytor:** Dyspozytor `can_parser.c` przechowuje wyłącznie 4-bajtowe wskaźniki na funkcje-gettery profili (`can_profile_getter_fn`).

```mermaid
flowchart TD
    UserApp["Aplikacja (np. main.c lub komenda CLI)"] -->|can_parser_select(inst, ID)| Dispatcher["can_parser.c (Dyspozytor)"]
    Dispatcher -->|s_parsers[ID]| Getter["cp_<nazwa>_profile_get()"]
    Getter -->|Zwraca wskaźnik| Profile["can_parser_profile_t (static const)"]
    Profile -->|filters, num_filters| Manager["can_manager_setFilter()"]
    Manager -->|1. Konfiguracja sprzętu| HW["M_CAN Message RAM (filtry sprzętowe)"]
    Manager -->|2. Rejestracja programowa| RXTask["can_rx_task (dyspozytor programowy cb)"]
```

---

## 2. Struktura Katalogów i Konwencja Nazewnictwa

Wszystkie nowe parsery muszą być umieszczane w dedykowanym podkatalogu parserów magistrali CAN:
```text
app/can_bus/can_parser/parsers/
├── cp_v_box.h / .c          # Profil produkcyjny: Telemetria i przekaźnik V-Box
├── cp_example.h / .c        # Profil demonstracyjny: Diagnostyka UDS/OBD2 i kalibracja
└── cp_<nazwa>.h / .c        # Nowy parser (konwencja cp_*.h / cp_*.c)
```

* **Lokalizacja:** `app/can_bus/can_parser/parsers/`
* **Prefiks pliku:** `cp_` (skrót od *CAN Parser*), np. `cp_battery.c`, `cp_j1939.c`.

---

## 3. Szybkie Tworzenie: Generator Python (`tools/create_parser.py`)

Do projektu dołączone jest narzędzie automatyzujące tworzenie szkieletu nowego parsera:
```bash
python tools/create_parser.py <nazwa_parsera>
```

*Przykład:*
```bash
python tools/create_parser.py battery
```

**Co wykonuje generator automatycznie:**
1. Weryfikuje unikalność nazwy i brak konfliktów w plikach i enumach.
2. Tworzy `app/can_bus/can_parser/parsers/cp_battery.h` z prototypem gettera profilu.
3. Tworzy `app/can_bus/can_parser/parsers/cp_battery.c` z gotowym szkieletem ID, callbacków, tablicy filtrów i instrukcjami TODO.
4. Dopisuje `CAN_PARSER_ID_BATTERY` do `can_parser_id_t` w `can_parser.h`.
5. Dopisuje `#include "cp_battery.h"` oraz rejestruje wpis w `s_parsers` w `can_parser.c`.
6. Dopisuje `app/can_bus/can_parser/parsers/cp_battery.c` do `target_sources()` w `CMakeLists.txt`.

*Opcje narzędzia:*
* `--dry-run` – symulacja bez dokonywania zmian na dysku: `python tools/create_parser.py bms --dry-run`

---

## 4. Ręczna Procedura Krok po Kroku (Referencja)

### Krok 1: Utworzenie pliku nagłówkowego `cp_<nazwa>.h`

W katalogu `app/can_bus/can_parser/parsers/` utwórz plik `cp_<nazwa>.h`.
Plik nagłówkowy musi być minimalistyczny – włącza `can_parser.h` i deklaruje wyłącznie prototyp funkcji getter zwracającej profil:

```c
/**
 * @file cp_<nazwa>.h
 * @brief CAN parser profile: <Krótki opis, np. Battery Management System handler>.
 * @author Twoje Imie i Nazwisko <twoj.email@example.com>
 * @date 2026-10-13
 */

#ifndef CP_<NAZWA>_H
#define CP_<NAZWA>_H

#include "can_parser.h"

/**
 * @brief Returns the <nazwa> parser profile descriptor.
 * @return Pointer to static immutable profile structure.
 */
const can_parser_profile_t *cp_<nazwa>_profile_get(void);

#endif /* CP_<NAZWA>_H */
```

---

### Krok 2: Utworzenie pliku implementacji `cp_<nazwa>.c`

W katalogu `app/can_bus/can_parser/parsers/` utwórz plik `cp_<nazwa>.c`.
Zaimplementuj callbacki odbiorcze, statyczną tablicę filtrów oraz funkcję getter:

```c
/**
 * @file cp_<nazwa>.c
 * @brief Implementation of CAN parser profile: <Opis>.
 * @author Twoje Imie i Nazwisko <twoj.email@example.com>
 * @date 2026-10-13
 */

#include "cp_<nazwa>.h"
#include "safe_printf.h"
#include "can_msg_types.h"
#include <stddef.h>

/** @brief Logging tag for this parser profile. */
#define TAG "CP_<NAZWA_UPPER>"

/** @brief Example standard 11-bit CAN ID. */
#define CAN_ID_STATUS       0x120U

/** @brief Example extended 29-bit CAN ID. */
#define CAN_ID_EXT_DATA     0x18FF5021U

/* --- Private Callbacks -----------------------------------------------------*/

/**
 * @brief Handler callback for incoming status frame (Standard ID: 0x120).
 * @param[in] msg Pointer to received CAN message.
 */
static void on_status_rx(const CAN_msg_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    /* Przetwarzanie danych ramki msg->data... */
    safe_printf(TAG, "Status frame received (len=%u)\r\n", (unsigned int)msg->len);
}

/**
 * @brief Handler callback for incoming extended frame (Extended ID: 0x18FF5021).
 * @param[in] msg Pointer to received CAN message.
 */
static void on_ext_data_rx(const CAN_msg_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    safe_printf(TAG, "Extended frame received (ID=0x%08lX)\r\n",
                (unsigned long)CAN_msg_get_can_id(msg->ident));
}

/* --- Private Profile Definition --------------------------------------------*/

/** @brief Internal filter table for this profile. */
static const can_filter_t s_filters[] = {
    {
        .arb  = CAN_ID_STATUS,              /**< 11-bit Standard ID */
        .mask = CAN_MASK_EXACT_STANDARD,    /**< Exact match mask */
        .cb   = on_status_rx
    },
    {
        .arb  = CAN_ID_EXT_DATA,            /**< 29-bit Extended ID */
        .mask = CAN_MASK_EXACT_EXTENDED,    /**< Exact match mask */
        .cb   = on_ext_data_rx
    }
};

/** @brief Immutable profile descriptor. */
static const can_parser_profile_t s_profile = {
    .filters     = s_filters,
    .num_filters = (uint8_t)(sizeof(s_filters) / sizeof(s_filters[0]))
};

/* --- Public API ------------------------------------------------------------*/

const can_parser_profile_t *cp_<nazwa>_profile_get(void)
{
    return &s_profile;
}
```

> [!IMPORTANT]
> **Maski filtrów:**
> - `CAN_MASK_EXACT_STANDARD` (`0x000007FFU`): Dokładne dopasowanie identyfikatora standardowego 11-bit.
> - `CAN_MASK_EXACT_EXTENDED` (`0x1FFFFFFFU`): Dokładne dopasowanie identyfikatora rozszerzonego 29-bit.
> - `CAN_MASK_ALL` (`0x00000000U`): Akceptacja dowolnego identyfikatora (wszystkie bity *don't care*).

---

### Krok 3: Rejestracja nowego ID profilu w `can_parser.h`

Otwórz plik `app/can_bus/can_parser/can_parser.h` i dopisz nowy identyfikator w enumie `can_parser_id_t` (przed `CAN_PARSER_COUNT`):

```c
typedef enum
{
    CAN_PARSER_ID_V_BOX = 0, /**< Parser Profile: V-Box telemetry & relay */
    CAN_PARSER_ID_EXAMPLE,   /**< Parser Profile: Example diagnostic & calibration */
    CAN_PARSER_ID_MY_CUSTOM, /**< Parser Profile: <Twój nowy parser> */
    CAN_PARSER_COUNT
} can_parser_id_t;
```

---

### Krok 4: Rejestracja w dyspozytorze `can_parser.c`

Otwórz plik `app/can_bus/can_parser/can_parser.c`:
1. Dołącz nagłówek nowego parsera:
   ```c
   #include "cp_<nazwa>.h"
   ```
2. Zarejestruj funkcję-getter w tablicy `s_parsers`:
   ```c
   static const can_profile_getter_fn s_parsers[CAN_PARSER_COUNT] = {
       [CAN_PARSER_ID_V_BOX]     = cp_v_box_profile_get,
       [CAN_PARSER_ID_EXAMPLE]   = cp_example_profile_get,
       [CAN_PARSER_ID_MY_CUSTOM] = cp_<nazwa>_profile_get,
   };
   ```

---

### Krok 5: Rejestracja pliku źródłowego w `CMakeLists.txt`

Otwórz plik `CMakeLists.txt` w katalogu głównym projektu i dopisz nowo utworzony plik `.c` do sekcji `target_sources()`:

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  ...
  app/can_bus/can_parser/parsers/cp_<nazwa>.c
  ...
)
```

*(Folder `app/can_bus/can_parser/parsers` jest już zarejestrowany w `target_include_directories()`, więc nie trzeba dopisywać ścieżki include).*

---

### Krok 6: Aktywacja lub przełączenie profilu w aplikacji

Aby aktywować profil na danej instancji CAN (np. `can1_inst`), wywołaj funkcję `can_parser_select()`:

```c
#include "can_parser.h"

/* Aktywacja w main.c przy starcie systemu */
if (!can_parser_select(&can1_inst, CAN_PARSER_ID_2))
{
    /* Obsługa błędu konfiguracji filtrów */
}
```

Profil można również przełączać **dynamicznie w locie** w dowolnym momencie działania programu (np. po odebraniu dedykowanej ramki CAN, komendy UART lub polecenia CLI). Funkcja `can_parser_select()` zatrzyma kontroler FDCAN, przekonfiguruje filtry sprzętowe M_CAN oraz tablicę dyspozytora programowego, a następnie wznowi pracę magistrali.

### Krok 6b: Wyjątek kompozytowy – dołączenie `CAN_PARSER_ID_BOOT_OTA` (`can_parser_attach_boot_ota`)

Z założenia na jednej instancji CAN aktywny jest jeden profil aplikacyjny wybierany przez `can_parser_select()`. Jedynym architektonicznym wyjątkiem (przeznaczonym dla płytek niewyposażonych w transceiver `CAN2`) jest możliwość dołączenia filtrów aktualizacji `CAN_PARSER_ID_BOOT_OTA` (`0x01F2F3DF`, `0x01F2F3E0`) do aktualnie aktywnego profilu na `CAN1`:

```c
/* 1. Wybór głównego profilu aplikacyjnego na CAN1 */
can_parser_select(&can1_inst, CAN_PARSER_ID_V_BOX);

/* 2. Dołączenie obsługi Bootloadera / OTA równolegle na CAN1 */
can_parser_attach_boot_ota(&can1_inst);
```

---

## 4. Zasady Dobrej Praktyki

1. **Brak operacji blokujących w callbackach:** Callbacki `cb(msg)` są wywoływane w kontekście zadania FreeRTOS `can_rx_task`. Nie wolno w nich wywoływać `vTaskDelay()`, blokujących operacji wejścia/wyjścia ani czekać na nieskończone semafory.
2. **Komentarze wyłącznie w języku angielskim:** Zgodnie z punktem 10 `GEMINI.md`, wszelkie komentarze i dokumentacja kodu (Doxygen) muszą być pisane po angielsku.
3. **Hermetyzacja zmiennych:** Nigdy nie udostępniaj buforów parsera ani zmiennych stanu przez zmienne globalne. Zawsze stosuj słowo kluczowe `static`.


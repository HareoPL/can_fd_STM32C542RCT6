# Raport GPIO dla płytki NUCLEO-C542RC (MB2213)

Dokument zawiera przypisanie wyprowadzeń (GPIO) mikrokontrolera **STM32C542RCT6** (obudowa LQFP64) na płytce Nucleo-64 (MB2213, rewizja B-02) oraz konfigurację pinów w projekcie **V-BOX**.

Źródło numeracji złączy: [UM3615](pdf/um3615-stm32c5-nucleo64-board-mb2213-stmicroelectronics.pdf) (Table 13–17: złącza ARDUINO® CN5/CN8/CN9 i ST morpho CN7/CN10, Table 18: mostki lutownicze) oraz [schemat MB2213](pdf/mb2213-c542rc-b02-schematic.pdf). „–” w kolumnie złącza oznacza, że pin nie jest na nie wyprowadzony.

---

## 1. Piny zablokowane i zarezerwowane (System / Zasilanie / Zegary)
Te piny pełnią funkcje krytyczne dla działania mikrokontrolera i płytki lub są sprzętowo połączone z elementami zasilania i oscylatorami:

| Pin | Funkcja domyślna na Nucleo | Złącze ST-Morpho | Ograniczenia / Opis |
| :--- | :--- | :---: | :--- |
| **PH0** | `OSC_IN` | CN7 pin 29 | Podłączony do kwarcu HSE 24 MHz (X3). Niedostępny jako GPIO. |
| **PH1** | `OSC_OUT` | CN7 pin 31 | Podłączony do kwarcu HSE 24 MHz (X3). Niedostępny jako GPIO. |
| **PC14**| `OSC32_IN` | CN7 pin 25 | Podłączony do kwarcu LSE 32.768 kHz (X2). Niedostępny jako GPIO. |
| **PC15**| `OSC32_OUT` | CN7 pin 27 | Podłączony do kwarcu LSE 32.768 kHz (X2). Niedostępny jako GPIO. |
| **PH2** | `BOOT0` | CN7 pin 7 | Podłączony do układu wyboru trybu bootowania (przycisk B3). |
| **NRST** | `NRST` | CN7 pin 14 | Sprzętowy reset mikrokontrolera (przycisk B2 / programator ST-LINK). |
| Różne | `VDD`, `VSS`, `VCAP`, `VBAT`, `VREFP` | CN7 / CN10 | Szyny zasilania, filtracji i napięcia referencyjnego ADC. |

---

## 2. Piny zarezerwowane przez zintegrowane peryferia Nucleo
Piny podłączone do wbudowanych układów płytki Nucleo (debugera ST-LINK, portu USB-C, diod LED i przycisków pokładowych):

| Pin | Funkcja pokładowa | Złącze Arduino | Złącze ST-Morpho | Opis / Uwagi |
| :--- | :--- | :---: | :---: | :--- |
| **PA13**| `DBG_SWDIO` | - | CN7 pin 13 | Interfejs programowania i debugowania SWD (ST-LINK). Nie modyfikować. |
| **PA14**| `DBG_SWCLK` | - | CN7 pin 15 | Zegar debugera SWD (ST-LINK). Nie modyfikować. |
| **PB3** | `DBG_SWO` | D3 | CN10 pin 31 | Śledzenie (Trace SWO) połączone ze ST-LINK. |
| **PA2** | `VCP_TX` | - | CN7 pin 9 | Virtual COM Port - nadawanie UART (USART2_TX) do ST-LINK. |
| **PA3** | `VCP_RX` | - | CN7 pin 11 | Virtual COM Port - odbiór UART (USART2_RX) ze ST-LINK. |
| **PA5** | `USER_LED (LD1)` | D13 | CN10 pin 11 | Wbudowana zielona dioda LED użytkownika na płytce. |
| **PC13**| `USER_BUTTON (B1)` | - | CN7 pin 23 | Wbudowany niebieski przycisk użytkownika ze sprzętowym filtrem RC. |
| **PA11**| `USB_FS_N` | - | CN10 pin 14 | Linia danych D- wbudowanego złącza USB Typu-C. |
| **PA12**| `USB_FS_P` | - | CN10 pin 12 | Linia danych D+ wbudowanego złącza USB Typu-C. |
| **PC4** | `VBUS_DETECT` | - | CN10 pin 34 | Sprzętowa detekcja zasilania 5V ze złącza USB Type-C. |

---

## 3. Magistrale komunikacyjne CAN (FDCAN1 i FDCAN2)

### 3.1. FDCAN1 (wbudowany transceiver pokładowy U7)
Magistrala CAN1 korzysta z wbudowanego na płytce Nucleo transceivera **Microchip MCP2562FD** (układ U7). Piny są połączone z transceiverem mostkami SB36 / SB34 / SB32 (stan domyślny: ON):

| Pin | Funkcja CAN | Złącze Arduino | Złącze ST-Morpho | Opis sprzętowy |
| :--- | :--- | :---: | :---: | :--- |
| **PB9** | `FDCAN1_TX` | - | - | Linia nadawcza TX połączona z transceiverem U7 (SB34). |
| **PB8** | `FDCAN1_RX` | - | - | Linia odbiorcza RX połączona z transceiverem U7 (SB36). |
| **PE2** | `FDCAN1_STBY` | - | CN7 pin 21 | Sterowanie trybem Standby transceivera pokładowego (niski = aktywny, SB32). |

### 3.2. FDCAN2 (zewnętrzny transceiver)
Magistrala CAN2 wymaga zewnętrznego transceivera CAN podłączonego do złącza CN10:

| Pin | Funkcja CAN2 | Złącze Arduino | Złącze ST-Morpho | Status i przeznaczenie |
| :--- | :--- | :---: | :---: | :--- |
| **PB5** | `FDCAN2_RX` | - | **CN10 pin 6** | **FDCAN2 RX** *(funkcja alternatywna AF9)*, wejście RXD z transceivera. |
| **PB6** | `FDCAN2_TX` | D15 | **CN10 pin 3** | **FDCAN2 TX** *(funkcja alternatywna AF9)*, wyjście TXD do transceivera. |

---

## 4. Przypisanie pinów w aplikacji V-BOX

### 4.1. Wejścia cyfrowe (Przełączniki / Krańcówki SW0..SW8)
Wejścia skonfigurowane w trybie `INPUT` z wewnętrznymi rezystorami pull-down (`HAL_GPIO_PULL_DOWN`). Stan spoczynkowy 0 (otwarty/niepodłączony), naciśnięcie/zwarcie podaje +3.3 V:

| Wejście | Bit w masce | Pin MCU | Złącze Arduino | Złącze ST-Morpho | Funkcja w pojeździe / Uwagi |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **SW0** | Bit 0 | **PB4** | D5 | **CN10 pin 27** | **ACC stacyjka** |
| **SW1** | Bit 1 | **PC8** | - | **CN10 pin 2** | **Drzwi INNE** |
| **SW2** | Bit 2 | **PC7** | - | **CN10 pin 4** | **Drzwi KIER** |
| **SW3** | Bit 3 | **PB7** | D14 | **CN10 pin 5** | **Światła** |
| **SW4** | Bit 4 | **PB12** | - | **CN10 pin 16** | **Zamki ZAMKNIĘTE** *(Fizyczny feedback położenia siłownika/przekaźnika)* |
| **SW5** | Bit 5 | **PB13** | - | **CN10 pin 30** | **Awaryjne** |
| **SW6** | Bit 6 | **PB14** | D1 ⁽¹⁾ | **CN10 pin 28** ⁽¹⁾ | **Maska** |
| **SW7** | Bit 7 | **PB15** | D0 ⁽¹⁾ | **CN10 pin 26** ⁽¹⁾ | **Hamulec RĘCZNY** |
| **SW8** | Bit 8 | **PB2** | - | **CN10 pin 22** | **Obroty silnika** |

⁽¹⁾ PB14 / PB15 są wyprowadzone na CN10 pin 28 / 26. Przy mostkach SB48 / SB49 ON są również na Arduino D1 / D0 i na CN10 pin 35 / 37. Nie podłączać sygnałów jednocześnie do obu wyprowadzeń.

### 4.2. Wyjścia cyfrowe (Aktuatory / Sterowanie)
| Wyjście | Pin MCU | Złącze Arduino | Złącze ST-Morpho | Funkcja / Opis |
| :--- | :---: | :---: | :---: | :--- |
| **RELAY_CTRL** | **PA8** | D7 | **CN10 pin 23** | Sterowanie przekaźnikiem bistabilnym/zamkiem za pomocą zboczy (zbocze narastające = ZAMKNIJ, zbocze opadające = OTWÓRZ). |

### 4.3. Wejścia analogowe (Pomiary ADC)
| Sygnał | Pin MCU | Złącze Arduino | Złącze ST-Morpho | Funkcja / Opis |
| :--- | :---: | :---: | :---: | :--- |
| **ADC_IN** | **PA0** | A0 | **CN7 pin 28** | Poziom paliwa: pomiar napięcia potencjometru przez 12-bitowy przetwornik ADC1 (`bsp_adc`). |

### 4.4. Port szeregowy USART1
USART1 w trybie asynchronicznym, 57600 baud, 8N1, bez kontroli przepływu (funkcja alternatywna AF7). Odbiór przerwaniowy przez adapter `stm32c5_uart1_adapter` (interfejs `uart_port.h`). Port obsługuje binarny protokół RS485; transceiver przełącza DE/RE automatycznie, więc nie jest potrzebny dodatkowy pin kierunku. Piny idą bezpośrednio na złącza, bez mostków lutowniczych:

| Sygnał | Pin MCU | Złącze Arduino | Złącze ST-Morpho | Funkcja / Opis |
| :--- | :---: | :---: | :---: | :--- |
| **USART1_TX** | **PA6** | D12 | **CN10 pin 13** | Nadawanie UART (wyjście TX do RX urządzenia zewnętrznego). |
| **USART1_RX** | **PA7** | D11 | **CN10 pin 15** | Odbiór UART (wejście RX z TX urządzenia zewnętrznego). |

---

## 5. Piny wolne do wykorzystania (Niewykorzystane w aplikacji)

Poniższe wyprowadzenia mikrokontrolera nie są zajęte przez logikę aplikacji ani peryferia płytki Nucleo i mogą zostać swobodnie użyte:

### Port A
* **PA1** (CN7 pin 30 / Arduino A1) – wolne wejście ADC/GPIO.
* **PA4** (CN7 pin 32 / Arduino A2) – wolne wejście ADC / wyjście DAC.
* **PA9** (CN10 pin 21 / Arduino D8) – wolne GPIO.
* **PA10** (CN10 pin 33 / Arduino D2) – wolne GPIO.
* **PA15** (CN7 pin 17) – wolne GPIO (lub JTDI).

### Port B
* **PB0** (CN7 pin 34 / Arduino A3) – wolne wejście ADC/GPIO.
* **PB1** (CN10 pin 18) – wolne GPIO.
* **PB10** (CN10 pin 25 / Arduino D6) – wolne GPIO (lub TIM2_CH3).

### Port C
* **PC0** (CN7 pin 38 / Arduino A5) – wolne GPIO/ADC.
* **PC1** (CN10 pin 29 / Arduino D4) – wolne GPIO.
* **PC2** (CN7 pin 35) – wolne GPIO/ADC.
* **PC3** (CN7 pin 37) – wolne GPIO/ADC.
* **PC5** (CN7 pin 36 / Arduino A4) – wolne GPIO/ADC.
* **PC6** (CN10 pin 19 / Arduino D9) – wolne GPIO (lub TIMx_CHy).
* **PC9** (CN10 pin 17 / Arduino D10) – wolne GPIO.
* **PC10** (CN7 pin 1) – wolne GPIO.
* **PC11** (CN7 pin 2) – wolne GPIO.
* **PC12** (CN7 pin 3) – wolne GPIO.

### Port D
* **PD2** (CN7 pin 4) – wolne GPIO.

---

## 6. Szkic fizyczny złącza ST-Morpho CN10

```text
               ZŁĄCZE ST-MORPHO CN10 (Prawa krawędź Nucleo-64)

        Wewnętrzny rząd (Odd)                 Zewnętrzny rząd (Even)
        ---------------------                 ----------------------
Rząd 1: CN10 pin 1  -> NC                |    CN10 pin 2  -> PC8  [Drzwi INNE - SW1]
Rząd 2: CN10 pin 3  -> PB6  [CAN2_TX]    |    CN10 pin 4  -> PC7  [Drzwi KIER - SW2]
Rząd 3: CN10 pin 5  -> PB7  [Światła-SW3]|    CN10 pin 6  -> PB5  [CAN2_RX]
Rząd 4: CN10 pin 7  -> VREFP (AREF)      |    CN10 pin 8  -> 5V_STLK
Rząd 5: CN10 pin 9  -> GND               |    ...
Rząd 6: CN10 pin 11 -> PA5  [LD1]        |    CN10 pin 12 -> PA12 [USB_DP]
Rząd 7: CN10 pin 13 -> PA6  [USART1_TX]  |    CN10 pin 14 -> PA11 [USB_DM]
Rząd 8: CN10 pin 15 -> PA7  [USART1_RX]  |    CN10 pin 16 -> PB12 [Zamki ZAMKNIĘTE - SW4]
Rząd 9: CN10 pin 17 -> PC9  [Wolny]      |    CN10 pin 18 -> PB1  [Wolny]
Rząd 10:CN10 pin 19 -> PC6  [Wolny]      |    ...
Rząd 11:CN10 pin 21 -> PA9  [Wolny]      |    CN10 pin 22 -> PB2  [Obroty silnika - SW8]
Rząd 12:CN10 pin 23 -> PA8  [RELAY]      |    ...
Rząd 13:CN10 pin 25 -> PB10 [Wolny]      |    CN10 pin 26 -> PB15 [Hamulec RĘCZNY - SW7]
Rząd 14:CN10 pin 27 -> PB4  [ACC - SW0]  |    CN10 pin 28 -> PB14 [Maska - SW6]
Rząd 15:CN10 pin 29 -> PC1  [Wolny]      |    CN10 pin 30 -> PB13 [Awaryjne - SW5]
Rząd 16:CN10 pin 31 -> PB3  [SWO]        |    CN10 pin 32 -> AGND
Rząd 17:CN10 pin 33 -> PA10 [Wolny]      |    CN10 pin 34 -> PC4  [VBUS_DETECT]
Rząd 18:CN10 pin 35 -> PB14 (SB48)       |    CN10 pin 36 -> NC
Rząd 19:CN10 pin 37 -> PB15 (SB49)       |    CN10 pin 38 -> NC
```

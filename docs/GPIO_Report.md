# Raport GPIO dla płytki NUCLEO-C542RC (MB2213)

Dokument ten zawiera kompleksową analizę wyprowadzeń (GPIO) mikrokontrolera **STM32C542RCT6** (obudowa LQFP64) na płytce Nucleo-64 (MB2213, rewizja B-02) wraz z aktualną konfiguracją pinów w projekcie **V-BOX**.

---

## 1. Piny zablokowane i zarezerwowane (System / Zasilanie / Zegary)
Te piny pełnią funkcje krytyczne dla działania mikrokontrolera i płytki lub są sprzętowo połączone z elementami zasilania i oscylatorami:

| Pin | Funkcja domyślna na Nucleo | Złącze ST-Morpho | Ograniczenia / Opis |
| :--- | :--- | :---: | :--- |
| **PH0** | `OSC_IN` | CN7 pin 29 | Podłączony do kwarcu HSE 24 MHz (X3). Niedostępny jako GPIO. |
| **PH1** | `OSC_OUT` | CN7 pin 31 | Podłączony do kwarcu HSE 24 MHz (X3). Niedostępny jako GPIO. |
| **PC14**| `OSC32_IN` | CN7 pin 25 | Podłączony do kwarcu LSE 32.768 kHz (X2). Niedostępny jako GPIO. |
| **PC15**| `OSC32_OUT` | CN7 pin 27 | Podłączony do kwarcu LSE 32.768 kHz (X2). Niedostępny jako GPIO. |
| **PH2** | `BOOT0` | CN7 pin 7 | Podłączony do układu wyboru trybu bootowania (pin BOOT). |
| **Pin 7** | `NRST` | CN7 pin 14 | Sprzętowy reset mikrokontrolera (przycisk B2 / programator ST-LINK). |
| Różne | `VDD`, `VSS`, `VCAP`, `VBAT`, `VREF+` | CN7 / CN10 | Szyny zasilania, filtracji i napięcia referencyjnego ADC. |

---

## 2. Piny zarezerwowane przez zintegrowane peryferia Nucleo
Piny podłączone do wbudowanych układów płytki Nucleo (debugera ST-LINK, portu USB-C, diod LED i przycisków pokładowych):

| Pin | Funkcja pokładowa | Złącze Arduino | Złącze ST-Morpho | Opis / Uwagi |
| :--- | :--- | :---: | :---: | :--- |
| **PA13**| `DBG_SWDIO` | - | CN7 pin 13 | Interfejs programowania i debugowania SWD (ST-LINK). Nie modyfikować. |
| **PA14**| `DBG_SWCLK` | - | CN7 pin 15 | Zegar debugera SWD (ST-LINK). Nie modyfikować. |
| **PB3** | `DBG_SWO` | D3 | CN10 pin 31 | Śledzenie (Trace SWO) połączone ze ST-LINK. |
| **PA2** | `VCP_TX` | D1 (TX) | CN7 pin 9 | Virtual COM Port - nadawanie UART (USART2_TX) do ST-LINK. |
| **PA3** | `VCP_RX` | D0 (RX) | CN7 pin 11 | Virtual COM Port - odbiór UART (USART2_RX) ze ST-LINK. |
| **PA5** | `USER_LED (LD1)` | D13 | CN10 pin 11 | Wbudowana zielona dioda LED użytkownika na płytce. |
| **PC13**| `USER_BUTTON (B1)` | - | CN7 pin 23 | Wbudowany niebieski przycisk użytkownika ze sprzętowym filtrem RC. |
| **PA11**| `USB_FS_N` | - | CN10 pin 12 | Linia danych D- wbudowanego złącza USB Typu-C. |
| **PA12**| `USB_FS_P` | - | CN10 pin 10 | Linia danych D+ wbudowanego złącza USB Typu-C. |
| **PC4** | `VBUS_DETECT` | - | CN10 pin 34 | Sprzętowa detekcja zasilania 5V ze złącza USB Type-C. |

---

## 3. Magistrale komunikacyjne CAN (FDCAN1 i FDCAN2)

### 3.1. FDCAN1 (Magistrala aktywna – wbudowany transceiver pokładowy U7)
Magistrala CAN1 korzysta z wbudowanego na płytce Nucleo transceivera **Microchip MCP2562FD** (układ U7):

| Pin | Funkcja CAN | Złącze Arduino | Złącze ST-Morpho | Opis sprzętowy |
| :--- | :--- | :---: | :---: | :--- |
| **PB9** | `FDCAN1_TX` | D14 | CN10 pin 33 | Linia nadawcza TX połączona bezpośrednio z transceiverem U7. |
| **PB8** | `FDCAN1_RX` | D15 | CN10 pin 35 | Linia odbiorcza RX połączona bezpośrednio z transceiverem U7. |
| **PE2** | `FDCAN1_STBY` | - | CN7 pin 21 | Sterowanie trybem Standby transceivera pokładowego (niski = aktywny). |

### 3.2. FDCAN2 (Magistrala przyszłościowa – piny zarezerwowane)
Piny te zostały celowo zwolnione w logice wejść dyskretnych aplikacji, aby umożliwić bezkolizyjne podłączenie drugiego transceivera CAN (FDCAN2) w przyszłości:

| Pin | Funkcja CAN2 | Złącze Arduino | Złącze ST-Morpho | Status i przeznaczenie |
| :--- | :--- | :---: | :---: | :--- |
| **PB5** | `FDCAN2_RX` | D4 | **CN10 pin 6** | **Zarezerwowany dla FDCAN2 RX** *(funkcja alternatywna AF9)*. Zwolniony z dawnej roli wejścia krańcówki Drzwi KIER. |
| **PB6** | `FDCAN2_TX` | D10 | **CN10 pin 3** | **Zarezerwowany dla FDCAN2 TX** *(funkcja alternatywna AF9)*. Zwolniony z dawnej roli wejścia krańcówki Drzwi INNE. |

---

## 4. Przypisanie pinów w aplikacji V-BOX

### 4.1. Wejścia cyfrowe (Przełączniki / Krańcówki SW0..SW8)
Wejścia skonfigurowane w trybie `INPUT` z wewnętrznymi rezystorami pull-down (`HAL_GPIO_PULL_DOWN`). Stan spoczynkowy 0 (otwarty/niepodłączony), naciśnięcie/zwarcie podaje +3.3 V:

| Wejście | Bit w masce | Pin MCU | Złącze Arduino | Złącze ST-Morpho | Funkcja w pojeździe / Uwagi |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **SW0** | Bit 0 | **PB4** | D5 | **CN10 pin 27** | **ACC stacyjka** |
| **SW1** | Bit 1 | **PC8** | D2 | **CN10 pin 31** | **Drzwi KIER** *(Nowy pin – zoptymalizowany pod kątem CAN2)* |
| **SW2** | Bit 2 | **PC7** | D8 | **CN10 pin 19** | **Drzwi INNE** *(Nowy pin – zoptymalizowany pod kątem CAN2)* |
| **SW3** | Bit 3 | **PB7** | - | **CN10 pin 5** | **Światła** |
| **SW4** | Bit 4 | **PB12** | - | **CN10 pin 16** | **Zamki ZAMKNIĘTE** *(Fizyczny feedback położenia siłownika/przekaźnika)* |
| **SW5** | Bit 5 | **PB13** | - | **CN10 pin 30** | **Maska** |
| **SW6** | Bit 6 | **PB14** | D1 | **CN10 pin 28** | **Awaryjne** |
| **SW7** | Bit 7 | **PB15** | D0 | **CN10 pin 26** | **Hamulec RĘCZNY** |
| **SW8** | Bit 8 | **PB2** | - | **CN10 pin 22** | **Dodatkowy 9. pin wejściowy (EXTRA)** |

### 4.2. Wyjścia cyfrowe (Aktuatory / Sterowanie)
| Wyjście | Pin MCU | Złącze Arduino | Złącze ST-Morpho | Funkcja / Opis |
| :--- | :---: | :---: | :---: | :--- |
| **RELAY_CTRL** | **PA8** | D7 | **CN10 pin 23** | Sterowanie przekaźnikiem bistabilnym/zamkiem za pomocą zboczy (zbocze narastające = ZAMKNIJ, zbocze opadające = OTWÓRZ). |

### 4.3. Wejścia analogowe (Pomiary ADC)
| Sygnał | Pin MCU | Złącze Arduino | Złącze ST-Morpho | Funkcja / Opis |
| :--- | :---: | :---: | :---: | :--- |
| **ADC_IN** | **PA0** | A0 | **CN7 pin 28** | Pomiar napięcia przez 12-bitowy przetwornik ADC1 (`bsp_adc`). |

---

## 5. Piny wolne do wykorzystania (Niewykorzystane w aplikacji)

Poniższe wyprowadzenia mikrokontrolera nie są zajęte przez logikę aplikacji ani peryferia płytki Nucleo i mogą zostać swobodnie użyte:

### Port A
* **PA1** (CN7 pin 30 / Arduino A1) – wolne wejście ADC/GPIO.
* **PA4** (CN7 pin 32 / Arduino A2) – wolne wejście ADC / wyjście DAC.
* **PA6** (CN10 pin 13 / Arduino D12) – wolne GPIO (lub SPI1_MISO).
* **PA7** (CN10 pin 15 / Arduino D11) – wolne GPIO (lub SPI1_MOSI).
* **PA9** (CN10 pin 2 / Arduino D8) – wolne GPIO.
* **PA10** (CN10 pin 4 / Arduino D2) – wolne GPIO.
* **PA15** (CN7 pin 17) – wolne GPIO (lub JTDI).

### Port B
* **PB0** (CN7 pin 34 / Arduino A3) – wolne wejście ADC/GPIO.
* **PB1** (CN7 pin 32 via SB28 / CN10 pin 18 via SB29) – wolne GPIO.
* **PB10** (CN10 pin 25 / Arduino D6) – wolne GPIO (lub TIM2_CH3).

### Port C
* **PC0** (CN7 pin 38 / Arduino A5) – wolne GPIO/ADC.
* **PC1** (CN7 pin 36 / Arduino A4) – wolne GPIO/ADC.
* **PC2** (CN7 pin 35) – wolne GPIO.
* **PC3** (CN7 pin 37) – wolne GPIO.
* **PC5** (CN10 pin 6 alternatywnie) – wolne GPIO.
* **PC6** (CN10 pin 17) – wolne GPIO.
* **PC9** (CN10 pin 1 / Arduino D10) – wolne GPIO.
* **PC10** (CN7 pin 1) – wolne GPIO.
* **PC11** (CN7 pin 2) – wolne GPIO.
* **PC12** (CN7 pin 3) – wolne GPIO.

### Port D
* **PD2** (CN7 pin 4) – wolne GPIO.

---

## 6. Szkic fizyczny złącza ST-Morpho CN10

```text
               ZŁĄCZE ST-MORPHO CN10 (Prawa krawędź Nucleo-64)
               
        Wewnętrzny rząd (Odd)           Zewnętrzny rząd (Even)
        ---------------------           ----------------------
Rząd 1: CN10 pin 1 (PC9 / SB9 DNF)|     CN10 pin 2 -> PC8 [DRZWI KIER]
Rząd 2: CN10 pin 3 -> PB6 [CAN2_TX|     CN10 pin 4 -> PC7 [DRZWI INNE]
Rząd 3: CN10 pin 5 -> PB7 [Światła|     CN10 pin 6 -> PB5  [CAN2_RX] todo
Rząd 4: CN10 pin 7 -> AREF        |     CN10 pin 8 -> 5V_STLK
...                               |     ...
Rząd 8: CN10 pin 15 -> PA7        |     CN10 pin 16 -> PB12 [Zamki ZAMKNIĘTE - SW4]
...                               |     ...
Rząd 10:CN10 pin 19 -> PC7 [INNE] |     CN10 pin 20 -> GND
...                               |     ...
Rząd 11:CN10 pin 21 -> PA9 [Wolny]|     CN10 pin 22 -> PB2  [Dodatkowy - SW8]
Rząd 12:CN10 pin 23 -> PA8 [RELAY]|     CN10 pin 24 -> GND
Rząd 13:CN10 pin 25 -> PB10       |     CN10 pin 26 -> PB15 [Hamulec RĘCZNY - SW7]
Rząd 14:CN10 pin 27 -> PB4 [ACC]  |     CN10 pin 28 -> PB14 [Awaryjne - SW6]
Rząd 15:CN10 pin 29 -> PB3        |     CN10 pin 30 -> PB13 [Maska - SW5]
Rząd 16:CN10 pin 31 -> PC8 [KIER] |     CN10 pin 32 -> AGND
Rząd 17:CN10 pin 33 -> PA10[Wolny]|     CN10 pin 34 -> PC4  [VBUS_DETECT]
Rząd 18:CN10 pin 35 -> PB8        |     CN10 pin 36 -> NC
```

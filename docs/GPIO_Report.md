# Raport GPIO dla płytki NUCLEO-C542RC (MB2213)

Dokument ten zawiera analizę wyprowadzeń (GPIO) mikrokontrolera STM32C542RCT6 (obudowa LQFP64) na podstawie schematu płytki Nucleo-64 (MB2213, rewizja B-02).

## 1. Piny zablokowane i zarezerwowane (System / Zasilanie / Zegary)
Te piny pełnią funkcje krytyczne dla działania systemu lub są sprzętowo podłączone do elementów zasilania i oscylatorów na płytce.

| Pin | Funkcja domyślna na Nucleo | Ograniczenia / Opis |
|-----|----------------------------|---------------------|
| **PH0** | `OSC_IN` | Podłączony do kwarcu HSE 24 MHz (X3). Niedostępny jako zwykłe GPIO, chyba że usuniesz rezonator z płytki. |
| **PH1** | `OSC_OUT` | Podłączony do kwarcu HSE 24 MHz (X3). Niedostępny jako GPIO. |
| **PC14**| `OSC32_IN` | Podłączony do kwarcu LSE 32.768 kHz (X2). Niedostępny jako GPIO. |
| **PC15**| `OSC32_OUT` | Podłączony do kwarcu LSE 32.768 kHz (X2). Niedostępny jako GPIO. |
| **PH2** | `BOOT0` | Podłączony do układu wyboru bootowania i pinu BOOT. |
| **Pin 7** | `NRST` | Sygnał resetu systemowego (przycisk B2 / programator). |
| Różne | `VDD`, `VSS`, `VCAP`, `VBAT`, `VREF+` | Piny zasilania i masy. |

## 2. Piny używane przez peryferia zintegrowane na płytce Nucleo
Piny te są podłączone do układów na płytce. Można ich użyć do innych celów, jednak może to powodować konflikty ze sprzętem zintegrowanym na PCB (np. mruganie diodą czy komunikacja po CAN).

| Pin | Funkcja domyślna na Nucleo | Złącze Arduino | Ograniczenia / Opis |
|-----|----------------------------|----------------|---------------------|
| **PA13**| `DBG_SWDIO` | - | Interfejs programowania/debugowania ST-LINK. (Nie zmieniaj) |
| **PA14**| `DBG_SWCLK` | - | Interfejs programowania/debugowania ST-LINK. (Nie zmieniaj) |
| **PB3** | `DBG_SWO` | D3 | Interfejs debugowania (Trace). Sprzętowo połączony do ST-LINK. |
| **PA2** | `VCP_TX` | D1 (TX) | Virtual COM Port - podłączony do ST-LINK (USART2_TX). |
| **PA3** | `VCP_RX` | D0 (RX) | Virtual COM Port - podłączony do ST-LINK (USART2_RX). |
| **PA5** | `USER_LED (LD1)` | D13 | Zielona dioda użytkownika na płytce. |
| **PC13**| `USER_BUTTON (B1)` | - | Niebieski przycisk użytkownika. Wyposażony w sprzętowy filtr. |
| **PB9** | `FDCAN_TX` | D14 | Wyprowadzony na wbudowany transceiver CAN (układ U7 MCP2562FD). |
| **PB8** | `FDCAN_RX` | D15 | Wyprowadzony na wbudowany transceiver CAN (układ U7 MCP2562FD). |
| **PE2** | `FDCAN_STBY` | - | Pin sterujący trybem STBY (Standby) dla wbudowanego transceivera CAN. |
| **PA11**| `USB_FS_N` | - | Dedykowane złącze USB Typu C (Data -). |
| **PA12**| `USB_FS_P` | - | Dedykowane złącze USB Typu C (Data +). |
| **PC4** | `VBUS_DETECT` | - | Wykrywanie zasilania USB na złączu Type-C. |

## 3. Piny wolne do wykorzystania (Dostępne na listwach ST-Morpho i Arduino Uno V3)
Poniższe piny można swobodnie wykorzystać w aplikacji (są wyprowadzone na listwy, nie są sprzętowo obciążone wewnętrznymi elementami aktywnymi).

### Port A
* **PA0** (Arduino A0)
* **PA1** (Arduino A1)
* **PA4** (Arduino A2, DAC/ADC)
* **PA6** (Arduino D12, SPI_MISO)
* **PA7** (Arduino D11, SPI_MOSI)
* **PA8** (Arduino D7)
* **PA9** (Arduino D8)
* **PA10** (Arduino D2)
* **PA15**

### Port B
* **PB0** (Arduino A3)
* **PB1**
* **PB2**
* **PB4** (Arduino D5)
* **PB5** (Arduino D4)
* **PB6** (Arduino D10)
* **PB7**
* **PB10** (Arduino D6)
* **PB12**
* **PB13**
* **PB14**
* **PB15**

### Port C
* **PC0** (Arduino A5)
* **PC1** (Arduino A4)
* **PC2**
* **PC3**
* **PC5**
* **PC6**
* **PC7** (Arduino D9)
* **PC8**
* **PC9**
* **PC10**
* **PC11**
* **PC12**

### Port D
* **PD2**


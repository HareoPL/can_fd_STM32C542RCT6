# Plan Wdrożenia: Przyspieszenie Transferu OTA na Klasycznym CAN 500 kbps

**Magistrala:** Classic CAN 2.0, 500 kbps, 29-bit Extended ID (bez zmian sprzętowych)  
**Warstwa transportowa:** ISO 15765-2 (ISO-TP)  
**Cel:** skrócenie czasu wgrywania aplikacji (~63.7 KB) z ~20 s do ~3–5 s

---

## 1. Analiza Stanu Wyjściowego

| Etap transferu bloku 256 B (~38 ramek CF) | Czas |
|---|---|
| Rzeczywista transmisja ramek na magistrali 500 kbps (~0.26 ms / ramkę) | ~10 ms |
| Sztuczna przerwa STmin = 2 ms między ramkami CF | ~76 ms |
| ACK + programowanie Flash | kilka ms |

Magistrala była wykorzystywana jedynie w ~13%. Wąskim gardłem były:
- Flow Control urządzenia wymuszający **STmin = 2 ms** (`isotp.c`).
- Dolny limit opóźnienia `max(0.002, …)` w `tools/can_updater.py` (oraz niska rozdzielczość `time.sleep()` w Windows dla Pythona < 3.11, typowo ~15 ms).
- Mały blok OTA (256 B) – duża liczba rund ACK.

## 2. Zidentyfikowane Zależności i Ryzyka

1. **Odroczony zapis tablicy wektorów (Bootloader):** bufor `s_deferred_vector_block` miał 512 B. Większy blok pod adresem `0x08008000` byłby zapisywany natychmiast, co łamie gwarancję wejścia w Recovery Mode po przerwanej aktualizacji. Bufor musi rosnąć razem z rozmiarem bloku.
2. **Ścieżka odbioru w Aplikacji:** ISR → `can_rx_task` → kolejka `ota_rx_queue` (64 ramki) → `ota_worker`. Bez kontroli przepływu blok 1 KB (~148 ramek) mógłby przepełnić kolejkę (`xQueueSend(..., 0)` po cichu gubi ramki). Wymagany **Block Size (BS)** w Flow Control.
3. **Brak obsługi BS po stronie odbiorczej ISO-TP:** `isotp.c` zawsze wysyłał FC z BS = 0.
4. **Filtr globalny Bootloadera:** cały ruch magistrali trafiał do 3-elementowej sprzętowej kolejki RX FIFO0, co na obciążonej magistrali mogło wypierać ramki OTA.

## 3. Decyzje Projektowe

| Parametr | Wartość | Uzasadnienie |
|---|---|---|
| Rozmiar bloku OTA | **1024 B** (`OTA_MAX_BLOCK_SIZE`) | Zysk z 2048 B to ~0.2 s, kosztem RAM i dłuższego retry |
| FC Bootloadera | **BS = 0, STmin = 0** | Pętla bare-metal opróżnia FIFO co ~10 µs, ramki co ~260 µs |
| FC Aplikacji | **BS = 16, STmin = 0** | Gwarancja braku przepełnienia kolejki 64 ramek |
| Filtr sprzętowy Bootloadera | **tylko ID OTA** (`0x01F2F3E0`, `0x01F2F3DF`) | Ochrona FIFO0 przed obcym ruchem |
| Zgodność wstecz z Bootloaderem v1.1 | **brak** | Wszystkie urządzenia zostaną zaktualizowane |

## 4. Etapy Wdrożenia (atomowe commity)

Gałąź: `feat/ota-fast-transfer`

1. **ISO-TP (`app/can_bus/isotp/`)** – API `isotp_set_flow_control()`, obsługa BS po stronie odbiorczej, rozdzielenie buforów `ISOTP_RX_BUF_SIZE` / `ISOTP_TX_BUF_SIZE`, stała `OTA_MAX_BLOCK_SIZE` w `ota_protocol.h`.
2. **Bootloader (`app/bootloader/`)** – FC BS=0/STmin=0, bufor wektorów = `OTA_MAX_BLOCK_SIZE`, filtr sprzętowy ID OTA, licznik utraconych ramek RX FIFO0, wersja v1.2 (`0x12`).
3. **Aplikacja (`app/ota_update/`)** – FC BS=16/STmin=0, wersja firmware v1.2.0.
4. **Narzędzia PC (`tools/`)** – interpretacja STmin wg ISO 15765-2 (ms oraz 100–900 µs), usunięcie dolnego limitu 2 ms, precyzyjne oczekiwanie `perf_counter`, domyślny blok 1024 B (`--block-size`).
5. **Dokumentacja** – changelog, przewodnik OTA, instrukcja GUI, dokumentacja projektu.

## 5. Kolejność Aktualizacji Urządzeń

Nowy Bootloader jest instalowany przez slot staging Aplikacji, dlatego:
1. Nowa Aplikacja v1.2.0 oraz Bootloader v1.2 – wgranie przez ST-Link (`[Flash] Full Merged Image`).
2. Kolejne aktualizacje – po CAN nowym narzędziem PC.

## 6. Plan Testów Sprzętowych

| # | Test | Oczekiwany wynik |
|---|---|---|
| 1 | Pomiar czasu przed zmianami (baseline) | ~20 s |
| 2 | Aktualizacja Aplikacji przez nowy Bootloader | ~3–5 s, 0 retry |
| 3 | Aktualizacja Bootloadera przez Aplikację (staging) | OK, brak utraconych ramek |
| 4 | Przerwanie w połowie (`--abort-after`) | Recovery Mode, brak skoku do Aplikacji |
| 5 | Aktualizacja przy obcym ruchu na magistrali (~50% obciążenia) | OK, ewentualne retry, brak błędów CRC |
| 6 | Wiele węzłów – discovery + aktualizacja jednego | pozostałe węzły bez reakcji |

## 7. Ryzyka Rezydualne

- **Adapter USB-CAN nie nadąża z ramkami nadawanymi bez przerw:** podnieść STmin Bootloadera do `0xF5` (500 µs) – zmiana jednej stałej `BOOT_ISOTP_FC_ST_MIN`; oczekiwany czas nadal ~6–8 s.
- **Rozmiar Bootloadera:** wzrost o ~0.5–1 KB Flash i ~0.5 KB RAM (obecnie 21 KB z 32 KB).

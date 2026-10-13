# Dokumentacja Magistrali CAN

W tym folderze znajduje się dokumentacja architektury, implementacji i procedur portowania magistrali CAN.

## Dostępne dokumenty:
* [Przewodnik Portowania i Implementacji CAN (can_porting_guide.md)](can_porting_guide.md) – szczegółowa instrukcja krok po kroku opisująca architekturę warstwową, kontrakt sprzętowy `can_port.h`, adapter `stm32c5_can_adapter` z funkcją getter `stm32c5_can1_interfaceGet()`, obsługę przerwań FIFO 0, zadania FreeRTOS oraz inicjalizację w `main.c`.
* [Przewodnik Tworzenia Nowego Parsera CAN (can_parser_guide.md)](can_parser_guide.md) – instrukcja krok po kroku tworzenia nowego profilu parsera w `app/can_bus/can_parser/parsers/` (`cp_*.c`, `cp_*.h`), wzorzec profilu z getterem bez `extern`, rejestracja ID w `can_parser.h` i dyspozytorze `can_parser.c` oraz dynamiczne przełączanie w locie.


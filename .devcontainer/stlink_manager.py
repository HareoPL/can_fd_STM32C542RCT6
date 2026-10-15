"""
================================================================                
                 HOW TO USE - ST-LINK MANAGER
================================================================

OPIS:
Skrypt automatyzuje przekazywanie programatora ST-Link (V2 / V3) 
z Windowsa do kontenera Docker (przez WSL2) przy użyciu narzędzia usbipd.

WYMAGANIA:
1. Zainstalowany usbipd-win (winget install dorssel.usbipd-win).
2. Docker Desktop z włączonym silnikiem WSL 2.
3. Python zainstalowany na Windowsie.
4. Terminal (PowerShell/CMD) uruchomiony jako ADMINISTRATOR (lub uruchom stlink_attach.bat).

SPOSOBY UŻYCIA:

1. AUTOMATYCZNY (jeśli masz 1 programator ST-Link):
   python stlink_manager.py attach
   -> Skrypt sam wykryje programator po VID:PID (0483:374x / 0483:375x) lub nazwie i dołączy urządzenie.

2. INTERAKTYWNY (jeśli podłączono wiele programatorów):
   python stlink_manager.py attach
   -> Skrypt wyświetli listę wykrytych urządzeń i zapyta o numer.

3. PRECYZYJNY (przez argument --id):
   python stlink_manager.py attach --id 1-4
   -> Skrypt od razu dołączy urządzenie o zadanym BUSID.

4. ODŁĄCZANIE:
   python stlink_manager.py detach
   -> Zwraca urządzenie do systemu Windows.

ROZWIĄZYWANIE PROBLEMÓW:
- "bind --force": Skrypt automatycznie dodaje flagę --force, co rozwiązuje konflikt
  z filtrami USB (np. USBPcap / Wireshark).
- "No WSL 2 distribution running": Skrypt automatycznie wywołuje 'wsl echo', 
  aby wybudzić podsystem WSL.
================================================================
"""
import subprocess
import sys
import re
import argparse

# Dopasowanie po VID:PID ST-Linka (0483:374x / 0483:375x) lub po nazwie tekstowej
DEVICE_PATTERN = re.compile(r"0483:37[45][0-9a-fA-F]|ST[-]?Link|STM32.*STLink", re.IGNORECASE)

def ensure_wsl_running():
    """Upewnia się, że WSL 2 działa w tle przed operacją usbipd."""
    try:
        subprocess.run(['wsl', 'echo', 'WSL is awake'], capture_output=True, text=True, check=True)
    except Exception:
        print("[OSTRZEŻENIE] Nie udało się zweryfikować statusu WSL. Upewnij się, że Docker Desktop działa.")

def get_stlink_devices():
    """Wyszukuje wszystkie podłączone programatory ST-Link."""
    devices = []
    try:
        result = subprocess.run(['usbipd', 'list'], capture_output=True, text=True, check=True)
        lines = result.stdout.splitlines()
        for line in lines:
            if DEVICE_PATTERN.search(line):
                # Parsowanie BUSID (np. 1-4 lub 2-3.1)
                match = re.match(r"^\s*([0-9]+-[0-9]+(?:\.[0-9]+)*)\s+(.*)", line.strip())
                if match:
                    devices.append((match.group(1), match.group(2).strip()))
        return devices
    except subprocess.CalledProcessError as e:
        print(f"[BŁĄD] Nie można uruchomić usbipd: {e}")
        return []
    except FileNotFoundError:
        print("[BŁĄD] Narzędzie 'usbipd' nie zostało znalezione w zmiennej PATH.")
        print("Zainstaluj je komendą: winget install dorssel.usbipd-win")
        return []

def run_usbipd(action, busid):
    try:
        if action == "attach":
            ensure_wsl_running()
            print(f"[INFO] Rejestracja (bind) urządzenia {busid} (z opcją --force)...")
            subprocess.run(['usbipd', 'bind', '--force', '--busid', busid], capture_output=True)
            
            print(f"[INFO] Dołączanie (attach) urządzenia {busid} do WSL/Docker...")
            subprocess.run(['usbipd', 'attach', '--wsl', '--busid', busid], check=True)
            print(f"[SUKCES] ST-Link ({busid}) został pomyślnie przekazany do kontenera Docker/WSL.")
        elif action == "detach":
            print(f"[INFO] Odłączanie urządzenia {busid} z WSL/Docker...")
            subprocess.run(['usbipd', 'detach', '--busid', busid], capture_output=True)
            print(f"[INFO] Przywracanie natywnych sterowników Windows (unbind {busid})...")
            subprocess.run(['usbipd', 'unbind', '--busid', busid], capture_output=True)
            print(f"[SUKCES] ST-Link ({busid}) został w pełni odłączony i powrócił do Menedżera Urządzeń Windows.")
    except subprocess.CalledProcessError as e:
        print(f"[BŁĄD] Operacja usbipd nie powiodła się: {e}")
        print("Upewnij się, że terminal został uruchomiony jako Administrator oraz Docker Desktop działa.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Zarządzanie przekazywaniem ST-Link do kontenera Docker.")
    parser.add_argument("action", choices=["attach", "detach"], help="Akcja: attach (dołącz) lub detach (odłącz)")
    parser.add_argument("--id", help="Opcjonalny konkretny BUSID (np. 1-4)")
    
    args = parser.parse_args()
    
    if args.id:
        busid = args.id
    else:
        devices = get_stlink_devices()
        if not devices:
            print("[INFO] Nie znaleziono żadnego podłączonego urządzenia ST-Link.")
            sys.exit(1)
        
        if len(devices) == 1:
            busid = devices[0][0]
            print(f"[INFO] Znaleziono programator: BUSID {busid} -> {devices[0][1]}")
        else:
            print("\n[INFO] Znaleziono wiele urządzeń ST-Link:")
            for i, (bid, desc) in enumerate(devices):
                print(f"  [{i}] BUSID: {bid} | {desc}")
            choice = input("\nWybierz numer (domyślnie 0): ")
            idx = int(choice) if choice.strip() else 0
            busid = devices[idx][0]

    run_usbipd(args.action, busid)
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
==============================================================================
 Docker Image Transfer Tool with Live Progress Bar (Export & Import)
==============================================================================
 Provides real-time percentage, data throughput (MB/s), and ETA for:
   - Export: docker save -> tar archive
   - Import: tar archive -> docker load
 Supports graceful Ctrl+C interruption and atomic export file generation.
==============================================================================
"""

import sys
import os
import time
import json
import subprocess
import shutil

CHUNK_SIZE = 4 * 1024 * 1024  # 4 MB buffer for high NVMe/SSD throughput


def render_progress(current_bytes, total_bytes, start_time, last_update_time, force=False):
    """
    Renders an ASCII progress bar with percentage, transferred MB, speed, and ETA.
    Throttles screen refreshes to at most 10 FPS to prevent console lag.
    """
    now = time.time()
    if not force and (now - last_update_time) < 0.1:
        return last_update_time

    elapsed = max(now - start_time, 0.001)
    speed = current_bytes / elapsed

    if total_bytes and total_bytes > 0:
        pct = (current_bytes / total_bytes) * 100.0
        pct = min(max(pct, 0.0), 99.9 if current_bytes < total_bytes else 100.0)
        remaining = max(total_bytes - current_bytes, 0)
        eta_sec = remaining / max(speed, 1.0)
        if eta_sec < 60:
            eta_str = f"{int(eta_sec)}s"
        else:
            eta_str = f"{int(eta_sec)//60}m {int(eta_sec)%60:02d}s"
    else:
        pct = 0.0
        eta_str = "--"

    bar_len = 30
    filled = int(bar_len * (pct / 100.0))
    bar = '=' * filled + ('>' if filled < bar_len else '')
    bar = bar.ljust(bar_len)

    cur_mb = current_bytes / (1024.0 * 1024.0)
    tot_mb = (total_bytes / (1024.0 * 1024.0)) if total_bytes else 0.0
    spd_mb = speed / (1024.0 * 1024.0)

    if total_bytes:
        line = f"\r[{bar}] {pct:5.1f}% | {cur_mb:6.1f} / {tot_mb:6.1f} MB | {spd_mb:5.1f} MB/s | ETA: {eta_str:6s}"
    else:
        line = f"\r[{bar}] {cur_mb:6.1f} MB | {spd_mb:5.1f} MB/s | {elapsed:4.1f}s elapsed"

    sys.stdout.write(line)
    sys.stdout.flush()
    return now


def estimate_image_size(image_name, output_file):
    """
    Estimates the expected compressed tar size for docker save.
    """
    # 1. If an existing archive is present, use its size
    if os.path.isfile(output_file):
        try:
            sz = os.path.getsize(output_file)
            if sz > 10 * 1024 * 1024:
                return sz
        except OSError:
            pass

    # 2. Inspect docker image size and apply containerd compression ratio (~0.233)
    try:
        out = subprocess.check_output(
            ['docker', 'image', 'inspect', image_name],
            stderr=subprocess.DEVNULL
        )
        data = json.loads(out)[0]
        uncompressed = data.get('Size', 0)
        if uncompressed > 0:
            return int(uncompressed * 0.233)
    except Exception:
        pass

    # Fallback to ~943 MB (known size of vbox-builder image)
    return 943 * 1024 * 1024


def do_export(image_name, output_file):
    print("==============================================================================")
    print("             Eksport obrazu Docker do trwalego archiwum")
    print("==============================================================================")
    print(f"[INFO] Obraz zrodlowy : {image_name}")
    print(f"[INFO] Plik docelowy  : {output_file}")

    # Verify image exists
    try:
        subprocess.check_output(['docker', 'image', 'inspect', image_name], stderr=subprocess.DEVNULL)
    except Exception:
        print(f"\n[BLAD] Obraz '{image_name}' nie istnieje w lokalnym Dockerze!", file=sys.stderr)
        print("       Zbuduj go najpierw za pomoca: .devcontainer\\docker_build.bat", file=sys.stderr)
        return 1

    expected_size = estimate_image_size(image_name, output_file)
    print(f"[INFO] Szacowany rozmiar: {expected_size / (1024.0 * 1024.0):.1f} MB")
    print("[INFO] Trwa strumieniowanie i zapisywanie warstw obrazu...")
    print()

    tmp_file = output_file + ".tmp"
    if os.path.isfile(tmp_file):
        try:
            os.remove(tmp_file)
        except OSError:
            pass

    proc = subprocess.Popen(
        ['docker', 'save', image_name],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    start_time = time.time()
    last_update = 0
    bytes_written = 0

    try:
        with open(tmp_file, 'wb') as f_out:
            while True:
                chunk = proc.stdout.read(CHUNK_SIZE)
                if not chunk:
                    break
                f_out.write(chunk)
                bytes_written += len(chunk)
                last_update = render_progress(bytes_written, expected_size, start_time, last_update)

        proc.stdout.close()
        proc.wait()

        if proc.returncode != 0:
            err = proc.stderr.read().decode(errors='replace')
            print(f"\n[BLAD] docker save zakonczyl sie bledem (kod {proc.returncode}):\n{err}", file=sys.stderr)
            if os.path.isfile(tmp_file):
                os.remove(tmp_file)
            return proc.returncode

        # Force 100% display
        render_progress(bytes_written, bytes_written, start_time, 0, force=True)
        print("\n")

        # Atomic replacement of target file
        if os.path.isfile(output_file):
            try:
                os.remove(output_file)
            except OSError:
                pass
        os.rename(tmp_file, output_file)

        elapsed = time.time() - start_time
        final_mb = bytes_written / (1024.0 * 1024.0)
        avg_speed = final_mb / max(elapsed, 0.001)

        print("==============================================================================")
        print(f"[SUKCES] Obraz zostal wyeksportowany w {elapsed:.1f}s ({avg_speed:.1f} MB/s)!")
        print(f"[PLIK]   {output_file} ({final_mb:.1f} MB)")
        print("==============================================================================")
        print("[WSKAZOWKA] Ten plik mozesz zarchiwizowac na dysku sieciowym, NAS lub chmurze.")
        print("            Aby zaimportowac go na nowej maszynie w przyszlosci, uzyj:")
        print("            .devcontainer\\import_image.bat")
        print("==============================================================================")
        return 0

    except KeyboardInterrupt:
        print("\n\n[INFO] Przerwano operacje przez uzytkownika (Ctrl+C).")
        proc.kill()
        if os.path.isfile(tmp_file):
            try:
                os.remove(tmp_file)
            except OSError:
                pass
        return 130
    except Exception as e:
        print(f"\n[BLAD] Wyjatek podczas eksportu: {e}", file=sys.stderr)
        proc.kill()
        if os.path.isfile(tmp_file):
            try:
                os.remove(tmp_file)
            except OSError:
                pass
        return 1


def do_import(input_file):
    if not os.path.isfile(input_file):
        print("==============================================================================", file=sys.stderr)
        print("[BLAD] Nie znaleziono pliku archiwum obrazu!", file=sys.stderr)
        print("==============================================================================", file=sys.stderr)
        print(f"Szukano w sciezce: {input_file}", file=sys.stderr)
        print("\nUmiesc plik vbox-builder_stm32c542.tar w katalogu glownym projektu", file=sys.stderr)
        print("lub podaj sciezke do pliku jako parametr skryptu.", file=sys.stderr)
        return 1

    total_size = os.path.getsize(input_file)
    tot_mb = total_size / (1024.0 * 1024.0)

    print("==============================================================================")
    print("             Import obrazu Docker z trwalego archiwum")
    print("==============================================================================")
    print(f"[INFO] Plik zrodlowy : {input_file} ({tot_mb:.1f} MB)")
    print("[INFO] Trwa przesylanie i ladowanie warstw do silnika Docker...")
    print()

    proc = subprocess.Popen(
        ['docker', 'load'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    start_time = time.time()
    last_update = 0
    bytes_sent = 0

    try:
        with open(input_file, 'rb') as f_in:
            while True:
                chunk = f_in.read(CHUNK_SIZE)
                if not chunk:
                    break
                proc.stdin.write(chunk)
                bytes_sent += len(chunk)
                last_update = render_progress(bytes_sent, total_size, start_time, last_update)

        proc.stdin.close()
        render_progress(total_size, total_size, start_time, 0, force=True)
        print("\n")
        print("[INFO] Rejestrowanie obrazu w lokalnym silniku Docker...")

        out, err = proc.communicate()
        if proc.returncode != 0:
            print(f"[BLAD] docker load zakonczyl sie niepowodzeniem (kod {proc.returncode}):", file=sys.stderr)
            if err:
                print(err.decode(errors='replace'), file=sys.stderr)
            return proc.returncode

        if out:
            print(f"[OK] {out.decode(errors='replace').strip()}")

        elapsed = time.time() - start_time
        avg_speed = tot_mb / max(elapsed, 0.001)

        print()
        print("==============================================================================")
        print(f"[SUKCES] Obraz zostal pomyslnie zaimportowany w {elapsed:.1f}s ({avg_speed:.1f} MB/s)!")
        print("==============================================================================")
        print("[INFO] Mozesz teraz natychmiast kompilowac projekt zadaniem: [Docker] Build")
        print("==============================================================================")
        return 0

    except KeyboardInterrupt:
        print("\n\n[INFO] Przerwano operacje przez uzytkownika (Ctrl+C).")
        proc.kill()
        return 130
    except Exception as e:
        print(f"\n[BLAD] Wyjatek podczas importu: {e}", file=sys.stderr)
        proc.kill()
        return 1


def main():
    if len(sys.argv) < 2:
        print("Uzycie:")
        print("  python docker_transfer.py export <image_name> <output_file>")
        print("  python docker_transfer.py import <input_file>")
        sys.exit(1)

    cmd = sys.argv[1].lower()
    if cmd == 'export':
        if len(sys.argv) < 4:
            print("Blad: brakujace argumenty dla eksportu.")
            sys.exit(1)
        sys.exit(do_export(sys.argv[2], sys.argv[3]))
    elif cmd == 'import':
        if len(sys.argv) < 3:
            print("Blad: brakujaca sciezka do pliku dla importu.")
            sys.exit(1)
        sys.exit(do_import(sys.argv[2]))
    else:
        print(f"Nieznane polecenie: {cmd}")
        sys.exit(1)


if __name__ == '__main__':
    main()


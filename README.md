# V-BOX Firmware Project

Automotive controller firmware developed for the **STM32C542RCT6** microcontroller on the **NUCLEO-C542RC** board (ARM Cortex-M33 @ 144 MHz, TrustZone, FPU/DSP).

---

## Key Architectural Features
- **Strict Layered Architecture:** Complete separation of business application logic (`app/`), hardware abstraction port contracts in pure C (`app/port/`), hardware adapters (`app/port/stm32c5/`), and FreeRTOS middleware.
- **Event-Driven CAN Bus (FDCAN1):** Classic CAN 2.0B / CAN-FD running at 500 kbps nominal with hardware classic filtering, zero-polling RX via binary semaphore, thread-safe TX queue, and modular parser profiles (`app/can_bus/can_parser/parsers/`).
- **CAN2 Preparation (FDCAN2):** PB5 (`FDCAN2_RX`) and PB6 (`FDCAN2_TX`) reserved and vacated for drop-in secondary CAN bus support.
- **Fault-Tolerant Bistable Relay Engine:** Edge-controlled latching relay with pre-charge pulse generation, closed-loop feedback verification (PB12), retry mechanism, power-cut recovery, and boot synchronization without audible clicking.
- **Non-Blocking Asynchronous Worker:** Decoupled relay execution via FreeRTOS queue (`s_relay_cmd_queue`) and `relay_worker_task` to prevent CAN RX FIFO overflow.
- **Parallel 16-bit Input Debouncer:** 10 ms periodic sampling, 40 ms debouncing window for 9 discrete inputs (SW0..SW8) on GPIOB and GPIOC.
- **12-bit Analog Voltage Sensing:** ADC1 sampling on PA0 linearly mapped to 0..100% vehicle voltage representation.
- **Thread-Safe Logging:** Exclusive UART2 console logging at 115200 baud via `safe_printf` protected by FreeRTOS mutex.
- **Automated Memory Footprint Reporting:** Post-build script (`tools/print_memory.py`) displaying detailed FLASH and RAM usage, FreeRTOS heap (`ucHeap`), system stack, and custom sections.

---

## Quick Start & Build Guide

The project supports two symmetrical, interchangeable build environments. Both produce the final binary at the unified location: `build/debug_GCC_NUCLEO-C542RC/v_box.elf`.

### 1. Building on Windows (Native)

#### Required VS Code Extensions:
To configure, compile, and debug this project in Visual Studio Code on Windows, install the following extensions:
* **[STM32 VS Code Extension](https://marketplace.visualstudio.com/items?itemName=stmicroelectronics.stm32-vscode-extension)** (`stmicroelectronics.stm32-vscode-extension`):  
  Official ST extension suite. Manages toolchain integration, ST-Link GDB server, target registers, and FreeRTOS task views.
* **[CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)** (`ms-vscode.cmake-tools`):  
  Required to parse `CMakePresets.json`, configure the build tree, and execute CMake tasks (`[Windows] Configure`, `[Windows] Build`).
* **[clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd)** (`llvm-vs-code-extensions.vscode-clangd`):  
  Provides fast, accurate C/C++ code navigation and completion using `compile_commands.json` (the default `C_Cpp.intelliSenseEngine` is disabled in favor of clangd).

#### Recommended Productivity Extensions:
* **[C/C++ Include Guard](https://marketplace.visualstudio.com/items?itemName=akiramiyakoda.cppincludeguard)** (`akiramiyakoda.cppincludeguard`): Automatically generates and updates header include guards according to project conventions.
* **[koroFileHeader](https://marketplace.visualstudio.com/items?itemName=obkoro1.korofileheader)** (`obkoro1.korofileheader`): Auto-generates standardized Doxygen file header blocks (`@file`, `@brief`, `@author`, `@date`).
* **[Serial Monitor](https://marketplace.visualstudio.com/items?itemName=eclipse-cdt.serial-monitor)** (`eclipse-cdt.serial-monitor`): Integrated console to view UART2 `safe_printf` logs (115200 baud) without external tools like PuTTY.
* **[Memory Inspector](https://marketplace.visualstudio.com/items?itemName=eclipse-cdt.memory-inspector)** (`eclipse-cdt.memory-inspector`): Live inspection of RAM and peripheral registers during debug sessions.

*(Note: Opening the workspace in VS Code will automatically prompt you to install these via `.vscode/extensions.json`.)*

#### Build Commands:
* **VS Code Task:** Press `Ctrl+Shift+B` or run task **`[Windows] Build`**.
* **Clean Rebuild:** Run task **`[Windows] Rebuild`**.
* **Command Line (PowerShell/CMD):**
  ```powershell
  cmake --preset debug_GCC_NUCLEO-C542RC
  ninja -C build/debug_GCC_NUCLEO-C542RC
  ```

### 2. Building via Docker (Containerized)
The Docker container provides an identical, bit-for-bit reproducible build environment on any machine without installing local toolchains, Python packages, or CMake.

#### Step-by-Step Docker Setup & Build:

1. **Install Docker Engine on Windows:**
   * Install [Docker Desktop for Windows](https://www.docker.com/products/docker-desktop/) (ensure the WSL 2 backend is enabled during installation).
   * Verify Docker is running (whale icon in the Windows notification tray).

2. **Download Pre-built Docker Image Archive:**
   * Download the complete, pre-configured image archive `vbox-builder_stm32c542.tar` from corporate storage:  
     👉 **[v-box-tester (SharePoint Archive)](https://lincorspzoo-my.sharepoint.com/:f:/g/personal/j_lukaszewicz_lincor_pl/IgBjV5wdDW81QqDHNNTQZr4GARtXaX-XTG5QeiUvk5Fbme8?e=RkfN9G)**
   * Place the downloaded file `vbox-builder_stm32c542.tar` directly in the project root directory (`c:\WORK\5_v-box\`).

3. **Import the Image into Docker:**
   * Execute the import script in CMD or PowerShell:
     ```cmd
     .devcontainer\import_image.bat
     ```
     *(or run the VS Code task: `[Docker] Import Image from Archive`)*
   * In 15–30 seconds the image `vbox-builder:latest` will be loaded into your local Docker engine without needing any internet downloads.
   * *(Alternative: If you do not have the archive, running step 4 will automatically build the image from the local `Dockerfile`)*.

4. **Build the Project:**
   * **Using VS Code Tasks:**
     * Press `Ctrl+Shift+P` $\rightarrow$ `Run Task...` $\rightarrow$ select **`[Docker] Build`**
     * For full clean rebuild: select **`[Docker] Rebuild`**
     * To build and flash in one step: select **`[Docker] Build & Flash`**
   * **Using Command Line (PowerShell/CMD):**
     ```cmd
     .devcontainer\docker_build.bat
     ```
     *(To clean and rebuild from scratch: `.devcontainer\docker_build.bat rebuild`)*


### 3. Flashing to STM32 Hardware (ST-LINK V3)
A single unified task programs the microcontroller regardless of whether code was built on Windows or inside Docker:
* **VS Code Task:** **`Flash Device`** (runs `STM32_Programmer_CLI` via SWD in Under-Reset mode).
* **One-Click Build & Flash:**
  * For Windows: **`[Windows] Build & Flash`**
  * For Docker: **`[Docker] Build & Flash`**

### 4. VS Code Tasks Overview
| Task Label | Environment | Purpose |
| :--- | :---: | :--- |
| **`[Windows] Build`** *(Default `Ctrl+Shift+B`)* | Windows | Incremental build on host. Auto-configures if build folder is missing. |
| **`[Windows] Rebuild`** | Windows | Full clean and rebuild on host. |
| **`[Windows] Build & Flash`** | Windows | Builds on host and flashes target via ST-Link. |
| **`[Docker] Build`** | Docker | Headless container build. Copies `.elf` to the unified output path. |
| **`[Docker] Rebuild`** | Docker | Cleans container cache and rebuilds from scratch. |
| **`[Docker] Build & Flash`** | Docker | Builds in Docker and flashes target via host ST-Link. |
| **`Flash Device`** | Host Win | Direct flash of `build/debug_GCC_NUCLEO-C542RC/v_box.elf` via SWD. |
| **`Clean All`** | Host Win | Removes the entire `build/` directory (both Windows and Docker). |
| **`[USB] Attach ST-Link to Docker`** | Host Win | Passes ST-Link V3 to WSL/Docker via `usbipd` (with `--force`). |
| **`[USB] Detach ST-Link from Docker`** | Host Win | Unbinds ST-Link from WSL and returns it to Windows Device Manager. |
| **`[Docker] Export Image to Archive`** | Docker | Exports `vbox-builder` to `vbox-builder_stm32c542.tar` for long-term storage. |
| **`[Docker] Import Image from Archive`** | Docker | Imports image from `.tar` archive on a new PC without internet. |

---

## Detailed Documentation Directory (`docs/`)

Comprehensive technical and developer documentation is organized in the `docs/` folder:

* 📖 **[docs/build_and_development_guide.md](docs/build_and_development_guide.md)**  
  **Complete Build, Docker, and Tooling Manual:** Step-by-step instructions for Windows and Docker builds, ST-Link V3 USB pass-through, flashing architecture, long-term Docker image archiving (`export`/`import`), memory footprint reporter, QA tools (`cppcheck`, `doxygen`, `srecord`, `lcov`), and troubleshooting FAQ.

* 📖 **[docs/project_documentation.md](docs/project_documentation.md)**  
  **Hardware & Software Technical Specification:** Detailed hardware peripherals (FDCAN1, ADC1, USART2, GPIO, NVIC), full 64-pin Nucleo pinout table, ASCII board diagram, CAN frame definitions (`0x01F2F3F0`, `0x01F2F3F4`), bistable relay state machine, FreeRTOS task table, IPC concurrency diagram, and NVIC priority rules.

* 📖 **[docs/can_bus/can_parser_guide.md](docs/can_bus/can_parser_guide.md)**  
  **CAN Parser Profile Guide & Generator:** Architecture of the modular CAN dispatcher, zero-extern getter pattern, and instructions for the automated Python generator (`tools/create_parser.py <name>`).

* 📖 **[docs/can_bus/can_porting_guide.md](docs/can_bus/can_porting_guide.md)**  
  **Hardware Abstraction Layer (HAL) Porting Guide:** Instructions for porting the CAN subsystem and hardware adapters to other microcontroller families (e.g. STM32H5, STM32G4).

* 📖 **[docs/changelog.md](docs/changelog.md)**  
  **Project Changelog:** Complete, date-indexed record of all architectural updates, added modules, modified APIs, and bug fixes following the *Keep a Changelog* standard.

# hardware

Прошивка робота на **NUCLEO-F411RE** (STM32F411).  
Инструкция проверена **только на macOS 14.5 (Apple Silicon)**. На Windows/Linux шаги те же, пути к `/dev/cu.*` и установщикам могут отличаться.

Проект: `stm32/Robot_car/`

---

## Что установить

| Инструмент | Зачем | Скачать |
|------------|-------|---------|
| **STM32CubeIDE 2.x** | код, сборка, Debug | [st.com/stm32cubeide](https://www.st.com/en/development-tools/stm32cubeide.html) |
| **STM32CubeMX 6.x** | настройка пинов, `.ioc`, Generate Code | [st.com/stm32cubemx](https://www.st.com/en/development-tools/stm32cubemx.html) → macOS Apple Silicon |
| **st-stlink-server** | без него Debug в CubeIDE 2.x: *ST-Link Server is required* | [st.com/st-link-server](https://www.st.com/en/development-tools/st-link-server.html) |
| **STM32CubeProgrammer** | прошивка/диагностика без IDE (опционально) | [st.com/stm32cubeprog](https://www.st.com/en/development-tools/stm32cubeprog.html) |

> **CubeIDE 2.x не открывает pinout внутри IDE** — пины настраиваются в отдельном **CubeMX**, как на Windows со старым CubeIDE 1.19.

После установки CubeMX (путь по умолчанию):

```bash
sudo ln -sf /Applications/STMicroelectronics/STM32CubeMX.app /Applications/STM32CubeMX.app
```

ST-LINK Server:

```bash
sudo installer -pkg "/Applications/st-stlink-server.*.pkg" -target /
ls -l /usr/local/bin/stlink-server
```

---

## Подключение платы

- USB в **CN1** (ST-LINK), не CN2.
- Кабель с передачей данных.

Проверка на Mac:

```bash
system_profiler SPUSBDataType | grep -i -A6 "STLink"
ls /dev/cu.usbmodem*
```

Ожидаемо: `STM32 STLink` и порт вида `/dev/cu.usbmodem21203`.  
Для скриптов используй **`/dev/cu.…`**, не `tty.…`.

---

## Настройка пинов (CubeMX)

1. Запусти **STM32CubeMX**.
2. **File → Load Project** → `stm32/Robot_car/Robot_car.ioc`.
3. Вкладка **Pinout & Configuration** — назначение периферии (см. таблицу ниже).
4. **Project Manager:**
   - Toolchain: **STM32CubeIDE**
   - Project Location: `…/hardware/stm32/Robot_car`
   - **Keep User Code** — включено
5. **GENERATE CODE** → **Open Project** (или Refresh в IDE).

### Pinout проекта Robot_car

| Функция | Пины | Периферия |
|---------|------|-----------|
| UART ↔ Mac/Pi (Virtual COM ST-LINK) | PA2 TX, PA3 RX | USART2, 115200 |
| UART (в прошивке) | PA9 TX, PA10 RX | USART1, 115200 |
| PWM моторов (запуск в `main`) | PB6, PB7 | TIM4 CH1/CH2 |
| PWM (заготовка в коде) | PA6, PA7 | TIM3 CH1/CH2 |
| Enable драйвера | PB0, PB1 | GPIO OUT |
| Прочие выходы | PC0, PC1, PA0, PA1 | GPIO OUT |

SWD (отладка): PA13, PA14 — CubeMX ставит автоматически.

---

## Импорт и сборка (CubeIDE)

1. Запуск IDE **из терминала** (чтобы видел `stlink-server`):

   ```bash
   export PATH="/usr/local/bin:$PATH"
   /Applications/STM32CubeIDE.app/Contents/MacOS/STM32CubeIDE
   ```

2. **File → Import → Existing Projects into Workspace** → `stm32/Robot_car`.
3. **Project → Build Project** → `0 errors`, появится `Debug/Robot_car.elf`.

### Сборка на Mac: правки в `main.c`

GCC на Mac строже Windows. В `Core/Src/main.c` в блоке `USER CODE Includes` нужны:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
```

Функции `pwm_from_percent` и `parse_command` не должны быть закомментированы, если используются.

---

## Debug / прошивка

1. Закрой **STM32CubeProgrammer** (ST-LINK один на всех).
2. USB в CN1.
3. **Run → Debug As → STM32 C/C++ Application**.
4. На *Confirm Perspective Switch* → **Switch**.
5. **Resume** (F8) — MCU продолжит работу. Закончил → **Terminate**.

### Debug Configurations (если не коннектится)

**Run → Debug Configurations → Robot_car Debug → Debugger:**

| Параметр | Значение |
|----------|----------|
| Probe | ST-LINK (GDB server) |
| Interface | SWD |
| Reset mode | Connect under reset |

---

## Альтернатива: прошить без Debug

**STM32CubeProgrammer** → Connect (ST-LINK, SWD) → Download → `Debug/Robot_car.elf`.

---

## Типичные проблемы (Mac)

| Симптом | Решение |
|---------|---------|
| *ST-Link Server is required* | установить `st-stlink-server`, запускать IDE из терминала |
| *Unable To Launch* | **Run → Debug As → STM32 C/C++ Application** (не просто Run) |
| *Install stm32cubeMX* при клике на `.ioc` | открывать `.ioc` в **CubeMX**, не в IDE; symlink выше |
| Нет `/dev/cu.usbmodem*` | USB в CN1, другой кабель |
| Programmer подключён, IDE нет | Disconnect в Programmer |
| Обрыв загрузки с st.com | `aria2c -c URL` или `curl -L -C - -O URL` |

---

## Цикл разработки

```text
CubeMX  →  пины / таймеры / UART  →  GENERATE CODE
CubeIDE →  правка main.c  →  Build  →  Debug
```

Драйверы ST-LINK на Mac отдельно **не нужны** — достаточно пакетов ST выше.

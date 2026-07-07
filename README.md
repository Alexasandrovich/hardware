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

После прошивки **LD2 (зелёный LED на NUCLEO)** мигает ~раз в секунду — прошивка жива (см. `led_init` в `main.c`).

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

---

## Документация (ссылки)

Проверено для **NUCLEO-F411RE** + модуль **BTS7960 (IBT-2)**.

### Плата NUCLEO / MCU

| Документ | Зачем |
|----------|-------|
| [UM1724 — NUCLEO-64 (PDF)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) | Arduino/Morpho: **A0, D10**, GND, 5V — Figure 25 и далее |
| [STM32F411RE datasheet (PDF)](https://www.st.com/resource/en/datasheet/stm32f411re.pdf) | Корпус LQFP64, лимиты по току на pin |
| [RM0383 — Reference Manual (PDF)](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f411xc-e-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) | TIM, UART, GPIO, альтернативные функции |
| [NUCLEO-F411RE на st.com](https://www.st.com/en/evaluation-tools/nucleo-f411re.html) | Страница платы, прошивки ST-LINK |

### Драйвер моторов BTS7960

| Документ | Зачем |
|----------|-------|
| [BTS7960 module — Handsontec (PDF)](https://www.handsontec.com/dataspecs/module/BTS7960%20Motor%20Driver.pdf) | Подписи клемм: B+, B−, VCC, GND, RPWM, LPWM, R_EN, L_EN |
| [BTS7960B — Infineon](https://www.infineon.com/cms/en/product/power/motor-control-ics/intelligent-motor-control-ics/integrated-full-bridges/bts7960b/) | Логика полумостов, токи |

### Raspberry Pi (следующие этапы)

| Документ | Зачем |
|----------|-------|
| [RPi 5 Product Brief (PDF)](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008348-DS-6-raspberry-pi-5-product-brief.pdf) | Питание, интерфейсы |
| [USB-PD on Pi 5 (PDF)](https://pip-assets.raspberrypi.com/categories/685-app-notes-guides-whitepapers/documents/RP-009856-WP-1-USB%20Power%20delivery%20on%20Raspberry%20Pi%205.pdf) | Питание 5 V / 5 A |

### Питание робота

| Документ | Зачем |
|----------|-------|
| [XL4015 DC-DC datasheet (PDF)](http://www.xlsemi.com/datasheet/XL4015%20datasheet.pdf) | Понижение VBAT → 5 V |

### Свои заметки и схемы (репозиторий obsidian_notes)

| Материал | Путь |
|----------|------|
| Сборка 4WD, pinmap «полный» | `04 Resources/Electronics/Study/Lecture 6 - 4WD Robot Build Plan.md` |
| KiCad: питание + сигналы | `04 Resources/Electronics/Study/KiCad/Robot_Power_Distribution/` |
| Протокол Pi ↔ NUCLEO (позже) | `04 Resources/Electronics/Study/Lecture 8 - RBOT Protocol.md` |

---

## STM32 ↔ BTS7960: проводка под проект преподавателя (`Robot_car`)

> **Важно:** в Lecture 6 / KiCad другой pinmap (PA0/PA1, PA6/PA7, PC0/PC1).  
> **Сейчас ориентируемся на `.ioc` и комментарии в `main.c` от преподавателя** — иначе прошивка и провода не совпадут.

### Что настроено в CubeMX / `main.c`

| Сигнал в коде | MCU | NUCLEO (Arduino) | TIM4 ~100 Гц |
|---------------|-----|------------------|--------------|
| LPWM (левый ШИМ) | **PB6** | **D10** (CN5-2) | TIM4_CH1 |
| RPWM (правый ШИМ) | **PB7** | Morpho **CN7-21** | TIM4_CH2 |
| L_EN | **PB0** | **A3** (CN8-4) | GPIO |
| R_EN | **PB1** | Morpho **CN10-24** | GPIO |

В `main()` уже: `HAL_TIM_PWM_Start_IT(TIM4)`, `PB0/PB1 = HIGH`.

**Почему TIM4 и ~100 Гц:** преподаватель задал Prescaler=99, Period=1599 → около **100 импульсов/с** на PB6/PB7. Для первого теста мотор **может** крутиться и так; в Lecture 6 позже рекомендуют **15–20 kHz** (меньше писка, лучше для BTS7960). Пока **не меняем** — сначала проверяем проводку.

**Почему один TIM на два канала:** один таймер, два независимых «газа» — левый и правый ШИМ с одной частотой.

### Один модуль BTS7960 (первый тест)

Подключи **одну** сторону (левый драйвер по комментариям):

| BTS7960 | → | NUCLEO / питание |
|---------|---|------------------|
| **GND** | → | GND (CN6 или Morpho) |
| **VCC** | → | **5V** (CN6-5) — логика модуля |
| **L_EN** | → | **PB0** (A3) |
| **R_EN** | → | **PB1** (Morpho CN10-24) или тоже HIGH через перемычку на 3.3V |
| **LPWM** | → | **PB6** (D10) |
| **RPWM** | → | **GND** (пока только «назад» канал не нужен) *или* оставь не подключённым и крути только LPWM |
| **B+ / B−** | → | аккумулятор **через предохранитель** (не от USB NUCLEO) |
| **M+ / M−** | → | один мотор |

Общая **GND** аккумулятора и NUCLEO обязательна.

Для **вперёд на одном драйвере:** LPWM = PWM, RPWM = 0 (GND), оба EN = HIGH.

### Два модуля BTS7960 (лево / право колёса)

| Сторона | LPWM | RPWM | L_EN | R_EN |
|---------|------|------|------|------|
| **Левый** драйвер | PB6 (D10) | GND | PB0 (A3) | PB0 или 3.3V HIGH |
| **Правый** драйвер | GND | PB7 (CN7-21) | PB1 или 3.3V | PB1 (CN10-24) |

Так два PWM преподавателя = **по одному «газу» на сторону**, только **вперёд** (для учебного старта). Задний ход — позже (второй PWM на каждый драйвер или pinmap из Lecture 6).

### Не трогать

| Pin | Почему |
|-----|--------|
| **PA2, PA3** | USART2 → USB к Pi / Mac |
| **PA5** | LD2 — уже мигает, проверка прошивки |
| **PA13, PA14** | SWD — отладка |

### Расхождение с Lecture 6 (на будущее)

Lecture 6: **4 PWM** (PA0/PA1 + PA6/PA7) + **4 EN** — полный вперёд/назад.  
Преподаватель: **2 PWM** (PB6/PB7) + **2 EN** — проще, сначала « газ на сторону ». Переход на Lecture 6 = правка CubeMX + прошивки + перепайка.


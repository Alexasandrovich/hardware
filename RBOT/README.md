# RBOT — Pi client + mock STM32 + STM32 firmware

Протокол: [[../Lecture 8 - RBOT Protocol]]

## Быстрый старт (Pi или Mac)

```bash
cd RBOT
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt
pytest -q
python -m rbot demo --mock
```

## Реальный NUCLEO (Robot_car + RBOT)

1. Прошить `Robot_car` в CubeIDE (файлы `Core/Src/rbot_*.c` должны быть в проекте).
2. USB NUCLEO → USB Pi
3. ```bash
   cd RBOT
   sudo apt install python3-serial
   export PYTHONPATH=$PWD
   python3 drive_arrows.py --port /dev/ttyACM0
   ```

Стрелки: ↑↓←→, пробел — стоп, Q — выход.

## Wi-Fi hotspot + браузер (web_drive)

1. Скопируй на Pi папку `RBOT` (или `web_drive.py` + `rbot/`).
2. Hotspot:
   ```bash
   chmod +x scripts/setup_hotspot.sh
   sudo ROBOT_SSID=RobotCar ROBOT_PASS=robotcar123 ./scripts/setup_hotspot.sh up
   ```
3. Подключись с телефона к Wi-Fi `RobotCar`, открой `http://10.42.0.1:8080`.
4. На Pi:
   ```bash
   sudo apt install python3-serial python3-flask
   cd RBOT
   export PYTHONPATH=$PWD
   python3 web_drive.py --port /dev/ttyACM0
   ```

Удерживай кнопку направления в браузере — как стрелки в `drive_arrows.py`. E-STOP — красная кнопка внизу.

Выключить hotspot: `sudo ./scripts/setup_hotspot.sh down`

Тест без стрелок:
```bash
python -m rbot demo --port /dev/ttyACM0 --drive 30 30 --seconds 2
```

## Структура

```text
RBOT/
  rbot/           # Python: protocol, client, mock, logger
  tests/
  stm32/
    nucleo-f411re-rbot/   # CubeIDE: .ioc + App/rbot
```

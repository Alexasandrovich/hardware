#!/bin/bash
# Wi-Fi hotspot на Raspberry Pi для управления роботом с телефона.
# Требует NetworkManager (Raspberry Pi OS Bookworm+).

set -euo pipefail

SSID="${ROBOT_SSID:-RobotCar}"
PASS="${ROBOT_PASS:-robotcar123}"
CON_NAME="${ROBOT_CON_NAME:-RobotHotspot}"
IFACE="${ROBOT_IFACE:-wlan0}"
ACTION="${1:-up}"

usage() {
  cat <<EOF
Использование: $0 [up|down|status]

  up      — поднять hotspot (по умолчанию)
  down    — выключить hotspot
  status  — показать состояние

Переменные:
  ROBOT_SSID      SSID (по умолчанию: RobotCar)
  ROBOT_PASS      пароль WPA (по умолчанию: robotcar123)
  ROBOT_CON_NAME  имя профиля nmcli (RobotHotspot)
  ROBOT_IFACE     интерфейс Wi-Fi (wlan0)

После up открой в браузере телефона:
  http://10.42.0.1:8080
EOF
}

need_root() {
  if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    echo "Запусти с sudo: sudo $0 $ACTION" >&2
    exit 1
  fi
}

need_nmcli() {
  if ! command -v nmcli >/dev/null 2>&1; then
    echo "nmcli не найден. На Pi: sudo apt install network-manager" >&2
    exit 1
  fi
}

warn_ssh_disconnect() {
  if [[ -n "${SSH_CONNECTION:-}" ]]; then
    echo ""
    echo "ВНИМАНИЕ: ты в SSH. После up Wi-Fi переключится в AP — сессия оборвётся (Broken pipe)."
    echo "Это нормально. Подключись к Wi-Fi $SSID и зайди: ssh alex@10.42.0.1"
    echo "Или запускай скрипт с локального терминала Pi / по Ethernet."
    echo ""
  fi
}

write_status_file() {
  local ip="${1:-10.42.0.1}"
  cat > /tmp/robot_hotspot.info <<EOF
Hotspot: $SSID
Password: $PASS
Pi IP: $ip
Web UI: http://${ip}:8080
EOF
}

hotspot_up() {
  need_root
  need_nmcli
  warn_ssh_disconnect
  write_status_file

  if nmcli -t -f NAME connection show | grep -qx "$CON_NAME"; then
    echo "Профиль $CON_NAME уже есть — поднимаю…"
    nmcli connection up "$CON_NAME" || true
  else
    echo "Создаю hotspot $SSID на $IFACE…"
    nmcli device wifi hotspot ifname "$IFACE" con-name "$CON_NAME" ssid "$SSID" password "$PASS"
  fi

  nmcli connection modify "$CON_NAME" connection.autoconnect yes 2>/dev/null || true

  IP=$(nmcli -g IP4.ADDRESS device show "$IFACE" 2>/dev/null | head -1 | cut -d/ -f1)
  IP="${IP:-10.42.0.1}"
  write_status_file "$IP"

  echo ""
  echo "Hotspot готов."
  echo "  SSID:     $SSID"
  echo "  Пароль:   $PASS"
  echo "  Pi IP:    $IP"
  echo "  Web UI:   http://${IP}:8080"
  echo "  Статус:   cat /tmp/robot_hotspot.info"
  echo ""
  echo "Запусти web_drive:"
  echo "  cd ~/RBOT && PYTHONPATH=. python3 web_drive.py"
}

hotspot_down() {
  need_root
  need_nmcli
  nmcli connection down "$CON_NAME" 2>/dev/null || true
  echo "Hotspot $CON_NAME выключен."
}

hotspot_status() {
  need_nmcli
  nmcli -f NAME,TYPE,DEVICE,STATE connection show --active
  echo "---"
  nmcli device wifi list 2>/dev/null | head -5 || true
}

case "$ACTION" in
  up) hotspot_up ;;
  down) hotspot_down ;;
  status) hotspot_status ;;
  -h|--help|help) usage ;;
  *)
    echo "Неизвестная команда: $ACTION" >&2
    usage
    exit 1
    ;;
esac

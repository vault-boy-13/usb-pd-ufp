# USB PD UFP

[EN](#english) · [RU](#русский)

## English

USB PD UFP is an optional Atmosphère sysmodule for Nintendo Switch. When a compatible USB-C PD power source is connected, it can request a USB data-role switch so the console can remain a USB device while charging.

The module changes the USB data role only. It does not provide USB streaming, file transfer, or any other USB function; another app or sysmodule must provide that.

### Compatibility and limitations

- Hardware testing has been limited to one Nintendo Switch Lite and one ROHM BM92T36 controller revision (`04b5:03b0`, firmware `0603`, revision `0d7c`). Other controller revisions are ignored.
- Support for other Switch models and normal dock operation has not been confirmed. Do not assume compatibility with every charger, hub, or dock.
- The module checks power-source and dock indicators before acting. If state cannot be read or is ambiguous, it does nothing.
- It sends at most one role-switch request per connection. A request that has already been sent is not automatically undone when the module is disabled or removed; disconnect and reconnect USB/PD to restore a fresh connection state.

### Installation

1. Download the latest release archive from the project's Releases page and extract it on your computer.
2. Copy the `atmosphere/` and `config/` folders from the archive to the root of the SD card.
3. Check that title ID `42000000000000C1` is not already used by another sysmodule, then reboot the console.

To update, keep a backup of the existing module folder and your `config/usb-pd-ufp/config.ini` before copying the new files.

### Configuration and logs

The module reads `config/usb-pd-ufp/config.ini` at startup and reloads it every five seconds. Set `enabled=0` to prevent new role-switch requests. Set `dry_run=1` to log when a request would be made without sending it. A new connection or sleep/wake cycle is needed before a previously skipped request can be considered again.

Logs are written to `config/usb-pd-ufp/log.txt`; the previous log is rotated to `log.prev.txt` at 1 MiB. Logs include USB/PD state and request results. Reported voltage and current are system values, not physical measurements.

### Disable and uninstall

To stop new requests without removing the files, set `enabled=0` or create `config/usb-pd-ufp/disable.flag`. The flag is checked while the module is running; config changes are picked up within five seconds. Disabling does not undo a request already sent. To uninstall, remove `atmosphere/contents/42000000000000C1/` from the SD card and reboot. Remove `config/usb-pd-ufp/` if you no longer need its settings or logs.

### License and acknowledgments

Original project code is licensed under [MIT](LICENSE). The binary uses libnx; see [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Nintendo Switch is a trademark of Nintendo; this independent project is not affiliated with or endorsed by Nintendo.

## Русский

USB PD UFP — необязательный sysmodule для Atmosphère. При подключении совместимого источника USB-C PD он может запросить переключение роли данных USB, чтобы консоль оставалась USB-устройством во время зарядки.

Модуль меняет только роль порта. Он не добавляет передачу видео, файлов или другие функции USB — для них нужно отдельное приложение или sysmodule.

### Совместимость и ограничения

- На аппаратуре модуль проверен только с одной Switch Lite и одной ревизией контроллера ROHM BM92T36 (`04b5:03b0`, firmware `0603`, revision `0d7c`). Другие ревизии контроллера пропускаются.
- Работа на других моделях Switch и со штатным доком не подтверждена. Совместимость со всеми зарядками, хабами и доками не гарантируется.
- Перед переключением модуль проверяет источник питания и признаки дока. Если состояние не удалось прочитать или оно неоднозначно, модуль ничего не делает.
- На одно подключение отправляется не более одного запроса переключения. Уже отправленный запрос не отменяется при отключении модуля; для нового состояния USB/PD отсоедините и подключите кабель заново.

### Установка

1. Скачайте последний архив из раздела Releases проекта и распакуйте его на компьютере.
2. Скопируйте папки `atmosphere/` и `config/` из архива в корень SD-карты.
3. Убедитесь, что title ID `42000000000000C1` не занят другим sysmodule, затем перезагрузите консоль.

Перед обновлением сохраните резервную копию папки модуля и своего `config/usb-pd-ufp/config.ini`.

### Настройка и логи

Модуль читает `config/usb-pd-ufp/config.ini` при запуске и перечитывает его каждые пять секунд. Укажите `enabled=0`, чтобы запретить новые запросы переключения. Значение `dry_run=1` включает наблюдение без отправки команды: в лог записывается, когда запрос был бы отправлен. Чтобы повторно рассмотреть пропущенный запрос, нужно новое подключение или цикл сна и пробуждения.

Лог записывается в `config/usb-pd-ufp/log.txt`; при достижении 1 МиБ предыдущий лог переименовывается в `log.prev.txt`. В нём есть состояние USB/PD и результат запроса. Напряжение и ток из системного отчёта не являются физическими измерениями.

### Отключение и удаление

Чтобы запретить новые запросы без удаления файлов, задайте `enabled=0` или создайте `config/usb-pd-ufp/disable.flag`. Модуль проверяет флаг во время работы; изменения конфига применяются не позднее чем через пять секунд. Отключение не отменяет уже отправленный запрос. Для удаления сотрите с SD-карты `atmosphere/contents/42000000000000C1/` и перезагрузите консоль. Если настройки и логи больше не нужны, удалите `config/usb-pd-ufp/`.

### Лицензия и благодарности

Оригинальный код проекта распространяется по лицензии [MIT](LICENSE). Бинарная сборка использует libnx; см. [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Nintendo Switch — товарный знак Nintendo; проект независимый, не связан с Nintendo и не одобрен ею.

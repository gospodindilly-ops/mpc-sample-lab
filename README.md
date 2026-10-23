**Русский** | [English](README.en.md)

# MPC Sample Lab

Личное исследование Akai MPC Sample (AC50, прошивка 1.3.0.12). Здесь собрано всё, что уже сделано: root-доступ без изменения прошивки, карта платформы, read-only бэкап eMMC, полный протокол панели управления, свой звук и экран и своя прошивка Sample Mode. Всё проверено на железе. Прошивка запускается из RAM вместо штатного приложения и ничего не пишет во флеш.

> В репозитории **нет** прошивки Akai, образов eMMC, бэкапов и ключей: `.gitignore` не даст их закоммитить. Ключ challenge-response панели (XTEA) намеренно не приведён. В `captures/`, `recon/` и `backup-manifest/` описан один экземпляр устройства. Перед публикацией (2026-10-23) они проверены: серийных номеров, MAC-адресов и ключей нет, а хеши разделов не раскрывают их содержимое.

## Что внутри

| Путь | Что это |
|---|---|
| [docs/control-surface-protocol.md](docs/control-surface-protocol.md) | Протокол панели: формат RBIN, МК, все сообщения пэдов, кнопок, ручек и светодиодов, SysEx. Отмечено, что взято из кода, а что проверено на устройстве |
| [firmware/](firmware/) | **Своя прошивка, этап 1 — Sample Mode по руководству:**<br>• 8 банков × 16 пэдов, 32 голоса;<br>• огибающие, тюн, mono/poly, mute-группы, луп, реверс, Note On;<br>• страницы B1–B3, ручки K1–K3, энкодер, фейдер Pad Volume;<br>• светодиоды, экран с формой волны и огибающей.<br>`make test` — самотест на Mac/Linux, `make` — бинарь для устройства |
| [docs/firmware-roadmap.md](docs/firmware-roadmap.md) | Все функции руководства по этапам: что сделано, что дальше |
| [docs/audio-display.md](docs/audio-display.md) | Звук (I2S, AK4558, переключатели ALSA) и экран (SPI ST7796S через DRM) |
| [sampler/sampler.c](sampler/sampler.c) | Шаг к прошивке: 16 WAV с SD на пэды, MAIN VOLUME, поток реального времени |
| [sound-test/sound_test.c](sound-test/sound_test.c) | Тест звука и экрана: тон на пэд, сетка пэдов через DRM |
| [panel-demo/panel.c](panel-demo/panel.c) | Первое своё приложение: читает панель, зажигает светодиод нажатой кнопки, красит ударенный пэд |
| [tools/az0x.py](tools/az0x.py) | Разбор и распаковка `Update.img` (контейнер AZ0x, только чтение) |
| [tools/rbin.py](tools/rbin.py) | Разбор `UpdateImage.rbin` прошивки панели и помощники для дизассемблирования (capstone) |
| [tools/emmc_backup.sh](tools/emmc_backup.sh) | Read-only бэкап всех разделов eMMC на SD с SHA-256 |
| [tools/surface_capture.sh](tools/surface_capture.sh) | 3-минутная запись сообщений панели (только приём), ac50 всегда запускается обратно |
| [captures/](captures/) | Записи панели и журналы запусков на устройстве |
| [recon/](recon/) | Вывод read-only разведки на устройстве |
| [backup-manifest/](backup-manifest/) | Хеши и таблица разделов бэкапа eMMC, хеш `Update.img` (без самих образов) |
| [patcher/](patcher/) | Ранний каркас патчера (история сохранена). Устарел, см. ниже |

## Устройство (подтверждено)

| | |
|---|---|
| SoC | Rockchip **RK3566**, 4 × Cortex-A55, 2 ГБ RAM без swap |
| ОС | Yocto «az0x» 5.0.14 (scarthgap), ядро 6.12.60-rt (PREEMPT_RT), userland BusyBox |
| eMMC 7.3 ГБ | 12 разделов: spl1/2, env1/2, uboot1/2, factory, boot, recovery, rootfs, content, data |
| Загрузка | U-Boot 2024.01. FIT загрузчика и ядра подписан `sha256,rsa2048`, ключ `akai-1`; подписаны kernel, fdt, ramdisk и cmdline |
| Корневая ФС | **dm-verity** (хеш корня в подписанном cmdline). Запись в rootfs сломает загрузку |
| Запись | `/data` = `/var/lib` (p12). `/factory` (p7) — **не трогать** |
| SD | `/media/MPC SAMPLE`, смонтирована с `noexec` |
| Аудио | I2S + AK4558; USB: UAC2 и f_midi (gadget) |
| Панель | отдельный МК класса STM32G0 на UART3, 4 Мбит/с, MIDI — см. docs |
| Радио | Wi-Fi нет; UART для Bluetooth в DT выключен |

Эталон прошивки: `Update.img` 1.3.0.12, SHA-256 в [backup-manifest/update-img.SHA256SUMS](backup-manifest/update-img.SHA256SUMS). Разделы boot, recovery и rootfs на устройстве побайтно совпадают с ним.

## Доступ к устройству

- **Root по SSH.** Временный доступ по USB даёт сторонний набор [orrpunk-art/mpc-sample-root-ssh](https://github.com/orrpunk-art/mpc-sample-root-ssh) на коммите `8334ca08` (прочитан целиком перед использованием). Ключи создаёт его `setup_unix.sh`, а папка `autorun/` кладётся в корень SD (exFAT).
  ```bash
  ssh -p 2222 -i ~/.ssh/mpc_sample_client_ed25519 root@192.168.155.1
  ```
- **Копирование файлов** без sftp (в наборе его нет): `ssh … 'cat > /run/имя' < имя`.
- **Свои бинарники** запускаются только из `/run` или `/tmp`: на SD стоит `noexec`.
- **Запуск своего приложения вместо штатного.** systemd сам останавливает ac50 и гарантированно запускает его обратно: после выхода, падения или лимита времени.
  ```bash
  ssh … 'systemd-run --unit=NAME --collect -p RuntimeMaxSec=600 -p ExecStartPre="/usr/bin/systemctl stop ac50.service" -p ExecStopPost="/usr/bin/systemctl start ac50.service" /run/BINARY'
  ```

## Бэкап eMMC

`ssh … 'sh -s' < tools/emmc_backup.sh` только читает eMMC и пишет в `emmc-backup-ДАТА/` на SD. Бэкап от 2026-10-22 хранится на SD и в `~/code/mpc-sample-analysis/emmc-backup-2026-10-22/`, проверен `shasum -c`.
- Раздел `p12-data.img` снимался, пока менялся. Для `/data` используйте `p12-data-files.tar`.
- **Способ восстановления не проверен.**

## Панель управления: шпаргалка

Подробности в [docs/control-surface-protocol.md](docs/control-surface-protocol.md).
- **Пэды:** канал 10. `99 нота сила`, затем `A9 нота давление` и `89 нота 00`. Ноты пэдов 1–16: 37 36 42 82 40 38 46 44 48 47 45 43 49 55 51 53.
- **Кнопки:** канал 1, `90 нота 7F` и `80 нота 00`. 24 кнопки, таблица в docs, §8.
- **Ручки:** 1–4 → CC 19, 16, 17, 18. Фейдер → CC 20 (абсолютное значение 0–127). Большой энкодер → CC 100 (относительный: `01` = +1, `7F` = −1).
- **Светодиод кнопки:** `B0 <нота кнопки> <значение>`. Одиночный: 0 / 1 / ≥2. Двухцветный: 0–8 = 3·A + B.
- **Цвет пэда:** `F0 47 7F 5C 65 00 04 <пэд 0–15> <R> <G> <B> F7`, компоненты 7-битные.
- **Рукопожатие не нужно:** панель шлёт события и без штатного приложения.

## Демо-приложение

```bash
cd panel-demo && zig cc -target aarch64-linux-musl -static -O2 -o mpc-panel-demo panel.c
```
```bash
cc -DSELFTEST -o panel-test panel.c && ./panel-test
```
Развернуть: скопировать в `/run/mpc-panel-demo` и запустить по шаблону из раздела «Доступ к устройству». Выход: `systemctl stop mpc-panel-demo` или Ctrl-C. Проверено 2026-10-22: панель читается, кнопки светятся, пока их держат, пэды окрашиваются от синего к красному по силе удара.

## Своя прошивка

```bash
cd firmware && make test   # самотест движка, контроллера, экрана и WAV (ASan/UBSan)
cd firmware && make        # статический бинарь aarch64 (zig cc), ~100 КБ
```
Развернуть: скопировать в `/run/mpcfw` и запустить по шаблону из раздела «Доступ к устройству», передав папку с WAV 44,1 кГц:
```bash
ssh … 'systemd-run --unit=mpcfw --collect -p RuntimeMaxSec=900 -p ExecStartPre="/usr/bin/systemctl stop ac50.service" -p ExecStopPost="/usr/bin/systemctl start ac50.service" /run/mpcfw "/media/MPC SAMPLE/папка"'
```
Первые 128 файлов папки ложатся на банки A–H. При выходе прошивка печатает расход CPU по потокам и самый долгий рендер. Замер 2026-10-22: 3,3% одного ядра, рендер 124 мкс из 2902, 0 underrun, 37 МБ.

## patcher/

Каркас от 2026-10-19: патчи бинаря приложения в RAM с bind-mount. **На железе не запускался.** Его способ активации (`sdcard/scripts/autorun`) не совпадает с проверенным механизмом штатного автозапуска (`autorun/autorun.json` + `autorun.check` в корне SD), поэтому перед использованием его надо переделать.

## Статус плана

| # | Шаг | Статус |
|---|---|---|
| 1 | Исходное состояние | ✅ 1.3.0.12, проект сохранён |
| 2 | Офлайн-разбор прошивки | ✅ |
| 3 | Read-only разведка на устройстве | ✅ |
| 4 | Бэкап разделов | ✅ два экземпляра, проверены |
| 5 | Свои программы без изменения прошивки | ✅ протокол панели, звук, экран, прошивка этапа 1 |
| 6 | Патчи / альтернативная прошивка | ⏳ зависит от secure boot |

**Открыто:**
- аппаратный secure boot (eFuse) и способ восстановления (апдейтер, recovery, maskrom);
- ряд светодиодов CC 57–64 (предположительно буквы банков A–H), индекс 16 в 0x65, назначение 0x42;
- следующие этапы прошивки: фильтр, Chop, 16 Levels, браузер, запись, секвенсор, эффекты (см. дорожную карту).

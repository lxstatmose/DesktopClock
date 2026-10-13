# DesktopClock

Настольные часы на **ESP32-S3 SuperMini**: круглый экран **GC9A01 240×240**,
круглый UI на **LVGL 9**, датчики воздуха **ENS160 + AHT21**, ёмкостная кнопка
**TTP223**. Погода — **OpenWeatherMap**, метрики сервера и ПК — через локальный
хаб `server/clock-hub.py`, уведомления на телефон — через **ntfy.sh**.

## Экраны

Короткое нажатие — следующий экран, длинное (≥ 0.6 с) — предыдущий.

| # | Экран | Что показывает |
|---|---|---|
| 1 | Часы | время (NTP), дата |
| 2 | Погода | OpenWeatherMap: T, ощущается, влажность, ветер, давление |
| 3 | Дом | ENS160 + AHT21: T, RH, eCO2, TVOC, AQI |
| 4 | Сервер | метрики домашнего сервера с хаба |
| 5 | ПК | метрики рабочего ПК с хаба |
| 6 | Система | IP, RSSI, uptime, свободная RAM, температура чипа |

## Железо

| Модуль | Сигнал | GPIO |
|---|---|---|
| GC9A01 | SCLK / MOSI / DC / CS / RES / BL | 4 / 6 / 5 / 7 / 8 / 10 |
| AHT21 (I2C) | SDA / SCL | 1 / 2 (адрес 0x38) |
| ENS160 (I2C) | та же шина | 0x52 / 0x53 / 0x59 (автоопределение) |
| TTP223 | OUT | 11 (касание = HIGH) |

Пины — в `include/pin_config.h`. Безопасные GPIO у SuperMini: 1, 2, 4–8, 9–11,
15–18, 21, 33–38.

## Сборка

Нужны VS Code с расширением PlatformIO (или консольный `pio`).

```bash
pio run               # сборка
pio run -t upload     # прошивка
pio device monitor    # лог, 115200
```

Платформа `espressif32@^6.10.0`, фреймворк Arduino. До 4 МБ flash
(`huge_app.csv`), PSRAM не используется.

## Настройка

```bash
cp include/app_config_secrets.example.h include/app_config_secrets.h
cp server/hub.env.example              server/hub.env
```

Оба файла с секретами в `.gitignore`. `include/app_config.h` включает
`app_config_secrets.h`, остальное (таймзону, интервалы, пороги аварий, pinned
ядро для UI) стоит смотреть там же.

| Что | Где |
|---|---|
| Wi-Fi, SOCKS5, ключ OpenWeatherMap, адрес хаба, топик ntfy | `app_config_secrets.h` |
| Таймзона (`MSK-3`), NTP, интервалы опроса, яркость, пороги уведомлений | `app_config.h` |
| Токен и chat_id бота, токен хаба | `server/hub.env` |

## Структура

```
platformio.ini
boards/desktop_clock_s3.json     # 4 МБ flash, 3 МБ app, без PSRAM
include/
  app_config.h                   # настройки (в git)
  app_config_secrets*.h          # секреты (в git только .example)
  pin_config.h                   # карта выводов
  lgfx_config.hpp                # класс LGFX под GC9A01
  lv_conf.h                      # конфиг LVGL 9
src/
  display/                       # LovyanGFX + LVGL, flush по DMA, 2 буфера
  ui/                            # theme, 6 экранов, слайд-переходы, шрифты
  input/button.*                 # TTP223: короткое / длинное нажатие
  sensors/ens160_aht21.*         # ENS160 + AHT21 на одной шине I2C
  net/                           # wi-fi, NTP, SOCKS5-клиент, сетевая задача
  services/                      # weather, metrics, notify
server/clock-hub.py              # хаб метрик + Telegram-бот (stdlib Python)
tools/                           # TTF Roboto для шрифтов, замер ширины текста
```

UI и LVGL живут на ядре 1, сеть и датчики — на ядре 0 (`net_task`), состояние
делится через мьютекс.

## Хаб метрик

```bash
python3 server/clock-hub.py --port 8765                              # хаб
python3 server/clock-hub.py --agent server --hub http://<hub>:8765/api/report
python3 server/clock-hub.py --agent pc     --hub http://<hub>:8765/api/report
```

Схема: агент шлёт `POST /api/report` → хаб хранит последний снимок → плата
забирает `GET /api/state`. Зависимость только для агента: `psutil`.

## Шрифты

Встроенные шрифты LVGL без кириллицы, поэтому в `src/ui/fonts/` лежат
шрифты, сгенерированные из Roboto (`tools/fonts/`):

```bash
npx lv_font_conv --font tools/fonts/Roboto-Regular.ttf --size 20 --bpp 4 \
  --format lvgl --lv-include lvgl.h -o src/ui/fonts/lv_font_roboto_20.c \
  -r 0x20-0x7F,0xB0,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x2116,0x400-0x45F
```

## Особенности

- Прямой выход в интернет с платы на этой сети не работает (роутер обрывает
  данные после TCP-хендшейка), поэтому погода и ntfy идут через SOCKS5-прокси
  роутера — `WEATHER_USE_SOCKS5`. Метрики от хаба забираются обычным HTTP
  в локалке, прокси там не нужен.
- `api.telegram.org` из этой сети недоступен, поэтому уведомления на телефон
  уходят через ntfy.sh; хаб умеет Telegram-бота, но он работает только там,
  где доступен `api.telegram.org`.
- CPU работает на 160 МГц (`CPU_FREQ_MHZ`) — меньше нагрева, по плавности
  разницы нет.
- HTTPS-запросы с платы идут с `setInsecure()`: данные публичные, экономия
  RAM важнее проверки сертификата.

## Лицензии сторонних компонентов

LVGL (MIT), LovyanGFX (BSD), ArduinoJson (MIT), ScioSense_ENS16x (MIT),
PTSolns_AHTx (MIT), Roboto (Apache-2.0).

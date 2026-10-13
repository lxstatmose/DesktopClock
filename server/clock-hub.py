#!/usr/bin/env python3
"""
clock-hub.py — хаб для настенных часов DesktopClock.

Схема работы:
    агент на сервере/ПК  --POST /api/report-->  хаб (хранит последний снимок)
    ESP32-плата           --GET  /api/state -->  хаб (раз в METRICS_POLL_MS)
    Telegram-бот          --long polling-->     хаб (команды /status и др.)
    бот                   --POST /api/notify--> хаб (очередь уведомлений)

Хаб — единственная точка, где живёт состояние. Ни плата, ни бот не ходят
друг к другу напрямую: плата не умеет Telegram, бот не видит локалку.

Зависимостей нет: только стандартная библиотека Python.

Запуск:
    export TG_TOKEN=123456:ABC...       # токен от BotFather
    export TG_CHAT_ID=12345678          # id чата с ботом
    export HUB_TOKEN=случайная-строка   # плата шлёт его в заголовке
    python3 clock-hub.py --port 8765
"""

import argparse
import json
import os
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ---------------------------------------------------------------- настройки

PORT = int(os.environ.get("HUB_PORT", "8765"))
TG_TOKEN = os.environ.get("TG_TOKEN", "").strip()
TG_CHAT_ID = os.environ.get("TG_CHAT_ID", "").strip()
HUB_TOKEN = os.environ.get("HUB_TOKEN", "").strip()
BOT_NAME = os.environ.get("BOT_NAME", "часы")

# Данные старше этого (секунды) считаем протухшими.
STALE_SEC = int(os.environ.get("HUB_STALE_SEC", "120"))

# Как часто агент опрашивает систему (режим --agent).
AGENT_INTERVAL = int(os.environ.get("AGENT_INTERVAL", "10"))

_lock = threading.Lock()

# device -> {"name":..., "cpu":..., "mem_used":..., ..., "ts": unix}
devices = {}

# Уведомления для платы: id растёт монотонно, плата берёт последнее.
notices = []
next_notice_id = 1

# Сообщения, которые бот должен ОТПРАВИТЬ в Telegram.
outbox = []


def now_ts():
    return int(time.time())


# ---------------------------------------------------------------- метрики

def report_device(dev, payload):
    """Сохранить снимок метрик устройства."""
    with _lock:
        devices[dev] = {
            "name": payload.get("name") or dev,
            "cpu": float(payload.get("cpu", 0.0)),
            "mem_used": int(payload.get("mem_used", 0)),
            "mem_total": int(payload.get("mem_total", 0)),
            "temp": float(payload.get("temp", -999.0)),
            "load1": float(payload.get("load1", -1.0)),
            "uptime": int(payload.get("uptime", 0)),
            "ts": now_ts(),
        }
    return True


def snapshot():
    """Состояние для платы; протухшие снимки помечаем stale."""
    with _lock:
        out = {}
        for dev in ("server", "pc"):
            d = devices.get(dev)
            if not d:
                continue
            if now_ts() - d["ts"] > STALE_SEC:
                d = dict(d, stale=True)
            out[dev] = d
        if notices:
            out["notice"] = notices[-1]
    return out


def push_notice(text):
    """Положить сообщение в очередь для платы и в очередь для Telegram."""
    global next_notice_id
    with _lock:
        n = {"id": next_notice_id, "text": text[:95], "ts": now_ts()}
        next_notice_id += 1
        notices.append(n)
        notices[:] = notices[-20:]          # храним только последние 20
        outbox.append(text)
    print("[hub] уведомление #%d: %s" % (n["id"], n["text"]), flush=True)
    return n


def ack_notice(nid):
    """Плата подтвердила, что показала уведомление — забываем его."""
    with _lock:
        notices[:] = [n for n in notices if n["id"] > nid]


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        print("[hub] %s - %s" % (self.address_string(), fmt % args), flush=True)

    def _send(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _auth(self):
        """Агент и плата должны предъявить HUB_TOKEN."""
        if not HUB_TOKEN:
            return True                       # токен не настроен — открыто
        return self.headers.get("X-Hub-Token", "") == HUB_TOKEN

    def _read_json(self):
        n = int(self.headers.get("Content-Length", 0))
        if n <= 0:
            return {}
        return json.loads(self.rfile.read(n).decode("utf-8"))

    # ---- GET ----
    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path

        if path == "/api/state":
            if not self._auth():
                return self._send(403, {"error": "bad token"})
            return self._send(200, snapshot())

        if path == "/health":
            return self._send(200, {"ok": True,
                                    "devices": sorted(devices.keys()),
                                    "notices": len(notices)})

        return self._send(404, {"error": "not found"})

    # ---- POST ----
    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        if not self._auth():
            return self._send(403, {"error": "bad token"})

        try:
            body = self._read_json()
        except Exception as e:
            return self._send(400, {"error": str(e)})

        if path == "/api/report":
            dev = body.get("device")
            if dev not in ("server", "pc"):
                return self._send(400,
                                  {"error": "device должен быть server или pc"})
            report_device(dev, body)
            return self._send(200, {"ok": True})

        if path == "/api/notice/ack":
            ack_notice(int(body.get("id", 0)))
            return self._send(200, {"ok": True})

        if path == "/api/notify":
            # сюда кладёт текст бот (/say) или внешний скрипт
            text = body.get("text", "")
            if not text:
                return self._send(400, {"error": "пустой text"})
            push_notice(text)
            return self._send(200, {"ok": True})

        return self._send(404, {"error": "not found"})


# ---------------------------------------------------------------- Telegram

API = "https://api.telegram.org/bot%s/%s"


def tg_call(method, **payload):
    """Вызов Bot API; None при сетевой ошибке."""
    url = API % (TG_TOKEN, method)
    data = urllib.parse.urlencode(payload).encode("utf-8")
    req = urllib.request.Request(
        url, data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=25) as r:
            return json.loads(r.read().decode("utf-8"))
    except Exception as e:
        print("[tg] %s: %s" % (method, e), flush=True)
        return None


def tg_send(text, chat_id=None):
    if not TG_TOKEN or not (chat_id or TG_CHAT_ID):
        print("[tg] токен/чат не заданы — не отправлено: %s" % text)
        return
    tg_call("sendMessage", chat_id=(chat_id or TG_CHAT_ID), text=text,
            parse_mode="Markdown", disable_web_page_preview="true")


def fmt_device(dev):
    """Человекочитаемый снимок устройства для Telegram."""
    d = devices.get(dev)
    if not d:
        return "%s: данных нет (агент молчит)" % ("СЕРВЕР" if dev == "server"
                                                  else "ПК")

    lines = ["*%s*" % d["name"], "ЦП: %.0f%%" % d["cpu"]]

    if d["mem_total"]:
        lines.append("RAM: %d/%d МБ (%.0f%%)" % (
            d["mem_used"] // 1048576, d["mem_total"] // 1048576,
            100.0 * d["mem_used"] / max(1, d["mem_total"])))

    if d["temp"] > -900:
        lines.append("Темп: %.0f°C" % d["temp"])
    if d["load1"] >= 0:
        lines.append("Load: %.2f" % d["load1"])
    if d["uptime"]:
        lines.append("Аптайм: %d д %d ч" % (d["uptime"] // 86400,
                                          (d["uptime"] % 86400) // 3600))

    age = now_ts() - d["ts"]
    if age > STALE_SEC:
        lines.append("_данные устарели: %d с назад_" % age)
    return "\n".join(lines)


def handle_command(text):
    t = text.split()[0].lower().split("@")[0]
    if t in ("/start", "/help"):
        return ("Команды:\n"
                "/status — обе машины\n"
                "/server — домашний сервер\n"
                "/pc — компьютер\n"
                "/ping — проверить связь\n"
                "/say текст — показать текст на часах")
    if t == "/status":
        return fmt_device("server") + "\n\n" + fmt_device("pc")
    if t in ("/server", "/srv"):
        return fmt_device("server")
    if t == "/pc":
        return fmt_device("pc")
    if t == "/ping":
        return "pong, связь есть"
    if t == "/say":
        parts = text.split(None, 1)
        if len(parts) < 2:
            return "Что сказать часам? Например: /say Привет"
        push_notice(parts[1])
        return "Отправил на часы"
    return "Не знаю такую команду. /help"


def bot_loop():
    """Long polling: забираем апдейты, отвечаем, раздаём накопленное."""
    offset = 0
    print("[tg] бот запущен (%s), жду команды…" % BOT_NAME, flush=True)

    while True:
        res = tg_call("getUpdates", offset=str(offset), timeout=20)
        if not res or not res.get("ok"):
            time.sleep(5)
            continue

        for upd in res.get("result", []):
            offset = upd["update_id"] + 1
            msg = upd.get("message") or upd.get("edited_message")
            if not msg or not msg.get("text"):
                continue

            chat = (msg.get("chat") or {}).get("id")
            if TG_CHAT_ID and str(chat) != str(TG_CHAT_ID):
                continue                       # отвечаем только в свой чат

            tg_send(handle_command(msg["text"].strip()), chat_id=chat)

        # раздаём то, что накопилось (например, из /say)
        with _lock:
            pending, outbox[:] = list(outbox), []
        for t in pending:
            tg_send(t)


# ---------------------------------------------------------------- агент

def host_cpu_percent():
    """Загрузка ЦП хоста по двум снимкам /proc/stat.

    В контейнере psutil.process_cpu_percent() смотрит на изолированные
    диапазоны cgroup, поэтому надёжнее взять общие счётчики ядра.
    Два вызова подряд отдают 0 — мерим между снимками с паузой.
    """
    def snap():
        parts = [float(x) for x in
                 open("/proc/stat").read().split("\n")[0].split()[1:]]
        return parts[3] + parts[4], sum(parts)

    i1, t1 = snap()
    time.sleep(0.5)                       # окно измерения
    i2, t2 = snap()

    dt, di = t2 - t1, i2 - i1
    return round(100.0 * (1.0 - di / dt), 1) if dt > 0 else 0.0


def collect(name, host_view):
    """Собрать метрики машины.

    host_view=True  — читаем /proc и /sys хоста (контейнер на сервере);
    host_view=False — обычный режим, метрики этой же машины (агент на ПК).
    """
    import psutil

    d = {"name": name, "cpu": 0.0, "mem_used": 0, "mem_total": 0,
         "temp": -999.0, "load1": -1.0, "uptime": 0}

    if host_view:
        # ---------------- метрики хоста ----------------
        d["cpu"] = host_cpu_percent()

        info = {}
        for line in open("/proc/meminfo").read().splitlines():
            k, _, v = line.partition(":")
            info[k] = int(v.split()[0]) * 1024
        d["mem_total"] = info.get("MemTotal", 0)
        d["mem_used"] = d["mem_total"] - info.get(
            "MemAvailable", info.get("MemFree", 0))

        try:
            d["uptime"] = int(float(open("/proc/uptime").read().split()[0]))
        except (OSError, ValueError):
            pass
        try:
            d["load1"] = round(os.getloadavg()[0], 2)
        except OSError:
            pass

        # температура: перебираем типовые пути thermal/hwmon
        import glob
        for pat in ("/sys/class/thermal/thermal_zone*/temp",
                    "/sys/class/hwmon/hwmon*/temp*_input"):
            for path in sorted(glob.glob(pat)):
                try:
                    raw = float(open(path).read().strip())
                except (OSError, ValueError):
                    continue
                if raw <= 0:
                    continue
                celsius = raw / 1000.0 if raw > 200 else raw
                if 0 < celsius < 120:           # отсекаем мусор датчиков
                    d["temp"] = celsius
                    break
            if d["temp"] > -900:
                break
        return d

    # ---------------- своя машина (агент на ПК) ----------------
    vm = psutil.virtual_memory()
    d["cpu"] = psutil.cpu_percent(interval=None)
    d["mem_used"] = vm.used
    d["mem_total"] = vm.total
    d["uptime"] = int(time.time() - psutil.boot_time())

    try:
        temps = [t.current for t in psutil.sensors_temperatures().values() if t]
        if temps:
            d["temp"] = max(temps)
    except (AttributeError, OSError):
        pass                                   # не Linux — температуры нет

    if hasattr(os, "getloadavg"):
        try:
            d["load1"] = round(os.getloadavg()[0], 2)
        except OSError:
            pass

    return d


def run_agent(device, name, hub_url):
    """Раз в AGENT_INTERVAL шлём снимок метрик на хаб."""
    print("[agent] %s (%s) -> %s каждые %d с" %
          (device, name, hub_url, AGENT_INTERVAL), flush=True)

    # первый вызов cpu_percent(interval=None) всегда даёт 0.0 — прогреваем,
    # чтобы первое же значение было осмысленным
    import psutil
    psutil.cpu_percent(interval=None)

    while True:
        try:
            payload = collect(name)
            payload["device"] = device
            req = urllib.request.Request(
                hub_url,
                data=json.dumps(payload).encode("utf-8"),
                headers={"Content-Type": "application/json",
                         "X-Hub-Token": HUB_TOKEN},
                method="POST")
            urllib.request.urlopen(req, timeout=5).read()
            print("[agent] %s: ЦП %.0f%%, RAM %d МБ" %
                  (device, payload["cpu"],
                   payload["mem_used"] // 1048576), flush=True)
        except Exception as e:
            print("[agent] отправка не удалась: %s" % e, flush=True)

        time.sleep(AGENT_INTERVAL)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="Хаб часов + Telegram-бот")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--agent", choices=["server", "pc"],
                    help="запустить только агент этой машины")
    ap.add_argument("--name", default=None, help="имя машины для бота")
    ap.add_argument("--hub", default=None,
                    help="URL хаба для агента (по умолчанию 127.0.0.1)")
    args = ap.parse_args()

    if args.agent:
        default_name = {"server": "Дом-сервер", "pc": "Компьютер"}[args.agent]
        hub = args.hub or "http://127.0.0.1:%d/api/report" % args.port
        run_agent(args.agent, args.name or default_name, hub)
        return

    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)

    if TG_TOKEN:
        threading.Thread(target=bot_loop, daemon=True).start()
    else:
        print("[tg] TG_TOKEN не задан — бот выключен, хаб работает",
              flush=True)

    print("[hub] слушаю 0.0.0.0:%d (хаб-токен %s)" %
          (args.port, "задан" if HUB_TOKEN else "НЕ ЗАДАН — открытый доступ"),
          flush=True)

    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n[hub] остановлен")


if __name__ == "__main__":
    main()


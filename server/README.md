# Токен бота и секрет хаба НЕ хранятся в репозитории.
# Скопируй пример и заполни под себя:
cp hub.env.example hub.env && chmod 600 hub.env

# Хаб на сервере (приём метрик, раздача для платы, Telegram-бот)
python3 clock-hub.py --port 8765

# Агент для ПК (на самой машине, отправляет раз в 10 с)
python3 clock-hub.py --agent pc --hub http://192.168.2.2:8765/api/report
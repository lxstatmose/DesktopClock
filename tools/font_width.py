"""Замер ширины строк по метрикам сгенерированных шрифтов Roboto.

Нужен, чтобы убедиться: текст с оценкой («TVOC 12 ppb • отлично»)
влезает внутрь кольцевой шкалы (радиус 104 px от центра экрана).
"""
import os
import re
import sys

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    '..', 'src', 'ui', 'fonts')


def load_adv(size):
    """{codepoint: adv_w} из lv_font_roboto_<size>.c."""
    path = os.path.join(BASE, 'lv_font_roboto_%d.c' % size)
    with open(path, encoding='utf-8', errors='replace') as f:
        src = f.read()

    # Комментарий вида: /* U+0020 " " */ ... {.bitmap_index = 0, .adv_w = 79, ...}
    pat = re.compile(
        r'/\*\s*U\+([0-9A-Fa-f]{4,6})\s+"[^"]*"\s*\*/\s*'
        r'\{?\s*\.bitmap_index\s*=\s*\d+\s*,\s*\.adv_w\s*=\s*(\d+)')
    adv = {}
    for cp, w in pat.findall(src):
        adv[int(cp, 16)] = int(w)
    return adv


def text_w(adv, text):
    """Ширина в px (adv_w хранится в 1/4 px) и список отсутствующих глифов."""
    total = 0
    missing = []
    for ch in text:
        cp = ord(ch)
        if cp in adv:
            total += adv[cp]
        else:
            missing.append('U+%04X' % cp)
    return total / 4.0, missing


def main():
    ring_r = 104.0

    # (текст, размер, смещение центра от центра экрана)
    rows = [
        ('23.5°C', 28, -60),
        ('Влажность 100%', 16, -26),
        ('452 ppm • хорошо', 20, 18),
        ('452 ppm • вредно', 20, 18),
        ('TVOC 999 ppb • отлично', 16, 50),
        ('TVOC 999 ppb • средне', 16, 50),
        ('AQI 1 • отлично', 16, 80),
        ('AQI 5 • вредно', 16, 80),
    ]

    cache = {}
    print('%-28s %4s %5s %8s %8s  %s' %
          ('текст', 'разм', 'шир.', 'доп.пол', 'нужно', 'вердикт'))
    for text, size, dy in rows:
        if size not in cache:
            cache[size] = load_adv(size)
        w, missing = text_w(cache[size], text)
        if missing:
            print('%-28s %4d  MISSING: %s' % (repr(text), size, ','.join(missing)))
            continue
        allow = 2.0 * ((ring_r ** 2 - dy ** 2) ** 0.5)
        ok = 'OK' if w <= allow else 'НЕ ВЛЕЗАЕТ'
        print('%-28s %4d %5.0f %8.0f %8.0f  %s' %
              (text, size, w, allow, w, ok))


if __name__ == '__main__':
    sys.exit(main())

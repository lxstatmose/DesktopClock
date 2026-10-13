#include "weather_icon.h"
#include "theme.h"

#include <string.h>

struct WIconParts {
    lv_obj_t *sun;
    lv_obj_t *cloud[4];
    lv_obj_t *precip[3];
    lv_obj_t *fog[3];
};

static lv_obj_t *mk_circle(lv_obj_t *p, int d, lv_color_t c, int x, int y) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *mk_bar(lv_obj_t *p, int w, int h, lv_color_t c, int x, int y) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w < 1 ? 1 : w, h < 1 ? 1 : h);
    lv_obj_set_style_radius(o, (h < 2 ? 1 : h / 2), 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_scrollable(o, false);
    return o;
}

static void show(lv_obj_t *o, bool v) {
    if (!o) return;
    lv_obj_set_hidden(o, !v);
}

static void weather_icon_delete_cb(lv_event_t *e) {
    WIconParts *p = (WIconParts *)lv_event_get_user_data(e);
    if (p) {
        lv_free(p);
    }
}

lv_obj_t *weather_icon_create(lv_obj_t *parent, int size) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_scrollable(box, false);

    WIconParts *p = (WIconParts *)lv_malloc(sizeof(WIconParts));
    memset(p, 0, sizeof(WIconParts));
    lv_obj_set_user_data(box, p);
    lv_obj_add_event_cb(box, weather_icon_delete_cb, LV_EVENT_DELETE, p);

    const int S = size;

    /* солнце / луна */
    p->sun = mk_circle(box, (int)(S * 0.44f), COL_ACCENT2,
                       (int)(S * 0.08f), (int)(S * 0.08f));

    /* облако: 3 круга + основание */
    p->cloud[0] = mk_circle(box, (int)(S * 0.40f), COL_CLOUD, (int)(S * 0.16f), (int)(S * 0.44f));
    p->cloud[1] = mk_circle(box, (int)(S * 0.52f), COL_CLOUD, (int)(S * 0.33f), (int)(S * 0.30f));
    p->cloud[2] = mk_circle(box, (int)(S * 0.34f), COL_CLOUD, (int)(S * 0.58f), (int)(S * 0.44f));
    p->cloud[3] = mk_bar(box, (int)(S * 0.74f), (int)(S * 0.24f), COL_CLOUD,
                         (int)(S * 0.16f), (int)(S * 0.56f));

    /* осадки */
    for (int i = 0; i < 3; i++) {
        p->precip[i] = mk_bar(box, (int)(S * 0.08f), (int)(S * 0.24f), COL_ACCENT,
                              (int)(S * (0.24f + 0.20f * i)), (int)(S * 0.82f));
    }

    /* туман */
    for (int i = 0; i < 3; i++) {
        p->fog[i] = mk_bar(box, (int)(S * 0.62f), (int)(S * 0.08f), COL_CLOUD,
                           (int)(S * 0.19f), (int)(S * (0.40f + 0.18f * i)));
    }

    weather_icon_set(box, 0, 1);
    return box;
}

void weather_icon_set(lv_obj_t *icon, int wmoCode, int isDay) {
    WIconParts *p = (WIconParts *)lv_obj_get_user_data(icon);
    if (!p) return;

    const bool storm = (wmoCode >= 95);
    const bool snow  = ((wmoCode >= 71 && wmoCode <= 77) || wmoCode == 85 || wmoCode == 86);
    const bool rain  = ((wmoCode >= 51 && wmoCode <= 67) || (wmoCode >= 80 && wmoCode <= 82));
    const bool fog   = (wmoCode == 45 || wmoCode == 48);
    const bool cloud = (wmoCode >= 2) || fog || storm;
    const bool sun   = (wmoCode <= 2);

    show(p->sun, sun && !storm && !fog);
    for (int i = 0; i < 4; i++) show(p->cloud[i], cloud || rain || snow);
    for (int i = 0; i < 3; i++) show(p->precip[i], rain || storm);
    for (int i = 0; i < 3; i++) show(p->fog[i], fog);

    /* цвет осадков: дождь — синий, снег — белый, гроза — жёлтый */
    lv_color_t pc = storm ? COL_ACCENT2 : (snow ? COL_TEXT : COL_ACCENT);
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(p->precip[i], pc, 0);
    }

    /* ночью «солнце» превращаем в луну */
    if (sun) {
        lv_obj_set_style_bg_color(p->sun, isDay ? COL_ACCENT2 : lv_color_hex(0xD8DEE4), 0);
    }
}

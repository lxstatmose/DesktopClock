#include "theme.h"

lv_obj_t *ui_box(lv_obj_t *parent, int w, int h) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_center(o);
    lv_obj_set_scrollable(o, false);
    return o;
}

void ui_screen_style(lv_obj_t *scr) {
    /* Экран (parent == NULL) тема оформляет стилем scr — просто фон.
     * Панель-наследник получает стиль card: рамка, радиус, отступы —
     * отсюда «границы» по краю и смещённый контент (кривая раскладка).
     * Снимаем всё и задаём фон явно — вид как у настоящего экрана. */
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(scr, false);
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                   const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

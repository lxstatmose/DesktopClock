#include "ui_manager.h"
#include "screens.h"
#include "theme.h"
#include "app_config.h"

#define UI_SCREEN_COUNT 6
#define UI_ANIM_MS      550   /* длительность слайда, мс */

/* Кривая движения: cubic-bezier(0.4, 0, 0.2, 1) — «material standard».
 * Дольше тормозит в конце, чем разгоняется в начале: смена экрана
 * воспринимается мягче, чем симметричный ease-in-out (0.42/0/0.58/1).
 * Параметры в фиксированной точке LVGL (0..1024 = 0..1) — LV_BEZIER_VAL_FLOAT.
 * Настройка «под себя»: 0,0,0.2,1 — резче старт; 0.4,0,0.6,1 — мягче конец. */
#define UI_EASE_X1  LV_BEZIER_VAL_FLOAT(0.4f)
#define UI_EASE_Y1  0
#define UI_EASE_X2  LV_BEZIER_VAL_FLOAT(0.2f)
#define UI_EASE_Y2  LV_BEZIER_VAL_FLOAT(1.0f)

/* Все «экраны» — это панели-дочерние объекты единого корневого экрана.
 * Встроенные переходы LVGL (lv_screen_load_anim) анимируются линейно
 * (lv_anim_path_linear) — движение стартует и обрывается мгновенно,
 * выглядит рывками. Здесь слайд делаем сами: та же механика MOVE_LEFT/RIGHT
 * (панели едут навстречу друг другу, зазоров нет), но с плавным
 * bezier-разгоном/торможением.
 *
 * Нажатие во время анимации НЕ обрывает её (раньше панели прыгали
 * в конечные позиции мгновенно — это и было заметным рывком).
 * Направления складываются в маленькую очередь и проигрываются
 * последовательными слайдами после завершения текущего. */
static lv_obj_t *s_root;                     /* единственный экран дисплея */
static lv_obj_t *s_panel[UI_SCREEN_COUNT];
static int       s_cur = 0;

static lv_obj_t *s_incoming = nullptr;       /* панель, въезжающая сейчас */
static lv_obj_t *s_outgoing = nullptr;       /* панель, уезжающая сейчас */

/* Очередь нажатий, пришедших во время анимации (FIFO, до 4 штук). */
#define UI_PENDING_MAX 4
static int8_t s_pending[UI_PENDING_MAX];
static uint8_t s_pendingLen = 0;

static void pending_push(int dir) {
    if (s_pendingLen >= UI_PENDING_MAX) return;   /* зажим — теряем лишнее */
    s_pending[s_pendingLen++] = (int8_t)dir;
}

static int8_t pending_pop() {
    if (s_pendingLen == 0) return 0;
    const int8_t d = s_pending[0];
    for (uint8_t i = 1; i < s_pendingLen; i++) s_pending[i - 1] = s_pending[i];
    s_pendingLen--;
    return d;
}

static void slide_exec(void *obj, int32_t v) {
    lv_obj_set_x((lv_obj_t *)obj, v);
}

static void ui_go(int dir);   /* определена ниже; slide_done запускает из очереди */

/* Общая настройка анимации слайда (одна кривая/длительность у обеих панелей,
 * иначе края разойдутся и появится щель). */
static void slide_setup(lv_anim_t *a) {
    lv_anim_set_duration(a, UI_ANIM_MS);
    lv_anim_set_path_cb(a, lv_anim_path_custom_bezier3);
    lv_anim_set_bezier3_param(a, UI_EASE_X1, UI_EASE_Y1, UI_EASE_X2, UI_EASE_Y2);
    lv_anim_set_exec_cb(a, slide_exec);
}

/* Завершение перехода: убираем уехавшую панель и, если во время анимации
 * накопились нажатия, запускаем следующий слайд из очереди.
 * (Вызывается из таймера LVGL; lv_anim_start внутри completed_cb безопасен —
 *  LVGL явно помечает изменение списка анимаций в своём обработчике.) */
static void slide_done(lv_anim_t *a) {
    (void)a;
    if (s_outgoing) {
        lv_obj_set_hidden(s_outgoing, true);
        lv_obj_set_pos(s_outgoing, 0, 0);
    }
    s_incoming = nullptr;
    s_outgoing = nullptr;

    const int8_t dir = pending_pop();
    if (dir != 0) ui_go(dir);
}

void ui_init() {
    s_root = lv_obj_create(nullptr);
    ui_screen_style(s_root);
    lv_obj_set_scrollable(s_root, false);

    s_panel[0] = scr_clock_create(s_root);
    s_panel[1] = scr_weather_create(s_root);
    s_panel[2] = scr_home_create(s_root);
    s_panel[3] = scr_server_create(s_root);
    s_panel[4] = scr_pc_create(s_root);
    s_panel[5] = scr_system_create(s_root);

    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        lv_obj_set_size(s_panel[i], LCD_H_RES, LCD_V_RES);
        lv_obj_set_pos(s_panel[i], 0, 0);
        lv_obj_set_hidden(s_panel[i], i != 0);
    }

    s_cur = 0;
    lv_screen_load(s_root);
}

static void ui_go(int dir) {
    /* Нажатие во время анимации — в очередь, без обрыва: панели не прыгают,
     * слайды проигрываются последовательно и связно. */
    if (s_outgoing) {
        pending_push(dir);
        return;
    }

    int next = s_cur + dir;
    if (next < 0)                 next = UI_SCREEN_COUNT - 1;
    if (next >= UI_SCREEN_COUNT)  next = 0;
    if (next == s_cur) return;

    lv_obj_t *in  = s_panel[next];
    lv_obj_t *out = s_panel[s_cur];

    /* Въезжающая панель — с края; уезжающая стартует из центра.
     * Обе едут с одинаковым easing, поэтому их края всё время
     * смыкаются вплотную (in.x == out.x ± LCD_H_RES). */
    const int32_t inStart  = (dir > 0) ? LCD_H_RES : -LCD_H_RES;
    const int32_t outEnd   = (dir > 0) ? -LCD_H_RES : LCD_H_RES;

    lv_obj_set_hidden(in, false);
    lv_obj_set_pos(in, inStart, 0);
    lv_obj_set_pos(out, 0, 0);

    s_incoming = in;
    s_outgoing = out;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, in);
    lv_anim_set_values(&a, inStart, 0);
    slide_setup(&a);
    lv_anim_set_completed_cb(&a, slide_done);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, out);
    lv_anim_set_values(&a, 0, outEnd);
    slide_setup(&a);
    lv_anim_start(&a);

    s_cur = next;
}

void ui_next() { ui_go(+1); }
void ui_prev() { ui_go(-1); }

void ui_update() {
    switch (s_cur) {
        case 0: scr_clock_update();   break;
        case 1: scr_weather_update(); break;
        case 2: scr_home_update();     break;
        case 3: scr_server_update();   break;
        case 4: scr_pc_update();       break;
        case 5: scr_system_update();   break;
        default: break;
    }
}

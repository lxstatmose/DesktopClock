/**
 * @file lv_conf.h
 * Конфигурация LVGL 9.6 для DesktopClock (ESP32-S3).
 * Подключается через -DLV_CONF_INCLUDE_SIMPLE (файл лежит в include/).
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*============================================================================
 * ПАМЯТЬ И СТАНДАРТНАЯ БИБЛИОТЕКА
 *==========================================================================*/
/* ESP32 имеет хороший аллокатор — используем его (без статического пула). */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*============================================================================
 * ОПЕРАЦИОННАЯ СИСТЕМА
 *==========================================================================*/
#define LV_USE_OS               LV_OS_NONE

/*============================================================================
 * РЕНДЕРИНГ
 *==========================================================================*/
#define LV_COLOR_DEPTH          16
#define LV_USE_DRAW_SW          1
#define LV_DEF_REFR_PERIOD      16      /* ~60 Гц */
#define LV_DPI_DEF              130

/*============================================================================
 * ЛОГИРОВАНИЕ
 *==========================================================================*/
#define LV_USE_LOG              1
#if LV_USE_LOG
    #define LV_LOG_LEVEL        LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF       1
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_TRACE_MEM    0
    #define LV_LOG_TRACE_TIMER  0
    #define LV_LOG_TRACE_INDEV  0
    #define LV_LOG_TRACE_DISP_REFR 0
    #define LV_LOG_TRACE_EVENT  0
    #define LV_LOG_TRACE_OBJ_CREATE 0
    #define LV_LOG_TRACE_LAYOUT 0
    #define LV_LOG_TRACE_ANIM   0
    #define LV_LOG_TRACE_CACHE  0
#endif

/*============================================================================
 * ПРОВЕРКИ (ASSERTS)
 *==========================================================================*/
#define LV_USE_ASSERT_NULL             1
#define LV_USE_ASSERT_MALLOC           1
#define LV_USE_ASSERT_STYLE            0
#define LV_USE_ASSERT_MEM_INTEGRITY    0
#define LV_USE_ASSERT_OBJ              0

/*============================================================================
 * ШРИФТЫ
 *==========================================================================*/
/* Встроенные Montserrat отключаем — используем сгенерированный Roboto с кириллицей */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   0
#define LV_FONT_MONTSERRAT_16   0
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0
#define LV_FONT_SIMSUN_16_CJK   0

#define LV_FONT_CUSTOM_DECLARE \
    LV_FONT_DECLARE(lv_font_roboto_14) \
    LV_FONT_DECLARE(lv_font_roboto_16) \
    LV_FONT_DECLARE(lv_font_roboto_20) \
    LV_FONT_DECLARE(lv_font_roboto_28) \
    LV_FONT_DECLARE(lv_font_roboto_40) \
    LV_FONT_DECLARE(lv_font_roboto_48) \
    LV_FONT_DECLARE(lv_font_roboto_72)

#define LV_FONT_DEFAULT         &lv_font_roboto_16
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_FONT_COMPRESSED  1

/*============================================================================
 * ТЕКСТ
 *==========================================================================*/
#define LV_TXT_ENC              LV_TXT_ENC_UTF8

/*============================================================================
 * ВИДЖЕТЫ
 *==========================================================================*/
/* Все остальные виджеты включены значениями по умолчанию (lv_conf_internal.h).
 * Явно отключаем только то, что не используется (экономия флеша).
 * ВАЖНО: не отключать BUTTONMATRIX/TEXTAREA — от них зависят другие виджеты. */
#define LV_USE_KEYBOARD         0
#define LV_USE_TABLE            0
#define LV_USE_CHART            0

/*============================================================================
 * ТЕМЫ
 *==========================================================================*/
#define LV_USE_THEME_DEFAULT    1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/*============================================================================
 * ЛЭЙАУТЫ
 *==========================================================================*/
#define LV_USE_FLEX             1
#define LV_USE_GRID             1

/*============================================================================
 * ПРОЧЕЕ
 *==========================================================================*/
#define LV_USE_SNAPSHOT         0
#define LV_USE_OBJ_PROPERTY     0

#endif /* LV_CONF_H */

/*
 * brightness - System > brightness slider.
 *
 * The backlight driver (AP3032) is only switched on/off by the CH422G (EXIO2),
 * so there are two ways to dim (CONFIG_BACKLIGHT_PWM_GPIO):
 *
 *   -1 (default)  software: a black layer on lv_layer_top() with opacity
 *                 100 % - brightness. Works on every board; the backlight
 *                 still draws full power, and dirty areas are blended once more.
 *   GPIO          hardware: LEDC PWM into the AP3032 FB node via the dimming
 *                 pads (R11 10K / C11 1uF filter). Higher average voltage =
 *                 dimmer, so the duty is inverted. Needs a wire on the board.
 *
 * Auto brightness needs a light sensor, which the board doesn't have: the
 * checkbox is disabled.
 */
#include "lvgl.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "screens.h"
#include "views.h"

#if CONFIG_BACKLIGHT_PWM_GPIO >= 0
#include "driver/ledc.h"
#endif

static const char *TAG = "brightness";

extern objects_t objects;

#define MIN_PERCENT     10
#define SW_MAX_OPA      LV_OPA_80       /* software dimming never goes fully black */

#if CONFIG_BACKLIGHT_PWM_GPIO >= 0
#define PWM_FREQ_HZ     20000
#define PWM_RES         LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY    ((1u << 10) - 1)

static void hw_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = PWM_RES, .freq_hz = PWM_FREQ_HZ, .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t ch = {
        .gpio_num = CONFIG_BACKLIGHT_PWM_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ESP_LOGI(TAG, "backlight PWM on GPIO %d", CONFIG_BACKLIGHT_PWM_GPIO);
}

static void apply(uint8_t percent)
{
    /* inverted: 0 % duty = full brightness */
    uint32_t duty = (uint32_t)(100 - percent) * PWM_MAX_DUTY / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

#else

static lv_obj_t *s_shade;

static void hw_init(void)
{
    s_shade = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_shade);
    lv_obj_set_size(s_shade, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_shade, lv_color_black(), 0);
    lv_obj_clear_flag(s_shade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "software dimming (no backlight PWM pin configured)");
}

static void apply(uint8_t percent)
{
    if (percent >= 100) {
        lv_obj_add_flag(s_shade, LV_OBJ_FLAG_HIDDEN);     /* no cost at full brightness */
        return;
    }
    lv_opa_t opa = (lv_opa_t)((100 - percent) * SW_MAX_OPA / (100 - MIN_PERCENT));
    lv_obj_set_style_bg_opa(s_shade, opa, 0);
    lv_obj_clear_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
}

#endif

static uint8_t s_applied;

void brightness_init(uint8_t percent)
{
    hw_init();
    lv_slider_set_value(objects.sys_man_brightness, percent, LV_ANIM_OFF);
    lv_obj_add_state(objects.sys_auto_brightness, LV_STATE_DISABLED);   /* no light sensor */
    brightness_set(percent);
}

void brightness_set(uint8_t percent)
{
    if (percent < MIN_PERCENT) {
        percent = MIN_PERCENT;
    } else if (percent > 100) {
        percent = 100;
    }
    if (percent != s_applied) {
        s_applied = percent;
        apply(percent);
    }
}

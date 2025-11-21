#include "my_ui_behavior.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "ui.h"

#include "esp_log.h"
#include "my_lcd.h"
#include "my_nvs.h"
#include "my_ble.h"
#include "my_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "UI_BEHAVIOR"

// 页面状态管理
static int current_page = PAGE_START;  // 当前页面

static bool have_alarm = false;
static bool is_online = false;

// Unwind动物选择相关
static unwind_animal_t current_unwind_animal = UNWIND_ANIMAL_CAT;  // 默认选择cat
// WakeModeTest相关变量
static int wakeModeTestIndex = 0;
static bool isWakeModeTestAnimating = false; // 跟踪动画是否正在进行

// Unwind图片和标签信息结构体
typedef struct {
    const lv_img_dsc_t* image;  // 照片
    const char* label;          // 标签
} UnwindImageInfo;

// 动物图片和标签映射表（按照枚举顺序：CAT, FOX, HUMMINGBIRD, KOI, SWAN）
static const UnwindImageInfo unwind_animal_info[UNWIND_ANIMAL_MAX] = {
    {&ui_img_cat_png, "Deep Rest"},           // CAT
    {&ui_img_fox_png, "Cosmic Calm"},         // FOX
    {&ui_img_hummingbird_png, "Nature Echo"}, // HUMMINGBIRD
    {&ui_img_koi_png, "Zen Flow"},            // KOI
    {&ui_img_swan_png, "Piano Drift"}         // SWAN
};

// 前向声明
static void update_unwind_display(void);
static void update_unwind_choose_animation(void);

void my_ui_set_light(uint8_t val) {
    if (val > 100) val = 100;
    bsp_lcd_bl_set(val);
}

void my_ui_in_start() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Boot);
    current_page = PAGE_START;
    is_online = false;
    lvgl_port_unlock();
}

// 动态生成二维码的函数
// 新格式: https://lunawake.ai?sn={device_id}
void my_ui_generate_qr_code(const char* url, const char* device_id) {
    static char qr_url[256];
    
    // 拼接完整URL: https://lunawake.ai?sn={device_id}
    snprintf(qr_url, sizeof(qr_url), "%s?sn=%s", 
             url ? url : "",
             device_id ? device_id : "");
    
    ESP_LOGI(TAG, "生成二维码URL: %s", qr_url);
    
    lvgl_port_lock(0);
    
    static lv_obj_t *qr_code_obj = NULL;
    // 检查是否已经有二维码对象，如果有先删除
    if (qr_code_obj != NULL) {
        lv_obj_del(qr_code_obj);
        qr_code_obj = NULL;
    }
    
    // 确保网络引导页面和图片对象已初始化
    if (ui_NetworkBoot != NULL && ui_NetworkBootContainer != NULL && ui_NetworkBootImage != NULL) {
        // 隐藏原始的静态图片
        lv_obj_add_flag(ui_NetworkBootImage, LV_OBJ_FLAG_HIDDEN);
        
        // 创建新的二维码对象，放在同一个容器中
        qr_code_obj = lv_qrcode_create(ui_NetworkBootContainer);
        
        if (qr_code_obj != NULL) {
            // 设置二维码大小和样式，与原图片保持一致 (200 * 159/256 ≈ 124)
            lv_qrcode_set_size(qr_code_obj, 124);
            lv_qrcode_set_dark_color(qr_code_obj, lv_color_hex(0xFFFFFF)); // 白色二维码
            lv_qrcode_set_light_color(qr_code_obj, lv_color_hex(0x000000)); // 黑色背景
            
            // 生成二维码内容
            lv_qrcode_update(qr_code_obj, qr_url, strlen(qr_url));
            
            // 设置位置与原图片相同
            lv_obj_set_x(qr_code_obj, 0);
            lv_obj_set_y(qr_code_obj, -40);
            lv_obj_set_align(qr_code_obj, LV_ALIGN_CENTER);
            
            // 应用与原图片相同的标志
            lv_obj_add_flag(qr_code_obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(qr_code_obj, LV_OBJ_FLAG_SCROLLABLE);
            
            ESP_LOGI(TAG, "动态二维码生成成功");
        } else {
            ESP_LOGE(TAG, "创建二维码对象失败，保留原始图片");
            // 如果创建失败，显示原始图片
            lv_obj_clear_flag(ui_NetworkBootImage, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        ESP_LOGE(TAG, "网络引导页面或图片对象未初始化");
    }
    
    lvgl_port_unlock();
}

void my_ui_network_guide() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_NetworkBoot);
    current_page = PAGE_NETWORK_GUIDE;
    lvgl_port_unlock();
}

void my_ui_network_connecting() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Connecting);
    current_page = PAGE_NETWORK_CONNECTING;
    is_online = false;
    lvgl_port_unlock();
}

void my_ui_network_conn_suc() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingSuccess);
    current_page = PAGE_NETWORK_CONN_SUC;
    is_online = true;
    lvgl_port_unlock();
}

void my_ui_network_conn_fail() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingFailed);
    is_online = false;
    lvgl_port_unlock();
}

void my_ui_network_offline() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OfflineMode);
    current_page = PAGE_NETWORK_OFFLINE;
    is_online = false;
    lvgl_port_unlock();
}

void my_ui_in_clock() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MianNoPerson);
    current_page = PAGE_CLOCK;
    lvgl_port_unlock();
}

static char hour_str[8] = {0};
static char min_str[8] = {0};
void my_ui_clock_set_now_time(uint8_t hour, uint8_t min) {
    lvgl_port_lock(0);
    
    // 新UI使用24小时制直接显示，不需要转换为12小时制
    // 设置小时显示 (ui_MainHour)
    snprintf(hour_str, sizeof(hour_str), "%02d", hour);
    lv_label_set_text(ui_MainHour, hour_str);
    
    // 设置分钟显示 (ui_MainMinute)
    snprintf(min_str, sizeof(min_str), "%02d", min);
    lv_label_set_text(ui_MainMinute, min_str);
    
    // 根据24小时制时间设置太阳或月亮图标
    // 0 = 太阳 (06:00-17:59), 1 = 月亮 (18:00-05:59)
    uint8_t emoji_type;
    if(hour >= 6 && hour < 18) {
        emoji_type = 0;  // 白天显示太阳
    } else {
        emoji_type = 1;  // 夜晚显示月亮
    }

    if(!is_online) {
        emoji_type = 2;
    }
    
    lvgl_port_unlock();
    
    // 在解锁后设置图标，避免死锁
    my_ui_clock_set_emoji(emoji_type);
    
    // 调试日志
    // ESP_LOGI("CLOCK", "时钟显示 - 24小时制: %02d:%02d, 图标: %d", hour, min, emoji_type);
}

void my_ui_clock_set_tips(const char* tips) {
    lvgl_port_lock(0);
    // lv_label_set_text(ui_MianNoPersonLabel2, tips);
    lvgl_port_unlock();
}

// 在主页面显示闹钟时间
void my_ui_clock_show_alarm(uint8_t hour, uint8_t min) {
    lvgl_port_lock(0);
    static char alarm_display_str[32] = {0};
    
    // 转换为12小时制
    uint8_t display_hour;
    const char* am_pm;
    
    if(hour == 0) {
        display_hour = 12;
        am_pm = "AM";
    } else if(hour < 12) {
        display_hour = hour;
        am_pm = "AM";
    } else if(hour == 12) {
        display_hour = 12;
        am_pm = "PM";
    } else {
        display_hour = hour - 12;
        am_pm = "PM";
    }
    
    snprintf(alarm_display_str, sizeof(alarm_display_str), "Alarm: %d:%02d %s", display_hour, min, am_pm);
    // lv_label_set_text(ui_MianNoPersonLabel2, alarm_display_str);
    lvgl_port_unlock();
}

// 在主页面显示无闹钟
void my_ui_clock_show_no_alarm(void) {
    lvgl_port_lock(0);
    // lv_label_set_text(ui_MianNoPersonLabel2, "No Alarm");
    lvgl_port_unlock();
}

void my_ui_clock_set_emoji(uint8_t emoji) {
    lvgl_port_lock(0);
    switch (emoji)
    {
    case 0:
        // lv_image_set_src(ui_MianNoPersonImage, &ui_img_1271368945);  // 太阳 🌞
        break;
    case 1:
        // TODO: 需要确认月亮图标资源，ui_img_1271505029 可能不存在
        // lv_image_set_src(ui_MianNoPersonImage, &ui_img_component_8_png);  // 默认图标
        break;
    case 2:
        // TODO: 需要确认离线图标资源，ui_img_806445133 可能不存在
        // lv_image_set_src(ui_MianNoPersonImage, &ui_img_component_8_png);  // 默认图标
        break;
    default:
        // lv_image_set_src(ui_MianNoPersonImage, &ui_img_component_8_png);
        break;
    }
    lvgl_port_unlock();
}

void my_ui_in_funtion() {
    lvgl_port_lock(0);
    lv_roller_set_selected(ui_MueuRoller, 0, LV_ANIM_OFF);
    lv_disp_load_scr(ui_Memu);
    current_page = PAGE_FUNCTION;
    lvgl_port_unlock();
}

void my_ui_function_menu_up() {
    if (!lvgl_port_lock(0)) {
        return;  // 如果获取锁失败，直接返回
    }
    uint32_t max = lv_roller_get_option_count(ui_MueuRoller);
    uint32_t cur = lv_roller_get_selected(ui_MueuRoller);
    if (cur == 0) {
        cur = max - 1;
    } else {
        cur -= 1;
    }
    lv_roller_set_selected(ui_MueuRoller, cur, LV_ANIM_ON);
    lvgl_port_unlock();
}

void my_ui_function_menu_down() {
    if (!lvgl_port_lock(0)) {
        return;  // 如果获取锁失败，直接返回
    }
    uint32_t max = lv_roller_get_option_count(ui_MueuRoller);
    uint32_t cur = lv_roller_get_selected(ui_MueuRoller);
    if (cur >= max - 1) {
        cur = 0;
    } else {
        cur += 1;
    }
    lv_roller_set_selected(ui_MueuRoller, cur, LV_ANIM_ON);
    lvgl_port_unlock();
}

uint32_t my_ui_function_get_index() {
    lvgl_port_lock(0);
    uint32_t cur = lv_roller_get_selected(ui_MueuRoller);
    lvgl_port_unlock();
    return cur;
}

void my_ui_in_alarm() {
    // 使用 ui_SetTime 作为设置闹钟的页面（可编辑）
    my_ui_alarm_set_time(8, 0);
    my_ui_alarm_set_title("Alarm");
    my_ui_alarm_set_tips("Rotate to adjust");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_SetTime);
    current_page = PAGE_ALARM;
    lvgl_port_unlock();
}

void my_ui_in_no_alarm() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_AlarmOFF);
    lvgl_port_unlock();
    have_alarm = false;
}

void my_ui_alarm_set_title(const char *title) {
    lvgl_port_lock(0);
    lv_label_set_text(ui_SetTimeLabel1, title);
    lvgl_port_unlock();
}
void my_ui_alarm_set_tips(const char *tips) {
    lvgl_port_lock(0);
    lv_label_set_text(ui_SetTimeLabel2, tips);
    lvgl_port_unlock();
}

void my_ui_alarm_set_time(uint8_t hour, uint8_t min) {
    lvgl_port_lock(0);
    static char alarm_hour_str[8] = {0};
    static char alarm_min_str[8] = {0};
    
    // ui_SetTime 使用24小时制，直接显示
    snprintf(alarm_hour_str, sizeof(alarm_hour_str), "%02d", hour);
    lv_label_set_text(ui_SetTimeHour, alarm_hour_str);
    
    snprintf(alarm_min_str, sizeof(alarm_min_str), "%02d", min);
    lv_label_set_text(ui_SetTimeMinute, alarm_min_str);
    
    lvgl_port_unlock();
    have_alarm = true;
}
void my_ui_alarm_get_time(uint8_t *hour, uint8_t *min) {
    lvgl_port_lock(0);
    
    // ui_SetTime 使用24小时制，直接获取
    const char* hour_text = lv_label_get_text(ui_SetTimeHour);
    *hour = (hour_text[0] - '0') * 10 + (hour_text[1] - '0');
    
    const char* min_text = lv_label_get_text(ui_SetTimeMinute);
    *min = (min_text[0] - '0') * 10 + (min_text[1] - '0');
    
    lvgl_port_unlock();
}

void my_ui_in_volume() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Volume_);
    current_page = PAGE_VOLUME;
    lvgl_port_unlock();
}

void my_ui_volume_set(int val) {
    lvgl_port_lock(0);
    if (val > 100) val = 100;
    if (val < 0) val = 0;
    
    // 设置弧形进度条的值
    lv_arc_set_value(ui_VolumeArc, val);
    
    // 设置音量文字
    static char vol_str[16] = {0};
    snprintf(vol_str, sizeof(vol_str), "Volume: %d%%", val);
    lv_label_set_text(ui_VolumeLabel, vol_str);
    
    // 当音量为0时，更换 ui_VolumeImage 为静音图标
    if(val == 0) {
        lv_image_set_src(ui_VolumeImage, &ui_img_volumeno_png);
    } else {
        lv_image_set_src(ui_VolumeImage, &ui_img_volumeyes_png);
    }
    
    lvgl_port_unlock();
}

uint8_t my_ui_volume_get() {
    lvgl_port_lock(0);
    uint8_t val = lv_arc_get_value(ui_VolumeArc);
    lvgl_port_unlock();
    return val;
}

void my_ui_in_time_set() {
    // 使用 ui_SetTime 页面设置本地时间
    my_ui_alarm_set_title("Local Time");
    my_ui_alarm_set_tips("Connect to Wi-Fi to sync\nautomatically");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_SetTime);
    current_page = PAGE_TIME_SET;
    lvgl_port_unlock();
}

void my_ui_time_set(uint8_t hour, uint8_t min) {
    my_ui_alarm_set_time(hour, min);
}
void my_ui_time_get(uint8_t *hour, uint8_t *min) {
    my_ui_alarm_get_time(hour, min);
}

void my_ui_in_unwind() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_UnwindSelet);
    current_page = PAGE_UNWIND;
    lvgl_port_unlock();
    
    // 显示当前选中的动物图片和标签
    update_unwind_display();
}

void my_ui_unwind_select_mode(uint8_t mode) {
    lvgl_port_lock(0);
    // TODO: 新UI只有 ui_UnwindSelet 和 ui_UnwindOn 两个屏幕
    // 需要重新设计模式选择逻辑
    switch (mode)
    {
    case 0:
        lv_disp_load_scr(ui_UnwindSelet);  // 选择屏幕
        break;
    case 1:
    case 2:
        // lv_disp_load_scr(ui_UnwindOn);  // 运行屏幕
        break;
    default:
        lv_disp_load_scr(ui_UnwindSelet);
        break;
    }
    lvgl_port_unlock();
}

// 进入UnwindOn页面（动物选择）
void my_ui_in_unwind_on(void) {
    lvgl_port_lock(0);
    // lv_disp_load_scr(ui_UnwindOn);
    current_page = PAGE_UNWIND_ON;
    // 设置当前动物图片
    my_ui_unwind_set_animal(current_unwind_animal);
    lvgl_port_unlock();
}

// 进入UnwindSelet页面（确认选择）
void my_ui_in_unwind_select(void) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_UnwindSelet);
    current_page = PAGE_UNWIND_SELECT;
    lvgl_port_unlock();
    
    // 同步显示选中的动物图片和标签
    update_unwind_display();
}

// 进入NoiseTime页面（时间设置）
void my_ui_in_noise_time(void) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_NoiseTime);
    current_page = PAGE_NOISE_TIME;
    // 设置默认选中30分钟（索引2）
    lv_roller_set_selected(ui_NoiseTimeRoller, 2, LV_ANIM_OFF);
    lvgl_port_unlock();
}

// 更新unwind界面显示
static void update_unwind_display(void) {
    ESP_LOGI(TAG, "update_unwind_display, current_page: %d, animal: %d", current_page, current_unwind_animal);
    
    if (!lvgl_port_lock(0)) {
        ESP_LOGW(TAG, "update_unwind_display: failed to lock lvgl");
        return;
    }
    
    // 直接检查当前屏幕是否是 UnwindSelet
    lv_obj_t* current_screen = lv_disp_get_scr_act(NULL);
    if (current_screen != ui_UnwindSelet) {
        ESP_LOGW(TAG, "update_unwind_display: not in UnwindSelet screen");
        lvgl_port_unlock();
        return;
    }
    
    // 停止圆弧动画
    lv_anim_del(ui_UnwindSeletArc, NULL);
    
    // 重置圆弧值
    lv_arc_set_value(ui_UnwindSeletArc, 0);
    
    // 更新图片和标签
    if (current_unwind_animal < UNWIND_ANIMAL_MAX) {
        ESP_LOGI(TAG, "update_unwind_display: setting image and label for animal %d", current_unwind_animal);
        lv_image_set_src(ui_UnwindSeletImage, unwind_animal_info[current_unwind_animal].image);
        lv_label_set_text(ui_UnwindSeletLabel, unwind_animal_info[current_unwind_animal].label);
        lv_obj_invalidate(ui_UnwindSeletImage);
        lv_obj_invalidate(ui_UnwindSeletLabel);
        ESP_LOGI(TAG, "update_unwind_display: label set to '%s'", unwind_animal_info[current_unwind_animal].label);
    } else {
        ESP_LOGW(TAG, "update_unwind_display: invalid animal index %d", current_unwind_animal);
    }
    
    lvgl_port_unlock();
}

// 创建圆弧动画
static void update_unwind_choose_animation(void) {
    ESP_LOGI(TAG, "update_unwind_choose_animation, current_page: %d", current_page);
    
    if (!lvgl_port_lock(0)) {
        ESP_LOGW(TAG, "update_unwind_choose_animation: failed to lock lvgl");
        return;
    }
    
    // 直接检查当前屏幕是否是 UnwindSelet
    lv_obj_t* current_screen = lv_disp_get_scr_act(NULL);
    if (current_screen != ui_UnwindSelet) {
        ESP_LOGW(TAG, "update_unwind_choose_animation: not in UnwindSelet screen");
        lvgl_port_unlock();
        return;
    }
    
    ESP_LOGI(TAG, "update_unwind_choose_animation: starting arc animation");
    
    //TODO:// 创建圆弧动画 甲方要求先去掉
    // lv_anim_t arcAnim;
    // lv_anim_init(&arcAnim);
    // lv_anim_set_var(&arcAnim, ui_UnwindSeletArc);
    // lv_anim_set_values(&arcAnim, 0, 360);
    // lv_anim_set_time(&arcAnim, 300);  // 300ms
    // lv_anim_set_exec_cb(&arcAnim, (lv_anim_exec_xcb_t)lv_arc_set_value);
    // lv_anim_set_path_cb(&arcAnim, lv_anim_path_ease_out);  // ease-out缓动函数
    // lv_anim_start(&arcAnim);
    
    lvgl_port_unlock();
}

// 设置当前选择的动物
void my_ui_unwind_set_animal(unwind_animal_t animal) {
    if (animal >= UNWIND_ANIMAL_MAX) {
        animal = UNWIND_ANIMAL_CAT;
    }
    current_unwind_animal = animal;
    
    // 更新显示
    update_unwind_display();
    
    // 播放选择动画
    update_unwind_choose_animation();
}

// 获取当前选择的动物
unwind_animal_t my_ui_unwind_get_animal(void) {
    return current_unwind_animal;
}

// 切换到下一个动物
void my_ui_unwind_next_animal(void) {
    ESP_LOGI(TAG, "my_ui_unwind_next_animal, current: %d", current_unwind_animal);
    unwind_animal_t next_animal = (unwind_animal_t)((current_unwind_animal + 1) % UNWIND_ANIMAL_MAX);
    ESP_LOGI(TAG, "my_ui_unwind_next_animal, next: %d", next_animal);
    my_ui_unwind_set_animal(next_animal);
}

// 切换到上一个动物
void my_ui_unwind_prev_animal(void) {
    ESP_LOGI(TAG, "my_ui_unwind_prev_animal, current: %d", current_unwind_animal);
    unwind_animal_t prev_animal = (unwind_animal_t)((current_unwind_animal + UNWIND_ANIMAL_MAX - 1) % UNWIND_ANIMAL_MAX);
    ESP_LOGI(TAG, "my_ui_unwind_prev_animal, prev: %d", prev_animal);
    my_ui_unwind_set_animal(prev_animal);
}

// 获取NoiseTime roller当前选中项
uint8_t my_ui_noise_time_get_selection(void) {
    return lv_roller_get_selected(ui_NoiseTimeRoller);
}

void my_ui_in_good_morning() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MorningAnimation);
    current_page = PAGE_GOOD_MORNING;
    lvgl_port_unlock();
}

void my_ui_good_morning_set_emoji(uint8_t emoji) {
    lvgl_port_lock(0);
    // TODO: 需要确认早安页面的图标资源
    switch (emoji)
    {
    case 0:
        // lv_image_set_src(ui_MorningAnimationImage, &ui_img_moon);
        break;
    case 1:
        // lv_image_set_src(ui_MorningAnimationImage, &ui_img_1271368945);
        break;
    default:
        // lv_image_set_src(ui_MorningAnimationImage, &ui_img_default);
        break;
    }
    lvgl_port_unlock();
}

void my_ui_good_morning_set_txt(const char* txt) {
    lvgl_port_lock(0);
    lv_label_set_text(ui_MorningAnimationLabel, txt);
    lvgl_port_unlock();
}

void my_ui_in_sleep_data() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_sleepData);

    current_page = PAGE_SLEEP_DATA;
    lvgl_port_unlock();
}

// 进入Detection页面
void my_ui_in_detection(void) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Detection);
    current_page = PAGE_DETECTION;
    lvgl_port_unlock();
}

// 进入DetectionN页面
void my_ui_in_detection_n(void) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_DetectionN);
    current_page = PAGE_DETECTION_N;
    lvgl_port_unlock();
}

// 进入DetectionY页面
void my_ui_in_detection_y(void) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_DetectionY);
    current_page = PAGE_DETECTION_Y;
    lvgl_port_unlock();
}

// 进入DetectionN1页面
void my_ui_in_detection_n1(void) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_DetectionN1);
    current_page = PAGE_DETECTION_N1;
    lvgl_port_unlock();
}

// 进入MianYesPerson页面
void my_ui_in_mian_yes_person(void) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MianYesPerson);
    current_page = PAGE_MIAN_YES_PERSON;
    lvgl_port_unlock();
}

void my_ui_in_ota() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OTA);
    current_page = PAGE_OTA;
    lvgl_port_unlock();
}

void my_ui_ota_set_progress(uint8_t progress) {
    lvgl_port_lock(0);
    if (progress > 100) progress = 100;
    lv_slider_set_value(ui_OTASlider, progress, LV_ANIM_ON);
    lvgl_port_unlock();
}

// 雷达数据更新函数实现
void my_ui_radar_update_presence(bool presence) {
    // TODO: 新UI中暂无雷达数据显示组件 ui_state
    // 需要在新UI中添加相应的显示组件
    lvgl_port_lock(0);
    // if (ui_state != NULL) {
    //     lv_label_set_text(ui_state, presence ? "HAVE" : "NOBODY");
    // }
    lvgl_port_unlock();
}

void my_ui_radar_update_movement(uint8_t movement) {
    // 根据movement_param控制主页面图片的显示/隐藏
    // movement > 5: 有人 -> 显示图片
    // movement <= 5: 没人 -> 隐藏图片
    
    bool has_person = (movement > 5);
    my_ui_main_image_set_visibility(has_person);
    
    lvgl_port_lock(0);
    // if (ui_Body_movement != NULL) {
    //     if(movement == 0) {
    //         lv_obj_add_flag(ui_Body_movement, LV_OBJ_FLAG_HIDDEN);
    //     } else {
    //         char text[16];
    //         snprintf(text, sizeof(text), "%d", movement);
    //         lv_label_set_text(ui_Body_movement, text);
    //         lv_obj_clear_flag(ui_Body_movement, LV_OBJ_FLAG_HIDDEN);
    //     }
    // }
    lvgl_port_unlock();
}

void my_ui_radar_update_respiratory(uint8_t respiratory) {
    // TODO: 新UI中暂无雷达数据显示组件 ui_Respiratory
    // 需要在新UI中添加相应的显示组件
    lvgl_port_lock(0);
    // if (ui_Respiratory != NULL) {
    //     char text[16];
    //     snprintf(text, sizeof(text), "%d", respiratory);
    //     lv_label_set_text(ui_Respiratory, text);
    // }
    lvgl_port_unlock();
}

void my_ui_radar_update_heart_rate(uint8_t heart_rate) {
    // TODO: 新UI中暂无雷达数据显示组件 ui_Heart_Rate
    // 需要在新UI中添加相应的显示组件
    lvgl_port_lock(0);
    // if (ui_Heart_Rate != NULL) {
    //     char text[16];
    //     snprintf(text, sizeof(text), "%d", heart_rate);
    //     lv_label_set_text(ui_Heart_Rate, text);
    // }
    lvgl_port_unlock();
}

// 主页面图片显示控制
void my_ui_main_image_set_visibility(bool visible) {
    lvgl_port_lock(0);
    
    if (ui_MianNoPersonImage != NULL) {
        if (visible) {
            // 显示图片
            lv_obj_clear_flag(ui_MianNoPersonImage, LV_OBJ_FLAG_HIDDEN);
        } else {
            // 隐藏图片
            lv_obj_add_flag(ui_MianNoPersonImage, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    lvgl_port_unlock();
}

void my_ui_in_body_data() {
    // TODO: 新UI中暂无身体数据页面 ui_Screen1
    // 需要创建或选择合适的屏幕显示身体数据
    lvgl_port_lock(0);
    // lv_disp_load_scr(ui_Screen1);
    current_page = PAGE_BODY_DATA;
    lvgl_port_unlock();
}

// 当前唤醒模式
static wake_mode_t current_wake_mode = WAKE_MODE_SMART;
static wake_mode_t saved_wake_mode = WAKE_MODE_SMART;  // 已保存的模式

void my_ui_in_wake_mode() {
    lvgl_port_lock(0);
    // lv_disp_load_scr(ui_WakeMode);
    current_page = PAGE_WAKE_MODE;
    lvgl_port_unlock();
    
    // 强制刷新当前模式显示（确保UI正确更新）
}

// 设置唤醒模式并更新UI显示
void my_ui_wake_mode_set(wake_mode_t mode) {
    if (mode >= WAKE_MODE_MAX) {
        mode = WAKE_MODE_SMART;
    }
    
    ESP_LOGI("UI", "设置唤醒模式: %s", mode == WAKE_MODE_SMART ? "Smart" : "Classic");
    current_wake_mode = mode;
    
    lvgl_port_lock(0);
    
    lvgl_port_unlock();
}

// 获取当前唤醒模式（返回已保存的模式）
wake_mode_t my_ui_wake_mode_get(void) {
    return saved_wake_mode;
}

// 保存当前浏览的模式为设置的模式
void my_ui_wake_mode_save_current(void) {
    saved_wake_mode = current_wake_mode;
    ESP_LOGI("UI", "保存唤醒模式: %s", saved_wake_mode == WAKE_MODE_SMART ? "Smart" : "Classic");
}

// 切换到下一个模式（只改变当前浏览的模式）
void my_ui_wake_mode_next(void) {
    wake_mode_t next_mode = (wake_mode_t)((current_wake_mode + 1) % WAKE_MODE_MAX);
    my_ui_wake_mode_set(next_mode);
}

// 切换到上一个模式（只改变当前浏览的模式）
void my_ui_wake_mode_prev(void) {
    wake_mode_t prev_mode = (wake_mode_t)((current_wake_mode + WAKE_MODE_MAX - 1) % WAKE_MODE_MAX);
    my_ui_wake_mode_set(prev_mode);
}

// 文本颜色渐变动画回调函数 - 使用百分比值进行插值
static void text_color_anim_cb(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    
    // 计算透明度百分比 (0-100)
    uint8_t percent = (uint8_t)v;
    
    // 计算灰色值 (0x808080) 到白色 (0xFFFFFF) 的渐变
    // 灰色分量: R=128, G=128, B=128
    // 白色分量: R=255, G=255, B=255
    uint8_t r = 128 + (127 * percent / 100);
    uint8_t g = 128 + (127 * percent / 100);
    uint8_t b = 128 + (127 * percent / 100);
    
    // 创建颜色并设置
    lv_color_t color = lv_color_make(r, g, b);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// 将文本渐变为白色（选中状态）
static void animate_to_white(lv_obj_t* label) {
    if (label == NULL) {
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_values(&anim, 0, 100);  // 从0%白色渐变到100%白色
    lv_anim_set_time(&anim, 300);  // 动画持续300ms
    lv_anim_set_exec_cb(&anim, text_color_anim_cb);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);  // 缓入缓出效果
    lv_anim_start(&anim);
}

// 将文本渐变为灰色（未选中状态）
static void animate_to_gray(lv_obj_t* label) {
    if (label == NULL) {
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_values(&anim, 100, 0);  // 从100%白色渐变到0%白色（灰色）
    lv_anim_set_time(&anim, 300);  // 动画持续300ms
    lv_anim_set_exec_cb(&anim, text_color_anim_cb);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);  // 缓入缓出效果
    lv_anim_start(&anim);
}

// 初始化WakeModeTest页面的文本颜色
void initWakeModeTestTextColors(void) {
    // 默认选中Classic项（index 0）
    lv_obj_set_style_text_color(ui_WakeModeTestLabel1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_WakeModeTestLabel2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_WakeModeTestLabel3, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_WakeModeTestLabel4, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
}

// LVGL定时器回调：延迟重置动画标志
static void wake_mode_anim_reset_timer_cb(lv_timer_t * timer) {
    (void)timer;
    // 重置动画标志
    isWakeModeTestAnimating = false;
    // 删除定时器
    lv_timer_del(timer);
}

void wakeModeTestUP(void) {
    // 检查当前是否可以向上移动（不是第一项且没有动画正在进行）
    if (wakeModeTestIndex <= 0 || isWakeModeTestAnimating) {
        return;
    }
    
    // 先更新索引（在锁外，避免长时间持有锁）
    wakeModeTestIndex--;
    
    // 设置动画进行标志
    isWakeModeTestAnimating = true;
    
    // 使用lvgl_port_lock保护所有LVGL操作，但尽量缩短锁的持有时间
    if (!lvgl_port_lock(0)) {
        // 如果获取锁失败，恢复索引并重置标志
        wakeModeTestIndex++;
        isWakeModeTestAnimating = false;
        return;
    }
    
    // 在WakeModeTest页面，左旋操作向上移动80像素点
    memuDown_Animation(ui_WakeModeTestContainer2, 0);
    memuDown_Animation(ui_WakeModeTestContainer3, 0);
    
    // 更新文本颜色：使用渐变动画实现逐渐选中的感觉
    if (wakeModeTestIndex == 0) {
        // 选中Classic项：Classic文本渐变为白色，Smart文本渐变为灰色
        animate_to_white(ui_WakeModeTestLabel1);
        animate_to_white(ui_WakeModeTestLabel2);
        animate_to_gray(ui_WakeModeTestLabel3);
        animate_to_gray(ui_WakeModeTestLabel4);
    }
    
    lvgl_port_unlock();
    
    // 使用LVGL定时器延迟重置动画标志（300ms后，与动画时长一致）
    // LVGL定时器在LVGL线程中执行，不需要额外锁保护
    lv_timer_t * timer = lv_timer_create(wake_mode_anim_reset_timer_cb, 300, NULL);
    if (timer == NULL) {
        // 如果创建定时器失败，直接重置标志
        isWakeModeTestAnimating = false;
    }
}

void wakeModeTestDown(void) {
    // 检查当前是否可以向下移动（不是最后一项且没有动画正在进行）
    if (wakeModeTestIndex >= 1 || isWakeModeTestAnimating) { // 0和1两个索引值
        return;
    }
    
    // 先更新索引（在锁外，避免长时间持有锁）
    wakeModeTestIndex++;
    
    // 设置动画进行标志
    isWakeModeTestAnimating = true;
    
    // 使用lvgl_port_lock保护所有LVGL操作，但尽量缩短锁的持有时间
    if (!lvgl_port_lock(0)) {
        // 如果获取锁失败，恢复索引并重置标志
        wakeModeTestIndex--;
        isWakeModeTestAnimating = false;
        return;
    }
    
    // 在WakeModeTest页面，右旋操作向下移动80像素点
    memuUp_Animation(ui_WakeModeTestContainer2, 0);
    memuUp_Animation(ui_WakeModeTestContainer3, 0);
    
    // 更新文本颜色：使用渐变动画实现逐渐选中的感觉
    if (wakeModeTestIndex == 1) {
        // 选中Smart项：Classic文本渐变为灰色，Smart文本渐变为白色
        animate_to_gray(ui_WakeModeTestLabel1);
        animate_to_gray(ui_WakeModeTestLabel2);
        animate_to_white(ui_WakeModeTestLabel3);
        animate_to_white(ui_WakeModeTestLabel4);
    }
    
    lvgl_port_unlock();
    
    // 使用LVGL定时器延迟重置动画标志（300ms后，与动画时长一致）
    // LVGL定时器在LVGL线程中执行，不需要额外锁保护
    lv_timer_t * timer = lv_timer_create(wake_mode_anim_reset_timer_cb, 300, NULL);
    if (timer == NULL) {
        // 如果创建定时器失败，直接重置标志
        isWakeModeTestAnimating = false;
    }
}


// 当前亮度档位
static uint8_t current_light_duty = 100;  // 默认100%


// 亮度图片映射表（按顺序：20%, 30%, 40%, 50%, 60%, 70%, 80%, 90%, 100%）
static const lv_img_dsc_t* light_images[9] = {
    &ui_img_light20_png,   // 20%
    &ui_img_light30_png,   // 30%
    &ui_img_light40_png,   // 40%
    &ui_img_light50_png,   // 50%
    &ui_img_light60_png,   // 60%
    &ui_img_light70_png,   // 70%
    &ui_img_light80_png,   // 80%
    &ui_img_light90_png,   // 90%
    &ui_img_light100_png   // 100%
};

void my_ui_in_light() {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Light);
    current_page = PAGE_LIGHT;
    lvgl_port_unlock();
    
    // 更新显示为当前亮度
    my_ui_light_set(current_light_duty);
}

// 设置亮度并更新UI显示
void my_ui_light_set(uint8_t duty) {
    // 限制亮度范围：20%-100%，步进10%
    if (duty < 20) duty = 20;
    if (duty > 100) duty = 100;
    if ((duty - 20) % 10 != 0) {
        // 如果不是10的倍数，调整到最近的10的倍数
        duty = ((duty - 20) / 10) * 10 + 20;
    }
    
    current_light_duty = duty;
    
    lvgl_port_lock(0);
    
    // 更新亮度文字显示
    static char light_str[24] = {0};
    snprintf(light_str, sizeof(light_str), "Brightness: %d%%", duty);
    lv_label_set_text(ui_LightLabel, light_str);
    
    // 更新亮度图片
    // 计算图片索引：(duty - 20) / 10
    uint8_t image_index = (duty - 20) / 10;
    if (image_index < 9) {
        lv_image_set_src(ui_LightImage, light_images[image_index]);
    }
    bsp_lcd_bl_set(duty);
    
    lvgl_port_unlock();
}

// 获取当前亮度
uint8_t my_ui_light_get(void) {
    return current_light_duty;
}

// 切换到下一个亮度档位
void my_ui_light_next(void) {
    if (current_light_duty < 100) {
        my_ui_light_set(current_light_duty + 10);
    } else {
        my_ui_light_set(100);  
    }
}

// 切换到上一个亮度档位
void my_ui_light_prev(void) {
    if (current_light_duty > 20) {
        my_ui_light_set(current_light_duty - 10);
    } else {
        my_ui_light_set(20);  
    }
}

// 页面状态管理函数
int my_ui_get_current_page(void) {
    return current_page;
}

// 页面跳转函数
void navigate_to_page(page_t page) {
    switch (page) {
        case PAGE_START:
            my_ui_in_start();
            break;
        case PAGE_NETWORK_GUIDE:
            my_ui_network_guide();
            break;
        case PAGE_NETWORK_CONNECTING:
            my_ui_network_connecting();
            break;
        case PAGE_NETWORK_CONN_SUC:
            my_ui_network_conn_suc();
            break;
        case PAGE_NETWORK_CONN_FAIL:
            my_ui_network_conn_fail();
            break;
        case PAGE_NETWORK_OFFLINE:
            my_ui_network_offline();
            break;
        case PAGE_CLOCK:
            my_ui_in_clock();
            break;
        case PAGE_FUNCTION:
            my_ui_in_funtion();
            break;
        case PAGE_ALARM:
            my_ui_in_alarm();
            break;
        case PAGE_VOLUME:
            my_ui_in_volume();
            break;
        case PAGE_TIME_SET:
            my_ui_in_time_set();
            break;
        case PAGE_UNWIND:
            my_ui_in_unwind();
            break;
        case PAGE_UNWIND_ON:
            my_ui_in_unwind_on();
            break;
        case PAGE_UNWIND_SELECT:
            my_ui_in_unwind_select();
            break;
        case PAGE_NOISE_TIME:
            my_ui_in_noise_time();
            break;
        case PAGE_GOOD_MORNING:
            my_ui_in_good_morning();
            break;
        case PAGE_SLEEP_DATA:
            my_ui_in_sleep_data();
            break;
        case PAGE_DETECTION:
            my_ui_in_detection();
            break;
        case PAGE_DETECTION_N:
            my_ui_in_detection_n();
            break;
        case PAGE_DETECTION_Y:
            my_ui_in_detection_y();
            break;
        case PAGE_DETECTION_N1:
            my_ui_in_detection_n1();
            break;
        case PAGE_MIAN_YES_PERSON:
            my_ui_in_mian_yes_person();
            break;
        case PAGE_OTA:
            my_ui_in_ota();
            break;
        case PAGE_BODY_DATA:
            my_ui_in_body_data();
            break;
        case PAGE_WAKE_MODE:
            my_ui_in_wake_mode();
            break;
        case PAGE_RADAR_DISPLAY:
            my_ui_in_radar_display();
            break;
        default:
            ESP_LOGW("NAVIGATE", "未知页面: %d", page);
            break;
    }
}

// ============================================================================
// 雷达显示页面实现
// ============================================================================

// 静态变量存储雷达显示UI对象
static lv_obj_t *ui_RadarDisplay = NULL;          // 雷达显示页面
static lv_obj_t *ui_RadarContainer = NULL;        // 雷达显示容器
static lv_obj_t *ui_RadarBall = NULL;             // 检测球
static lv_obj_t *ui_RadarHeartRateLabel = NULL;   // 心率标签
static lv_obj_t *ui_RadarBreathLabel = NULL;      // 呼吸频率标签

// 辅助函数：角度转X坐标偏移
static int angle_to_x_offset(float angle) {
    // -60° ~ 60° 映射到 -195 ~ 195 像素
    if (angle < -60.0f) angle = -60.0f;
    if (angle > 60.0f) angle = 60.0f;
    return (int)((angle / 60.0f) * 260.0f) + 20;
}

// 辅助函数：距离转球大小
static int distance_to_size(int distance_cm) {
    // 0 ~ 150cm 映射到 60 ~ 30 像素（近大远小）
    if (distance_cm < 0) distance_cm = 0;
    if (distance_cm > 150) distance_cm = 150;
    return 60 - (int)((distance_cm / 150.0f) * 30.0f);
}

void my_ui_in_radar_display(void) {
    lvgl_port_lock(0);
    
    // 创建雷达显示界面（如果不存在）
    if (ui_RadarDisplay == NULL) {
        // 创建主屏幕
        ui_RadarDisplay = lv_obj_create(NULL);
        lv_obj_remove_flag(ui_RadarDisplay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(ui_RadarDisplay, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_RadarDisplay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 创建圆形容器（360x360，居中显示）
        ui_RadarContainer = lv_obj_create(ui_RadarDisplay);
        lv_obj_set_size(ui_RadarContainer, 360, 360);
        lv_obj_center(ui_RadarContainer);
        lv_obj_set_style_bg_color(ui_RadarContainer, lv_color_hex(0x1a1a1a), 0);  // 深灰色背景
        lv_obj_set_style_bg_opa(ui_RadarContainer, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ui_RadarContainer, 2, 0);
        lv_obj_set_style_border_color(ui_RadarContainer, lv_color_hex(0x404040), 0);  // 边框
        lv_obj_set_style_radius(ui_RadarContainer, LV_RADIUS_CIRCLE, 0);  // 圆形
        lv_obj_set_style_pad_all(ui_RadarContainer, 0, 0);
        lv_obj_remove_flag(ui_RadarContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        // 创建心率标签（上方）
        ui_RadarHeartRateLabel = lv_label_create(ui_RadarContainer);
        lv_label_set_text(ui_RadarHeartRateLabel, "Heart Rate: -- bpm");
        lv_obj_set_style_text_color(ui_RadarHeartRateLabel, lv_color_white(), 0);
        lv_obj_set_style_text_font(ui_RadarHeartRateLabel, &lv_font_montserrat_18, 0);
        lv_obj_align(ui_RadarHeartRateLabel, LV_ALIGN_TOP_MID, 0, 50);
        
        // 创建呼吸频率标签（下方）
        ui_RadarBreathLabel = lv_label_create(ui_RadarContainer);
        lv_label_set_text(ui_RadarBreathLabel, "Breath: -- /min");
        lv_obj_set_style_text_color(ui_RadarBreathLabel, lv_color_white(), 0);
        lv_obj_set_style_text_font(ui_RadarBreathLabel, &lv_font_montserrat_18, 0);
        lv_obj_align(ui_RadarBreathLabel, LV_ALIGN_BOTTOM_MID, 0, -50);
        
        // 创建检测球（白色圆形，初始在中心）
        ui_RadarBall = lv_obj_create(ui_RadarContainer);
        lv_obj_set_size(ui_RadarBall, 30, 30);  // 初始大小30x30
        lv_obj_set_pos(ui_RadarBall, 180 - 15, 180 - 15);  // 居中（减去半径）
        lv_obj_set_style_bg_color(ui_RadarBall, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(ui_RadarBall, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ui_RadarBall, 0, 0);
        lv_obj_set_style_radius(ui_RadarBall, LV_RADIUS_CIRCLE, 0);  // 圆形
        lv_obj_set_style_shadow_width(ui_RadarBall, 15, 0);  // 添加光晕效果
        lv_obj_set_style_shadow_color(ui_RadarBall, lv_color_white(), 0);
        lv_obj_set_style_shadow_opa(ui_RadarBall, LV_OPA_50, 0);
        lv_obj_remove_flag(ui_RadarBall, LV_OBJ_FLAG_SCROLLABLE);
        
        ESP_LOGI(TAG, "雷达显示页面创建完成");
    }
    
    // 加载页面
    lv_disp_load_scr(ui_RadarDisplay);
    current_page = PAGE_RADAR_DISPLAY;
    
    ESP_LOGI(TAG, "进入雷达显示页面");
    
    lvgl_port_unlock();
}

void my_ui_radar_display_update(float angle, int distance_cm, uint8_t heart_rate, uint8_t breath_rate) {
    if (ui_RadarDisplay == NULL || ui_RadarBall == NULL) {
        ESP_LOGW(TAG, "雷达显示页面未创建");
        return;
    }
    
    lvgl_port_lock(0);
    
    // 计算球的位置
    int x_offset = angle_to_x_offset(angle);
    int x_pos = 180 + x_offset;  // 屏幕中心180 + 偏移
    int y_pos = 180;  // Y坐标固定在中心
    
    // 计算球的大小
    int size = distance_to_size(distance_cm);
    
    // 更新球的位置（减去半径使其居中）
    lv_obj_set_pos(ui_RadarBall, x_pos - size/2, y_pos - size/2);
    
    // 更新球的大小
    lv_obj_set_size(ui_RadarBall, size, size);
    
    // 更新心率显示
    if (heart_rate > 0) {
        char hr_text[32];
        snprintf(hr_text, sizeof(hr_text), "Heart Rate: %d bpm", heart_rate);
        lv_label_set_text(ui_RadarHeartRateLabel, hr_text);
    } else {
        lv_label_set_text(ui_RadarHeartRateLabel, "Heart Rate: -- bpm");
    }
    
    // 更新呼吸频率显示
    if (breath_rate > 0) {
        char br_text[32];
        snprintf(br_text, sizeof(br_text), "Breath: %d /min", breath_rate);
        lv_label_set_text(ui_RadarBreathLabel, br_text);
    } else {
        lv_label_set_text(ui_RadarBreathLabel, "Breath: -- /min");
    }
    
    ESP_LOGI(TAG, "雷达显示更新: 角度=%.1f° 距离=%dcm 心率=%d 呼吸=%d 球位置=(%d,%d) 球大小=%d", 
             angle, distance_cm, heart_rate, breath_rate, x_pos, y_pos, size);
    
    lvgl_port_unlock();
}
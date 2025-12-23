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
#include "fsm_main.h"  // 包含状态定义
#include "my_lidar.h"  // 包含雷达数据结构

// 前向声明，避免循环依赖
void fsm_main_set_radar_detect_enabled(bool enabled);

#define TAG "UI_BEHAVIOR"

// 页面状态管理
static int current_page = PAGE_START;  // 当前页面

static bool have_alarm = false;
static bool is_online = false;

// Unwind动物选择相关
static unwind_animal_t current_unwind_animal = UNWIND_ANIMAL_CAT;  // 默认选择cat
static TimerHandle_t unwind_label_timer = NULL;  // 文字隐藏定时器
static bool unwind_animation_started = false;    // 动画开始标志位
// WakeModeTest相关变量
static int wakeModeTestIndex = 0;
static bool isWakeModeTestAnimating = false; // 跟踪动画是否正在进行

// Unwind图片和标签信息结构体
typedef struct {
    const lv_image_dsc_t* image;  // 照片
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
static void unwind_label_show(void);
static void unwind_label_hide(void);
static void unwind_label_timer_callback(TimerHandle_t xTimer);

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
    
    // 从NVS读取保存的闹钟时间并更新主页面显示（无论是否启用，都显示保存的时间）
    const struct device_config *cfg = my_nvs_get_config();
    if (cfg != NULL && ui_MainMorningAlarm != NULL) {
        uint8_t alarm_hour = cfg->alarm_hour;
        uint8_t alarm_minute = cfg->alarm_minute;
        
        // 显示保存的闹钟时间（即使闹钟被禁用，也显示上次设置的时间）
        static char alarm_time_str[8] = {0};
        snprintf(alarm_time_str, sizeof(alarm_time_str), "%02d:%02d", alarm_hour, alarm_minute);
        lv_label_set_text(ui_MainMorningAlarm, alarm_time_str);
    }
    
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

    lvgl_port_unlock();
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
    uint32_t cur = lv_roller_get_selected(ui_MueuRoller);
    if (cur == 0) {
        cur = 0;
    } else {
        cur -= 1;
    }
    lv_roller_set_selected(ui_MueuRoller, cur, LV_ANIM_ON);
    lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, NULL);
    lvgl_port_unlock();
}

void my_ui_function_menu_down() {
    if (!lvgl_port_lock(0)) {
        return;  // 如果获取锁失败，直接返回
    }
    uint32_t max = lv_roller_get_option_count(ui_MueuRoller);
    uint32_t cur = lv_roller_get_selected(ui_MueuRoller);
    if (cur >= max - 1) {
        cur = max - 1;
    } else {
        cur += 1;
    }
    lv_roller_set_selected(ui_MueuRoller, cur, LV_ANIM_ON);
    lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, NULL);
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
    
    // 重置动画标志位
    unwind_animation_started = false;
    
    // 先强制显示文字（确保从菜单进入时文字是显示的）
    lv_obj_clear_flag(ui_UnwindSeletLabel, LV_OBJ_FLAG_HIDDEN);
    
    // 显示当前选中的动物图片和标签
    update_unwind_display();
    
    lvgl_port_unlock();
    
    // 显示文字并启动3秒定时器（从菜单进入时也会启动）
    unwind_label_show();
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

// 显示文字并启动3秒定时器
static void unwind_label_show(void) {
    if (!lvgl_port_lock(0)) {
        ESP_LOGW(TAG, "unwind_label_show: failed to lock lvgl");
        return;
    }
    
    // 检查当前屏幕是否是 UnwindSelet
    lv_obj_t* current_screen = lv_disp_get_scr_act(NULL);
    if (current_screen != ui_UnwindSelet) {
        lvgl_port_unlock();
        return;
    }
    
    // 显示文字（确保文字是显示的）
    lv_obj_clear_flag(ui_UnwindSeletLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(ui_UnwindSeletLabel);
    
    lvgl_port_unlock();
    
    // 停止之前的定时器（如果存在）
    if (unwind_label_timer != NULL) {
        xTimerStop(unwind_label_timer, 0);
        // 重置定时器为3秒
        xTimerChangePeriod(unwind_label_timer, pdMS_TO_TICKS(3000), 0);
    } else {
        // 创建定时器（一次性，3秒）
        unwind_label_timer = xTimerCreate("unwind_label_timer", 
                                         pdMS_TO_TICKS(3000), 
                                         pdFALSE, 
                                         NULL, 
                                         unwind_label_timer_callback);
        if (unwind_label_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create unwind label timer");
            return;
        }
    }
    
    // 启动定时器
    if (xTimerStart(unwind_label_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start unwind label timer");
    } else {
        ESP_LOGI(TAG, "unwind_label_show: label shown, timer started");
    }
}

// 隐藏文字并设置动画开始标志位
static void unwind_label_hide(void) {
    if (!lvgl_port_lock(0)) {
        ESP_LOGW(TAG, "unwind_label_hide: failed to lock lvgl");
        return;
    }
    
    // 检查当前屏幕是否是 UnwindSelet
    lv_obj_t* current_screen = lv_disp_get_scr_act(NULL);
    if (current_screen != ui_UnwindSelet) {
        lvgl_port_unlock();
        return;
    }
    
    // 隐藏文字
    lv_obj_add_flag(ui_UnwindSeletLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(ui_UnwindSeletLabel);
    
    lvgl_port_unlock();
    
    // 设置动画开始标志位
    unwind_animation_started = true;
    ESP_LOGI(TAG, "unwind_label_hide: label hidden, animation flag set");
    
    // 如果是猫猫，触发雷达
    if (current_unwind_animal == UNWIND_ANIMAL_CAT) {
        ESP_LOGI(TAG, "unwind_label_hide: cat selected, enabling radar");
        fsm_main_set_radar_detect_enabled(true);
    }else if (current_unwind_animal != UNWIND_ANIMAL_CAT){
        fsm_main_set_radar_detect_enabled(false);
    }
}

// 定时器回调函数
static void unwind_label_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "unwind_label_timer_callback: timer expired, hiding label");
    unwind_label_hide();
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
    
    // 重置动画标志位
    unwind_animation_started = false;
    ESP_LOGI(TAG, "my_ui_unwind_set_animal: animation flag reset to false");
    
    // 更新显示
    update_unwind_display();
    
    // 显示文字并启动3秒定时器（切换动物时重新显示）
    unwind_label_show();
    
    // 播放选择动画
    update_unwind_choose_animation();
    
}

// 只更新动物变量，不更新UI（用于直接切换动画）
void my_ui_unwind_set_animal_no_ui(unwind_animal_t animal) {
    if (animal >= UNWIND_ANIMAL_MAX) {
        animal = UNWIND_ANIMAL_CAT;
    }
    current_unwind_animal = animal;
    
    // 重置动画标志位
    unwind_animation_started = false;
    ESP_LOGI(TAG, "my_ui_unwind_set_animal_no_ui: animal set to %d, no UI update", animal);
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

// 获取动画开始标志位
bool my_ui_unwind_is_animation_started(void) {
    return unwind_animation_started;
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
    // 读取当前的wakeModeTestIndex值，根据它来设置显示状态
    if (wakeModeTestIndex == 0) {
        // 选中Container2（index 0）
        lv_obj_set_style_text_color(ui_WakeModeTestLabel1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_WakeModeTestLabel2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_WakeModeTestLabel3, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_WakeModeTestLabel4, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 显示Container2的解释内容，隐藏Container3的解释内容
        lv_obj_clear_flag(ui_WakeModeTestLabel2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WakeModeTestLabel4, LV_OBJ_FLAG_HIDDEN);
    } else {
        // 选中Container3（index 1）
        lv_obj_set_style_text_color(ui_WakeModeTestLabel1, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_WakeModeTestLabel2, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_WakeModeTestLabel3, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_WakeModeTestLabel4, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 隐藏Container2的解释内容，显示Container3的解释内容
        lv_obj_add_flag(ui_WakeModeTestLabel2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WakeModeTestLabel4, LV_OBJ_FLAG_HIDDEN);
    }
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
        // 选中Container2：Container2文本渐变为白色，Container3文本渐变为灰色
        animate_to_white(ui_WakeModeTestLabel1);
        animate_to_white(ui_WakeModeTestLabel2);
        animate_to_gray(ui_WakeModeTestLabel3);
        animate_to_gray(ui_WakeModeTestLabel4);
        
        // 显示Container2的解释内容，隐藏Container3的解释内容
        lv_obj_clear_flag(ui_WakeModeTestLabel2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_WakeModeTestLabel4, LV_OBJ_FLAG_HIDDEN);
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
        // 选中Container3：Container2文本渐变为灰色，Container3文本渐变为白色
        animate_to_gray(ui_WakeModeTestLabel1);
        animate_to_gray(ui_WakeModeTestLabel2);
        animate_to_white(ui_WakeModeTestLabel3);
        animate_to_white(ui_WakeModeTestLabel4);
        
        // 隐藏Container2的解释内容，显示Container3的解释内容
        lv_obj_add_flag(ui_WakeModeTestLabel2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_WakeModeTestLabel4, LV_OBJ_FLAG_HIDDEN);
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


// 亮度图片映射表（按顺序：10%, 20%, 30%, 40%, 50%, 60%, 70%, 80%, 90%, 100%）
static const lv_image_dsc_t* light_images[10] = {
    &ui_img_light10_png,   // 10%
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
    // 限制亮度范围：10%-100%，步进10%
    if (duty < 10) duty = 10;
    if (duty > 100) duty = 100;
    if ((duty - 10) % 10 != 0) {
        // 如果不是10的倍数，调整到最近的10的倍数
        duty = ((duty - 10) / 10) * 10 + 10;
    }
    
    current_light_duty = duty;
    
    lvgl_port_lock(0);
    
    // 更新亮度文字显示
    static char light_str[24] = {0};
    snprintf(light_str, sizeof(light_str), "Brightness: %d%%", duty);
    lv_label_set_text(ui_LightLabel, light_str);
    
    // 更新亮度图片
    // 计算图片索引：(duty - 10) / 10
    uint8_t image_index = (duty - 10) / 10;
    if (image_index < 10) {
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
    if (current_light_duty > 10) {
        my_ui_light_set(current_light_duty - 10);
    } else {
        my_ui_light_set(10);  
    }
}

// 静态变量：保存夜间模式圆圈对象
static lv_obj_t *s_night_mode_circle = NULL;  // 背景圆圈（白色边框）
static TaskHandle_t s_night_mode_task_handle = NULL;
static uint32_t s_night_mode_start_time = 0;  // 夜间模式开始时间（毫秒）
static bool s_night_mode_movement_valid = false;  // 体动数据是否有效

// 时间段结构：记录体动值变化的时间点
typedef struct {
    uint32_t start_time_ms;  // 相对于s_night_mode_start_time的开始时间（毫秒）
    uint32_t end_time_ms;    // 结束时间（0表示还在进行中）
    bool is_low_movement;    // true=体动<30（棕色），false=体动>=30（白色）
} movement_segment_t;

#define MAX_SEGMENTS 200  // 最多200个时间段（足够3分钟内的变化）
static movement_segment_t s_movement_segments[MAX_SEGMENTS];
static uint16_t s_segment_count = 0;  // 当前时间段数量
static lv_obj_t *s_segment_arcs[MAX_SEGMENTS];  // 每个时间段对应的arc对象

// 将睡眠模式页面恢复到初始样式（黄色月亮、Good Night）
void my_ui_sleep_mode_reset_to_default(void) {
    lvgl_port_lock(0);
    
    // 恢复月亮图片：从棕色月亮改回黄色月亮
    if (ui_sleepModeMoon != NULL) {
        lv_image_set_src(ui_sleepModeMoon, &ui_img_yello_moon_png);
    }
    
    // 恢复所有文字颜色：从 0xA86F2A 改回 0xFFBB5C
    lv_color_t default_color = lv_color_hex(0xFFBB5C);
    
    if (ui_sleepModeHour != NULL) {
        lv_obj_set_style_text_color(ui_sleepModeHour, default_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    if (ui_sleepModeMinute != NULL) {
        lv_obj_set_style_text_color(ui_sleepModeMinute, default_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    if (ui_sleepModeTips != NULL) {
        lv_label_set_text(ui_sleepModeTips, "Time for Bed");
        lv_obj_set_style_text_color(ui_sleepModeTips, default_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    if (ui_sleepModeLabel != NULL) {
        lv_obj_set_style_text_color(ui_sleepModeLabel, default_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // 屏蔽掉白圈（夜间模式的圆圈）
    if (s_night_mode_circle != NULL) {
        lv_obj_add_flag(s_night_mode_circle, LV_OBJ_FLAG_HIDDEN);
    }
    
    lvgl_port_unlock();
}

// 将睡眠模式页面切换到播放完成后的样式（显示白圈、Good Night）
void my_ui_sleep_mode_ready(void) {
    lvgl_port_lock(0);
    
    // 创建白圈（如果还没有创建）
    if (s_night_mode_circle == NULL && ui_sleepMode != NULL) {
        s_night_mode_circle = lv_arc_create(ui_sleepMode);
        lv_obj_set_width(s_night_mode_circle, 300);
        lv_obj_set_height(s_night_mode_circle, 300);
        lv_obj_set_align(s_night_mode_circle, LV_ALIGN_CENTER);
        
        // 设置背景圆圈：完整的360度，白色，细边框（2px）
        lv_arc_set_bg_angles(s_night_mode_circle, 0, 360);
        lv_obj_set_style_arc_color(s_night_mode_circle, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_night_mode_circle, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_width(s_night_mode_circle, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 设置背景为透明（镂空效果）
        lv_obj_set_style_bg_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 初始化指示器：从270度（最上方）开始，宽度2px，初始为0度
        lv_arc_set_angles(s_night_mode_circle, 270, 270);
        lv_obj_set_style_arc_width(s_night_mode_circle, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
        // 隐藏旋钮
        lv_obj_set_style_bg_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
        
        // 设置为不可点击
        lv_obj_clear_flag(s_night_mode_circle, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // 显示白圈
    if (s_night_mode_circle != NULL) {
        lv_obj_clear_flag(s_night_mode_circle, LV_OBJ_FLAG_HIDDEN);
        // 重置进度条
        lv_arc_set_angles(s_night_mode_circle, 270, 270);
        lv_obj_set_style_arc_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    
    // 文字改回 Good Night
    if (ui_sleepModeTips != NULL) {
        lv_label_set_text(ui_sleepModeTips, "Good Night");
    }
    
    lvgl_port_unlock();
}

// 将睡眠模式页面切换到夜间模式样式
void my_ui_sleep_mode_to_night_mode(void) {
    lvgl_port_lock(0);
    
    // 修改月亮图片：从黄色月亮改成棕色月亮
    if (ui_sleepModeMoon != NULL) {
        lv_image_set_src(ui_sleepModeMoon, &ui_img_brown_moon_png);
    }
    
    // 修改所有文字颜色：从 0xFFBB5C 改成 0xA86F2A
    lv_color_t night_color = lv_color_hex(0xA86F2A);
    
    if (ui_sleepModeHour != NULL) {
        lv_obj_set_style_text_color(ui_sleepModeHour, night_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    if (ui_sleepModeMinute != NULL) {
        lv_obj_set_style_text_color(ui_sleepModeMinute, night_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    if (ui_sleepModeTips != NULL) {
        lv_label_set_text(ui_sleepModeTips, "Night mode");
        lv_obj_set_style_text_color(ui_sleepModeTips, night_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    if (ui_sleepModeLabel != NULL) {
        lv_obj_set_style_text_color(ui_sleepModeLabel, night_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // 创建镂空圆圈（如果还没有创建）
    if (s_night_mode_circle == NULL && ui_sleepMode != NULL) {
        s_night_mode_circle = lv_arc_create(ui_sleepMode);
        lv_obj_set_width(s_night_mode_circle, 300);
        lv_obj_set_height(s_night_mode_circle, 300);
        lv_obj_set_align(s_night_mode_circle, LV_ALIGN_CENTER);
        
        // 设置背景圆圈：完整的360度，白色，细边框（2px）
        lv_arc_set_bg_angles(s_night_mode_circle, 0, 360);
        lv_obj_set_style_arc_color(s_night_mode_circle, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_night_mode_circle, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_width(s_night_mode_circle, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 设置背景为透明（镂空效果）
        lv_obj_set_style_bg_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 初始化指示器：从270度（最上方）开始，宽度2px，初始为0度
        lv_arc_set_angles(s_night_mode_circle, 270, 270);
        lv_obj_set_style_arc_width(s_night_mode_circle, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
        // 隐藏旋钮
        lv_obj_set_style_bg_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
        
        // 设置为不可点击
        lv_obj_clear_flag(s_night_mode_circle, LV_OBJ_FLAG_CLICKABLE);
    } else if (s_night_mode_circle != NULL) {
        // 如果已存在，显示它并重置进度
        lv_obj_clear_flag(s_night_mode_circle, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_angles(s_night_mode_circle, 270, 270);
        lv_obj_set_style_arc_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    
    // 记录开始时间（重新进入时重置）
    s_night_mode_start_time = esp_log_timestamp();
    
    // 清空所有时间段和arc对象（重新进入时重置）
    s_segment_count = 0;
    for (int i = 0; i < MAX_SEGMENTS; i++) {
        if (s_segment_arcs[i] != NULL) {
            lv_obj_del(s_segment_arcs[i]);
            s_segment_arcs[i] = NULL;
        }
    }
    
    // 清空背景圆圈（重新进入时重置）
    if (s_night_mode_circle != NULL) {
        lv_arc_set_angles(s_night_mode_circle, 270, 270);
        lv_obj_set_style_arc_opa(s_night_mode_circle, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    
    lvgl_port_unlock();
}

/**
 * @brief 夜间模式进度条更新任务
 * 轮询体动数据，分段绘制进度条：体动<30用棕色，体动>=30用白色
 * 0-360度代表3分钟，从270度（最上方）开始往右
 */
static void night_mode_progress_task(void* param) {
    ESP_LOGI(TAG, "night_mode_progress_task started");
    
    const uint32_t TOTAL_TIME_MS = 180000;  // 3分钟 = 180秒 = 180000毫秒
    const uint8_t MOVEMENT_THRESHOLD = 30;  // 体动阈值
    static uint8_t last_movement = 255;  // 记录上一次体动值，用于检测状态变化
    static bool last_is_low = false;  // 上一次是否为低体动（<30）
    
    while (fsm_main_get_current_state() == F_MAIN_S_NIGHTMODE) {
        // 轮询获取体动数据
        my_lidar_handle_t lidar_handle = fsm_main_get_lidar_handle();
        uint8_t movement = 0;
        bool has_movement_data = false;
        
        if (lidar_handle != NULL) {
            radar_latest_data_t data;
            if (my_lidar_get_latest_data(lidar_handle, &data) == ESP_OK) {
                movement = data.movement_param;
                has_movement_data = true;
            }
        }
        
        uint32_t current_time = esp_log_timestamp();
        uint32_t elapsed_time = current_time - s_night_mode_start_time;
        
        if (has_movement_data) {
            bool current_is_low = (movement < MOVEMENT_THRESHOLD);
            
            // 检测体动值变化：如果体动状态改变，结束当前时间段，开始新时间段
            if (last_movement == 255 || current_is_low != last_is_low) {
                // 结束当前时间段（如果有）
                if (s_segment_count > 0 && s_movement_segments[s_segment_count - 1].end_time_ms == 0) {
                    s_movement_segments[s_segment_count - 1].end_time_ms = elapsed_time;
                }
                
                // 开始新时间段（如果还有空间）
                if (s_segment_count < MAX_SEGMENTS) {
                    s_movement_segments[s_segment_count].start_time_ms = elapsed_time;
                    s_movement_segments[s_segment_count].end_time_ms = 0;  // 0表示还在进行中
                    s_movement_segments[s_segment_count].is_low_movement = current_is_low;
                    s_segment_count++;
                }
                
                last_is_low = current_is_low;
            }
            
            last_movement = movement;
        }
        
        // 更新当前时间段（最后一个）的结束时间为当前时间（用于绘制）
        if (s_segment_count > 0 && s_movement_segments[s_segment_count - 1].end_time_ms == 0) {
            s_movement_segments[s_segment_count - 1].end_time_ms = elapsed_time;
        }
        
        // 重新绘制所有时间段
        if (s_night_mode_circle != NULL && ui_sleepMode != NULL) {
            lvgl_port_lock(0);
            
            // 删除所有旧的arc对象
            for (int i = 0; i < MAX_SEGMENTS; i++) {
                if (s_segment_arcs[i] != NULL) {
                    lv_obj_del(s_segment_arcs[i]);
                    s_segment_arcs[i] = NULL;
                }
            }
            
            // 为每个时间段创建arc对象
            for (uint16_t i = 0; i < s_segment_count; i++) {
                movement_segment_t *seg = &s_movement_segments[i];
                
                // 计算时间段的开始和结束角度
                float start_progress = (float)seg->start_time_ms / TOTAL_TIME_MS;
                float end_progress = (float)seg->end_time_ms / TOTAL_TIME_MS;
                if (start_progress > 1.0f) start_progress = 1.0f;
                if (end_progress > 1.0f) end_progress = 1.0f;
                
                int32_t start_angle = 270 + (int32_t)(start_progress * 360);
                int32_t end_angle = 270 + (int32_t)(end_progress * 360);
                
                // 创建arc对象
                lv_obj_t *arc = lv_arc_create(ui_sleepMode);
                lv_obj_set_width(arc, 300);
                lv_obj_set_height(arc, 300);
                lv_obj_set_align(arc, LV_ALIGN_CENTER);
                
                // 设置背景为透明
                lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
                
                // 设置指示器角度和颜色
                lv_arc_set_angles(arc, start_angle, end_angle);
                lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                
                // 根据体动值设置颜色
                if (seg->is_low_movement) {
                    lv_obj_set_style_arc_color(arc, lv_color_hex(0xA86F2A), LV_PART_INDICATOR | LV_STATE_DEFAULT);  // 棕色
                } else {
                    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);  // 白色
                }
                
                // 隐藏旋钮
                lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
                lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
                
                s_segment_arcs[i] = arc;
            }
            
            // 恢复当前时间段为进行中状态
            if (s_segment_count > 0) {
                s_movement_segments[s_segment_count - 1].end_time_ms = 0;
            }
            
            lvgl_port_unlock();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // 每100ms更新一次
    }
    
    // 退出夜间模式时，删除所有arc对象
    lvgl_port_lock(0);
    for (int i = 0; i < MAX_SEGMENTS; i++) {
        if (s_segment_arcs[i] != NULL) {
            lv_obj_del(s_segment_arcs[i]);
            s_segment_arcs[i] = NULL;
        }
    }
    lvgl_port_unlock();
    
    ESP_LOGI(TAG, "night_mode_progress_task finished");
    s_night_mode_task_handle = NULL;
    vTaskDelete(NULL);
}

// 启动夜间模式进度条更新任务
void my_ui_night_mode_start_progress(void) {
    // 如果任务已存在，先停止
    if (s_night_mode_task_handle != NULL) {
        vTaskDelete(s_night_mode_task_handle);
        s_night_mode_task_handle = NULL;
    }
    
    // 创建新任务
    xTaskCreate(night_mode_progress_task, "night_progress", 4096, NULL, 5, &s_night_mode_task_handle);
}

// 停止夜间模式进度条更新任务
void my_ui_night_mode_stop_progress(void) {
    if (s_night_mode_task_handle != NULL) {
        vTaskDelete(s_night_mode_task_handle);
        s_night_mode_task_handle = NULL;
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
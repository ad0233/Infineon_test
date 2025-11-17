#include "fsm_lib.h"
#include "fsm_main.h"

#include "esp_log.h"
#include "my_utils.h"
#include "my_nvs.h"
#include "my_h264.h"
#include "esp_lvgl_port.h"
#include "my_ui_behavior.h"

static const char *TAG = "fsm_main";


enum fsm_main_state_enum {
    F_MAIN_S_UNINIT,              // 未初始化
    F_MAIN_S_UNINIT_PLAYING,       //开机动画
    F_MAIN_S_CLOCK,              // 时钟页面 
    F_MAIN_S_CLOCK_AWAY,        // 时钟页面人走
    F_MAIN_S_CLOCK_BACK,        //时种页面人回来过度（大蝴蝶）
    // 找人
    F_MAIN_S_FINDPERSONC_ANIM,  // 找人动画
    F_MAIN_S_FINDPERSON,        // 雷达找人检测
    F_MAIN_S_FINDSUC,           // 找人成功
    F_MAIN_S_FINDFAIL,          // 找人失败
    F_MAIN_S_FINDSUC_ANIM,      // 找人成功动画
    F_MAIN_S_FINDFAIL_ANIM,     // 找人失败动画
    // Wifi连接
    F_MAIN_S_WIFI_DETECT,       //wifi检测
    F_MAIN_S_WIFI_GUIDE,        // 二维码指引连接wifi
    F_MAIN_S_WIFI_CONN,         // wifi连接中
    F_MAIN_S_WIFI_CONN_SUC,     // wifi连接成功
    F_MAIN_S_WIFI_CONN_FAIL,    //  wifi连接失败
    F_MAIN_S_WIFI_OFFLINE,      //wifi离线状态（wifi云）
    // RTC配置
    F_MAIN_S_RTC_DETECT,         // RTC配置检测
    F_MAIN_S_RTC_DETECT_CLK,     // RTC时间
    //菜单
    F_MAIN_S_MENU,               //菜单
    F_MAIN_S_MENU_SELECT,        //菜单选择

    F_MAIN_S_MENU_WIFI,        //设置wifi页面
    F_MAIN_S_MENU_SLEEP_MODE,   //睡眠模式页面
    F_MAIN_S_MENU_ALARM,        //闹钟页面
    F_MAIN_S_MENU_UNWIND,        //选择歌曲页面
    F_MAIN_S_MENU_VOLUME,        //声音页面
    F_MAIN_S_MENU_BRIGHTNESS,      //亮度页面
    F_MAIN_S_MENU_SET_TIME,        //设置时间页面




};

// 状态名称获取
static const char* fsm_state_to_str(uint8_t state) {
    static const char* names[] = {
        "UNINIT",                    // F_MAIN_S_UNINIT
        "UNINIT_PLAYING",            // F_MAIN_S_UNINIT_PLAYING
        "CLOCK",                     // F_MAIN_S_CLOCK
        "CLOCK_AWAY",                // F_MAIN_S_CLOCK_AWAY
        "CLOCK_BACK",                // F_MAIN_S_CLOCK_BACK
        "FINDPERSONC_ANIM",          // F_MAIN_S_FINDPERSONC_ANIM
        "FINDPERSON",                // F_MAIN_S_FINDPERSON
        "FINDSUC",                   // F_MAIN_S_FINDSUC
        "FINDFAIL",                  // F_MAIN_S_FINDFAIL
        "FINDSUC_ANIM",              // F_MAIN_S_FINDSUC_ANIM
        "FINDFAIL_ANIM",             // F_MAIN_S_FINDFAIL_ANIM
        "WIFI_DETECT",               // F_MAIN_S_WIFI_DETECT
        "WIFI_GUIDE",                // F_MAIN_S_WIFI_GUIDE
        "WIFI_CONN",                 // F_MAIN_S_WIFI_CONN
        "WIFI_CONN_SUC",             // F_MAIN_S_WIFI_CONN_SUC
        "WIFI_CONN_FAIL",            // F_MAIN_S_WIFI_CONN_FAIL
        "WIFI_OFFLINE",              // F_MAIN_S_WIFI_OFFLINE
        "RTC_DETECT",                // F_MAIN_S_RTC_DETECT
        "RTC_DETECT_CLK",            // F_MAIN_S_RTC_DETECT_CLK
        "MENU",                      // F_MAIN_S_MENU
        "MENU_SELECT",               // F_MAIN_S_MENU_SELECT
        "MENU_WIFI",                 // F_MAIN_S_MENU_WIFI
        "MENU_SLEEP_MODE",           // F_MAIN_S_MENU_SLEEP_MODE
        "MENU_ALARM",                // F_MAIN_S_MENU_ALARM
        "MENU_UNWIND",               // F_MAIN_S_MENU_UNWIND
        "MENU_VOLUME",               // F_MAIN_S_MENU_VOLUME
        "MENU_BRIGHTNESS",           // F_MAIN_S_MENU_BRIGHTNESS
        "MENU_SET_TIME",             // F_MAIN_S_MENU_SET_TIME
    };
    return (state < sizeof(names)/sizeof(names[0]) && names[state]) ? names[state] : "UNKNOWN";
}


// 事件名称获取
static const char* fsm_event_to_str(uint8_t event) {
    if (event == F_MAIN_E_TIMEOUT) return "TIMEOUT";
    
    static const char* names[] = {
        "INIT",                      // F_MAIN_E_INIT
        "ANIM_PLAY_SUC",             // F_MAIN_E_ANIM_PLAY_SUC
        "LIDAR_FIND",                // F_MAIN_E_LIDAR_FIND
        "LIDAR_UPDATE",              // F_MAIN_E_LIDAR_UPDATE
        "LIDAR_NOT_FOUND",           // F_MAIN_E_LIDAR_NOT_FOUND
        "DEV_MOVE",                  // F_MAIN_E_DEV_MOVE
        "WIFI_CMD_TRIG",             // F_MAIN_E_WIFI_CMD_TRIG
        "WIFI_C_CONN",               // F_MAIN_E_WIFI_C_CONN
        "WIFI_C_SUC",                // F_MAIN_E_WIFI_C_SUC
        "WIFI_C_FAIL",               // F_MAIN_E_WIFI_C_FAIL
        "WIFI_C_UNCFG",              // F_MAIN_E_WIFI_C_UNCFG
        "RTC_EXIST",                 // F_MAIN_E_RTC_EXIST
        "RTC_NOT_EXIST",             // F_MAIN_E_RTC_NOT_EXIST
        "BTN_CLICKED",               // F_MAIN_E_BTN_CLICKED
        "BTN_L_CLICKED",             // F_MAIN_E_BTN_L_CLICKED
        "KNOB_CW",                   // F_MAIN_E_KNOB_CW
        "TIME",                      // F_MAIN_E_TIME
    };
    return (event < sizeof(names)/sizeof(names[0]) && names[event]) ? names[event] : "UNKNOWN";
}


// static struct StateTable fsm_user_table[] = {
//     //区分事件组变量       编号         到来的事件               当前的状态            下一个状态         超时  立即执行  将要要执行的函数
//     { nullptr            ,0          ,F_MAIN_E_INIT          , F_MAIN_S_UNINIT         , F_MAIN_S_FINDPERSON     ,15   ,false   ,fsm_main_lidar_find },
//     { nullptr            ,0          ,F_MAIN_E_LIDAR_UPDATE  , F_MAIN_S_CLOCK          , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_lidar_clock_update },
//     // 找人 
//     { nullptr            ,0          ,F_MAIN_E_DEV_MOVE      , F_MAIN_S_FINDFAIL       , F_MAIN_S_FINDPERSON     ,15   ,false   ,fsm_main_lidar_find      },
//     { nullptr            ,0          ,F_MAIN_E_LIDAR_FIND    , F_MAIN_S_FINDPERSON     , F_MAIN_S_FINDSUC        ,5    ,false   ,fsm_main_lidar_find_suc  },
//     { nullptr            ,0          ,F_MAIN_E_TIMEOUT       , F_MAIN_S_FINDPERSON     , F_MAIN_S_FINDFAIL       ,0    ,false   ,fsm_main_lidar_find_fail },
//     // 网络相关
//     { fm_has_w_c_state   ,FM_W_N_CFG ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_guide },
//     { fm_has_w_c_state   ,FM_W_CONN  ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
//     { fm_has_w_c_state   ,FM_W_SUC   ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
//     { fm_has_w_c_state   ,FM_W_FAI   ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },

//     { fm_has_w_c_state   ,FM_W_N_CFG ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_guide },
//     { fm_has_w_c_state   ,FM_W_CONN  ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
//     { fm_has_w_c_state   ,FM_W_SUC   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
//     { fm_has_w_c_state   ,FM_W_FAI   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },

//     { fm_has_w_c_state   ,FM_W_N_CFG ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_guide },
//     { fm_has_w_c_state   ,FM_W_CONN  ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
//     { fm_has_w_c_state   ,FM_W_SUC   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
//     { fm_has_w_c_state   ,FM_W_FAI   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },
//     //区分事件组变量       编号         到来的事件               当前的状态            下一个状态         超时  立即执行  将要要执行的函数
//     // 指引连接wifi
//     // wifi 连接 （时间判定）
//     // { nullptr            ,0          ,F_MAIN_E_TIMEOUT       , F_MAIN_S_WIFI_CONN      , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },
//     // wifi 连接 （事件判定）
//     { nullptr            ,0          ,F_MAIN_E_WIFI_CMD_TRIG , F_MAIN_S_WIFI_GUIDE     , F_MAIN_S_WIFI_CONN      ,0    ,false   ,fsm_main_wifi_connecting },
//     { nullptr            ,0          ,F_MAIN_E_WIFI_C_SUC    , F_MAIN_S_WIFI_CONN      , F_MAIN_S_WIFI_CONN_SUC  ,0    ,false   ,fsm_main_wifi_conn_suc },
//     { nullptr            ,0          ,F_MAIN_E_WIFI_C_FAIL   , F_MAIN_S_WIFI_CONN      , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },
//     // 失败后处理
//     { nullptr            ,0          ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_WIFI_CONN_FAIL , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
//     { nullptr            ,0          ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_WIFI_CONN_FAIL , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_forget },
    
//     { nullptr            ,0          ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_WIFI_CONN_SUC  , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
//     // 离线模式
//     { fsm_clock_need_cfg ,0          ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_WIFI_GUIDE     , F_MAIN_S_WIFI_OFFLINE   ,0    ,false   ,fsm_main_in_offline },
//     { fsm_clock_need_cfg ,1          ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_WIFI_GUIDE     , F_MAIN_S_WIFI_OFFLINE   ,0    ,false   ,fsm_main_in_offline },

//     // ...
    
//     { nullptr            ,0          ,F_MAIN_E_TURN          , F_MAIN_S_CLOCK          , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_menu_turn },
// };

static struct StateTable fsm_user_table[] = {
    //区分事件组变量       编号         到来的事件               当前的状态             下一个状态                超时  立即执行  将要要执行的函数

    //开机
    {nullptr             ,0            ,F_MAIN_E_INIT              ,F_MAIN_S_UNINIT         ,F_MAIN_S_UNINIT_PLAYING  ,0  ,false    ,  fsm_main_uninit_playing},//开机动画
    {nullptr             ,0            ,F_MAIN_E_ANIM_PLAY_SUC     ,F_MAIN_S_UNINIT_PLAYING ,F_MAIN_S_FINDPERSONC_ANIM ,0  ,false    ,  fsm_main_lidar_find_playing},//找人动画 
    //雷达找人
    {nullptr             ,0            ,F_MAIN_E_ANIM_PLAY_SUC      ,F_MAIN_S_FINDPERSONC_ANIM ,F_MAIN_S_FINDPERSON        ,0   ,false       ,  nullptr },//找人动画 
    {fm_has_h_fd_state   ,FM_H_F_TOUT  ,F_MAIN_E_LIDAR_UPDATE      ,F_MAIN_S_FINDPERSON     ,F_MAIN_S_FINDPERSONC_ANIM       ,0  ,false    ,fsm_main_lidar_find_playing},//没找到人继续找人动画
    {fm_has_h_fd_state   ,FM_H_F_SUC    ,F_MAIN_E_LIDAR_FIND        ,F_MAIN_S_FINDPERSON     ,F_MAIN_S_FINDSUC_ANIM     ,0  ,false    ,   fsm_main_find_someone },//找到人动画
    {fm_has_h_fd_state   ,FM_H_F_FAI    ,F_MAIN_E_LIDAR_NOT_FOUND   ,F_MAIN_S_FINDPERSON     ,F_MAIN_S_FINDFAIL_ANIM    ,0  ,false    ,  fsm_main_no_find_someone},//超时动画   
    //找人动画结束
    {nullptr             ,0            ,F_MAIN_E_ANIM_PLAY_SUC     ,F_MAIN_S_FINDSUC_ANIM   ,F_MAIN_S_FINDSUC           ,0  ,false    ,    fsm_main_lidar_find_suc }, //成功找人动画结束
    {nullptr             ,0            ,F_MAIN_E_ANIM_PLAY_SUC     ,F_MAIN_S_FINDFAIL_ANIM  ,F_MAIN_S_FINDFAIL          ,0  ,false    ,    fsm_main_lidar_find_fail },//找不人动画结束
    {nullptr             ,0            ,F_MAIN_E_DEV_MOVE          ,F_MAIN_S_FINDFAIL       ,F_MAIN_S_FINDPERSON        ,0  ,false    ,     fsm_main_lidar_find_playing},//移动设备重新找人
    //找人---网络
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED        , F_MAIN_S_FINDSUC        , F_MAIN_S_WIFI_DETECT     ,0    ,false        , nullptr},  //找人成功跳转检测wifi
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED       , F_MAIN_S_FINDFAIL         , F_MAIN_S_WIFI_DETECT    ,0    ,false         ,nullptr },  //找人失败跳转检测wifi

    { fm_has_w_c_state           ,FM_W_N_CFG   ,F_MAIN_E_WIFI_C_UNCFG    , F_MAIN_S_WIFI_DETECT     , F_MAIN_S_WIFI_GUIDE        ,0    ,false         ,fsm_main_wifi_guide },  //未配置去二维码
    { fm_has_w_c_state           ,FM_W_CONN    ,F_MAIN_E_WIFI_C_CONN     , F_MAIN_S_WIFI_DETECT     , F_MAIN_S_WIFI_CONN         ,0   ,false          ,fsm_main_wifi_connecting },  //连接中
    { fm_has_w_c_state           ,FM_W_SUC     ,F_MAIN_E_WIFI_C_SUC      , F_MAIN_S_WIFI_DETECT     , F_MAIN_S_CLOCK             ,0    ,false          , fsm_main_to_clock},    //连接成功去主页面
    { fm_has_w_c_state           ,FM_W_FAI     ,F_MAIN_E_WIFI_C_FAIL     , F_MAIN_S_WIFI_DETECT     , F_MAIN_S_WIFI_GUIDE        ,0    ,false         , fsm_main_wifi_guide},        //连接失败去二维码
    //连接状态判断
    { nullptr           ,0           ,F_MAIN_E_WIFI_C_SUC      , F_MAIN_S_WIFI_CONN         , F_MAIN_S_WIFI_CONN_SUC    ,0    ,false         , fsm_main_wifi_conn_suc}, //连接成功
     { nullptr           ,0         ,F_MAIN_E_BTN_CLICKED    , F_MAIN_S_WIFI_CONN_SUC       , F_MAIN_S_CLOCK            ,0    ,false         ,fsm_main_to_clock }, //连接成功去时钟
    { nullptr           ,0           ,F_MAIN_E_WIFI_C_FAIL     , F_MAIN_S_WIFI_CONN         , F_MAIN_S_WIFI_GUIDE       ,0    ,false         , fsm_main_wifi_guide}, //连接失败去二维码
    //二维码
    { nullptr           ,0            ,F_MAIN_E_WIFI_CMD_TRIG   , F_MAIN_S_WIFI_GUIDE       , F_MAIN_S_WIFI_CONN        ,0    ,false          , fsm_main_wifi_connecting},   //二维码去连接中
    { nullptr           ,0            ,F_MAIN_E_BTN_L_CLICKED   , F_MAIN_S_WIFI_GUIDE       , F_MAIN_S_RTC_DETECT       ,0    ,false          , nullptr},  //二维码去rtc
    //rtc
    { nullptr           ,0          ,F_MAIN_E_RTC_EXIST       , F_MAIN_S_RTC_DETECT         , F_MAIN_S_UNINIT_PLAYING   ,0    ,false        ,nullptr },//rtc 有去主页面
    { nullptr           ,0          ,F_MAIN_E_RTC_NOT_EXIST   , F_MAIN_S_RTC_DETECT         , F_MAIN_S_RTC_DETECT_CLK   ,0    ,false        , nullptr},//rtc 无去配置时间
    { nullptr           ,0            ,F_MAIN_E_KNOB_CW         , F_MAIN_S_RTC_DETECT_CLK   , F_MAIN_S_RTC_DETECT_CLK   ,0    ,false     ,nullptr },//rtc修改时间
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED     , F_MAIN_S_RTC_DETECT_CLK   , F_MAIN_S_CLOCK             ,0    ,false        , nullptr},//rtc去主页面
    //主页面
    { nullptr           ,0            ,F_MAIN_E_LIDAR_NOT_FOUND  , F_MAIN_S_CLOCK           , F_MAIN_S_CLOCK_AWAY        ,0    ,false        , nullptr},//主页面人走
    { nullptr           ,0            ,F_MAIN_E_LIDAR_FIND      , F_MAIN_S_CLOCK_AWAY        , F_MAIN_S_CLOCK_BACK       ,0    ,false        , nullptr},//主页面人回---过度
    { nullptr           ,0            ,F_MAIN_E_LIDAR_FIND      , F_MAIN_S_CLOCK_BACK        , F_MAIN_S_CLOCK            ,0    ,false        , nullptr},//主页面 过度--人在 
    { nullptr           ,0            ,F_MAIN_E_KNOB_CW         , F_MAIN_S_CLOCK             , F_MAIN_S_MENU             ,0    ,false        , nullptr},//主页面 去菜单  
    //菜单
    { nullptr           ,0            ,F_MAIN_E_KNOB_CW             , F_MAIN_S_MENU             , F_MAIN_S_MENU_SELECT       ,0    ,false        , nullptr},//菜单选择
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SELECT      , F_MAIN_S_MENU_WIFI         ,0    ,false        , nullptr},//菜单选择wifi    
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SELECT      , F_MAIN_S_MENU_SLEEP_MODE   ,0    ,false        , nullptr},//菜单选择唤醒模式 
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SELECT      , F_MAIN_S_MENU_ALARM        ,0    ,false        , nullptr},//菜单选择闹钟
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SELECT      , F_MAIN_S_MENU_UNWIND       ,0    ,false        , nullptr},//菜单选择歌曲选择
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SELECT      , F_MAIN_S_MENU_VOLUME       ,0    ,false        , nullptr},//菜单选择声音
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SELECT      , F_MAIN_S_MENU_BRIGHTNESS   ,0    ,false        , nullptr},//菜单选择亮度
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SELECT      , F_MAIN_S_MENU_SET_TIME     ,0    ,false        , nullptr},//菜单选择设置时间  
    //wifi设置
    { nullptr           ,0            ,F_MAIN_E_WIFI_CMD_TRIG       , F_MAIN_S_MENU_SELECT      , F_MAIN_S_WIFI_DETECT     ,0    ,false          , nullptr},//wifi 检测（不知道F_MAIN_E_WIFI_CMD_TRIG 是否正确可能要更改）
    { nullptr           ,0            ,F_MAIN_E_TIME                , F_MAIN_S_WIFI_DETECT      , F_MAIN_S_WIFI_CONN_SUC     ,0   ,false          , nullptr},// 有wifi
    { nullptr           ,0            ,F_MAIN_E_TIME                , F_MAIN_S_WIFI_DETECT      , F_MAIN_S_WIFI_OFFLINE     ,0    ,false          , nullptr}, //无wifi 要重启配置wifi
    { nullptr           ,0            ,F_MAIN_E_BTN_L_CLICKED       , F_MAIN_S_WIFI_CONN_SUC    , F_MAIN_S_WIFI_OFFLINE     ,0    ,false          , nullptr},//手动关闭wifi
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_WIFI_DETECT      , F_MAIN_S_WIFI_OFFLINE     ,0    ,false          , nullptr},//有wifi 长按退回菜单
    { nullptr           ,0            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_WIFI_CONN_SUC    , F_MAIN_S_WIFI_OFFLINE     ,0    ,false          , nullptr},//无wifi 短按退回菜单 
    //唤醒模式
    // { nullptr           ,0            ,F_MAIN_E_KNOB_CW         , F_MAIN_S_MENU_SLEEP_MODE    , F_MAIN_S_MENU_SLEEP_MODE     ,0    ,false          , nullptr},//唤醒模式 选择模式
    // { nullptr           ,0            ,F_MAIN_E_KNOB_CW         , F_MAIN_S_MENU_SLEEP_MODE    , F_MAIN_S_MENU_SLEEP_MODE     ,0    ,false          , nullptr},//唤醒模式 选择模式     
};


static uint32_t timeout_ms_tick = 0;

static void fsm_set_timeout_s(uint16_t sec) {
    if (sec) {
        if (sec != 0xffff) {
            timeout_ms_tick = esp_log_timestamp() + sec * 1000;
            ESP_LOGI("fsm_user", "timeout reset %ds", sec);
        }
    } else {
        timeout_ms_tick = 0;
    }
}

static void fsm_main_timeout_task(void* arg) {
    ( void )arg;
    fsm_handle_t fsm_handle = (fsm_handle_t)arg;
    if (fsm_handle == nullptr) {
        ESP_LOGE(TAG, "fsm_handle is nullptr");
        vTaskDelete(nullptr);
    }
    ESP_LOGI(TAG, "fsm_main_timeout_task started");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
        /**
         * @brief 超时处理
         * @details
         */
        if (timeout_ms_tick != 0 && timeout_ms_tick < esp_log_timestamp()) {
            timeout_ms_tick = 0;
            // ESP_LOGW("fsm_user", "timeout! now state : %s",
            //          state_str[fsm_get_current_state(s_main_fsm_handle)]);
            fsm_timeout_trig(fsm_handle);
        }
    }
}

static fsm_handle_t s_fsm_handle = nullptr;
int fsm_main_init(void) {
    if (s_fsm_handle != nullptr) {
        return 0;
    }
    s_fsm_handle =
        fsm_init(fsm_user_table, 
            F_MAIN_S_UNINIT, 
            sizeof(fsm_user_table),
            fsm_set_timeout_s);
    if (s_fsm_handle == nullptr) {
        ESP_LOGE(TAG, "s_main_fsm_handle init error");
        return -1;
    }

    my_thread_create(fsm_main_timeout_task, "fsm_main_timeout_task", 1024 * 4, s_fsm_handle, 5, nullptr);
    return 0;
}

void fsm_main_event_trig(enum fsm_main_event_enum event, void *arg) {
    ESP_LOGI(TAG, "Event: %s | State: %s", fsm_event_to_str(event), fsm_main_get_current_state_str());
    fsm_event_handle(s_fsm_handle, event, arg);
}

uint8_t fsm_main_get_current_state(void) {
    return fsm_get_current_state(s_fsm_handle);
}

const char* fsm_main_get_current_state_str(void) {
    return fsm_state_to_str(fsm_get_current_state(s_fsm_handle));
}
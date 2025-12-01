#include "fsm_lib.h"
#include "fsm_main.h"

#include "esp_log.h"
#include "my_utils.h"
#include "my_nvs.h"
#include "my_h264.h"
#include "esp_lvgl_port.h"
#include "my_ui_behavior.h"

static const char *TAG = "fsm_main";

// 状态名称获取
static const char* fsm_state_to_str(uint8_t state) {
    static const char* names[] = {
        "UNINIT",                    // F_MAIN_S_UNINIT (0)
        "UNINIT_PLAYING",            // F_MAIN_S_UNINIT_PLAYING (1)
        "CLOCK",                     // F_MAIN_S_CLOCK (2)
        "CLOCK_AWAY",                // F_MAIN_S_CLOCK_AWAY (3)
        "CLOCK_BACK",                // F_MAIN_S_CLOCK_BACK (4)
        "FINDPERSONC_ANIM",          // F_MAIN_S_FINDPERSONC_ANIM (5)
        "FINDPERSON",                // F_MAIN_S_FINDPERSON (6)
        "FINDSUC",                   // F_MAIN_S_FINDSUC (7)
        "FINDFAIL",                  // F_MAIN_S_FINDFAIL (8)
        "FINDSUC_ANIM",              // F_MAIN_S_FINDSUC_ANIM (9)
        "FINDFAIL_ANIM",             // F_MAIN_S_FINDFAIL_ANIM (10)
        "WIFI_DETECT",               // F_MAIN_S_WIFI_DETECT (11)
        "WIFI_GUIDE",                // F_MAIN_S_WIFI_GUIDE (12)
        "WIFI_CONN",                 // F_MAIN_S_WIFI_CONN (13)
        "WIFI_CONN_SUC",             // F_MAIN_S_WIFI_CONN_SUC (14)
        "WIFI_CONN_FAIL",            // F_MAIN_S_WIFI_CONN_FAIL (15)
        "WIFI_OFFLINE",              // F_MAIN_S_WIFI_OFFLINE (16)
        "RTC_DETECT",                // F_MAIN_S_RTC_DETECT (17)
        "RTC_DETECT_CLK",            // F_MAIN_S_RTC_DETECT_CLK (18)
        "MENU",                      // F_MAIN_S_MENU (19)
        "MENU_SLEEP_MODE",           // F_MAIN_S_MENU_SLEEP_MODE (20)
        "MENU_ALARM",                // F_MAIN_S_MENU_ALARM (21)
        "NO_MENU_ALARM",             // F_MAIN_S_NO_MENU_ALARM (22)
        "MENU_UNWIND",               // F_MAIN_S_MENU_UNWIND (23)
        "MENU_VOLUME",               // F_MAIN_S_MENU_VOLUME (24)
        "MENU_BRIGHTNESS",           // F_MAIN_S_MENU_BRIGHTNESS (25)
        "MENU_SET_TIME",             // F_MAIN_S_MENU_SET_TIME (26)
        "MENU_UNWIND_PLAYING",       // F_MAIN_S_MENU_UNWIND_PLAYING (27)
    };
    if (state < sizeof(names)/sizeof(names[0]) && names[state]) {
        return names[state];
    }
    return "UNKNOWN";
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

static struct StateTable fsm_user_table[] = {
    //区分事件组变量       编号         到来的事件               当前的状态             下一个状态                超时  立即执行  将要要执行的函数

    //开机
    {nullptr             ,0            ,F_MAIN_E_INIT              ,F_MAIN_S_UNINIT         ,F_MAIN_S_UNINIT_PLAYING  ,0  ,false    ,  fsm_main_uninit_playing},//开机动画
    {nullptr             ,0            ,F_MAIN_E_ANIM_PLAY_SUC     ,F_MAIN_S_UNINIT_PLAYING ,F_MAIN_S_FINDPERSONC_ANIM ,0  ,false    ,  fsm_main_lidar_find_playing},//找人动画 
    //雷达找人
    {fm_has_h_fd_state   ,FM_H_F_FAI    ,F_MAIN_E_ANIM_PLAY_SUC      , F_MAIN_S_FINDPERSONC_ANIM    ,F_MAIN_S_FINDPERSONC_ANIM       ,0  ,false    ,fsm_main_lidar_find_playing},//没找到人继续找人动画
    {fm_has_h_fd_state   ,FM_H_F_SUC    ,F_MAIN_E_ANIM_PLAY_SUC      ,F_MAIN_S_FINDPERSONC_ANIM     ,F_MAIN_S_FINDSUC_ANIM     ,0  ,false    ,   fsm_main_find_someone },//找到人动画
    {fm_has_h_fd_state   ,FM_H_F_TOUT    ,F_MAIN_E_ANIM_PLAY_SUC   ,F_MAIN_S_FINDPERSONC_ANIM     ,F_MAIN_S_FINDFAIL_ANIM    ,0  ,false    ,  fsm_main_no_find_someone},//超时动画   
    //找人动画结束
    {nullptr             ,0            ,F_MAIN_E_ANIM_PLAY_SUC     ,F_MAIN_S_FINDSUC_ANIM   ,F_MAIN_S_FINDSUC           ,0  ,false    ,    fsm_main_lidar_find_suc }, //成功找人动画结束
    {nullptr             ,0            ,F_MAIN_E_ANIM_PLAY_SUC     ,F_MAIN_S_FINDFAIL_ANIM  ,F_MAIN_S_FINDFAIL          ,0  ,false    ,    fsm_main_lidar_find_fail },//找不人动画结束
    {nullptr             ,0            ,F_MAIN_E_DEV_MOVE          ,F_MAIN_S_FINDFAIL       ,F_MAIN_S_FINDPERSON        ,0  ,false    ,     fsm_main_lidar_find_playing},//移动设备重新找人 //移动设备也没弄  

    // { fm_has_w_c_state           ,FM_W_N_CFG   ,F_MAIN_E_BTN_CLICKED    , F_MAIN_S_FINDSUC     , F_MAIN_S_WIFI_GUIDE        ,0    ,false         ,fsm_main_wifi_guide },  //未配置去二维码
    // { fm_has_w_c_state           ,FM_W_CONN    ,F_MAIN_E_BTN_CLICKED     , F_MAIN_S_FINDSUC     , F_MAIN_S_WIFI_CONN         ,0   ,false          ,fsm_main_wifi_connecting },  //连接中
    // { fm_has_w_c_state           ,FM_W_SUC     ,F_MAIN_E_BTN_CLICKED      , F_MAIN_S_FINDSUC     , F_MAIN_S_CLOCK             ,0    ,false          , fsm_main_to_clock},    //连接成功去主页面
    // { fm_has_w_c_state           ,FM_W_FAI     ,F_MAIN_E_BTN_CLICKED     , F_MAIN_S_FINDSUC     , F_MAIN_S_WIFI_GUIDE        ,0    ,false         , fsm_main_wifi_guide},        //连接失败去二维码

    
    { fm_has_w_c_state           ,FM_W_N_CFG   ,F_MAIN_E_BTN_CLICKED    , F_MAIN_S_FINDFAIL     , F_MAIN_S_WIFI_GUIDE        ,0    ,false         ,fsm_main_wifi_guide },           //未配置去二维码
    { fm_has_w_c_state           ,FM_W_CONN    ,F_MAIN_E_BTN_CLICKED     , F_MAIN_S_FINDFAIL     , F_MAIN_S_WIFI_CONN         ,0   ,false          ,fsm_main_wifi_connecting },     //连接中
    { fm_has_w_c_state           ,FM_W_SUC     ,F_MAIN_E_BTN_CLICKED      , F_MAIN_S_FINDFAIL     , F_MAIN_S_CLOCK             ,0    ,false          , fsm_main_to_clock},          //连接成功去主页面
    { fm_has_w_c_state           ,FM_W_FAI     ,F_MAIN_E_BTN_CLICKED     , F_MAIN_S_FINDFAIL     , F_MAIN_S_WIFI_GUIDE        ,0    ,false         , fsm_main_wifi_guide},           //连接失败去二维码
    //连接状态判断 
    { fm_has_w_c_state           ,FM_W_SUC      ,F_MAIN_E_WIFI_C_SUC      , F_MAIN_S_WIFI_CONN         , F_MAIN_S_WIFI_CONN_SUC    ,0    ,false         , fsm_main_wifi_conn_suc}, //连接中---连接成功
    { fm_has_w_c_state           ,FM_W_FAI     ,F_MAIN_E_WIFI_C_FAIL     , F_MAIN_S_WIFI_CONN         , F_MAIN_S_WIFI_GUIDE       ,0    ,false         , fsm_main_wifi_guide}, //连接中----去二维码
    {nullptr                     ,0             ,F_MAIN_E_BTN_CLICKED    , F_MAIN_S_WIFI_CONN_SUC       , F_MAIN_S_CLOCK            ,0    ,false         ,fsm_main_to_clock }, //连接成功-----时钟
    //二维码
    { nullptr           ,0                  ,F_MAIN_E_WIFI_CMD_TRIG   , F_MAIN_S_WIFI_GUIDE       , F_MAIN_S_WIFI_CONN        ,0    ,false          , fsm_main_wifi_connecting},   //二维码去连接中
    //rtcF_MAIN_E_LIDAR_FIND
    { fm_has_rtc_state   ,FM_RTC_EXIST       ,F_MAIN_E_BTN_CLICKED    , F_MAIN_S_FINDSUC         , F_MAIN_S_CLOCK   ,0    ,false        ,fsm_main_to_clock },//二维码去 ---rtc 有主页面
    { fm_has_rtc_state   ,FM_RTC_NO_EXIST   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC         , F_MAIN_S_RTC_DETECT_CLK   ,0    ,false        , fsm_main_set_time},//二维码去 ---rtc 无配置时间

    { fm_has_rtc_state   ,FM_RTC_EXIST       ,F_MAIN_E_BTN_CLICKED    , F_MAIN_S_FINDFAIL         , F_MAIN_S_CLOCK   ,0    ,false        ,fsm_main_to_clock },//二维码去 ---rtc 有主页面
    { fm_has_rtc_state   ,FM_RTC_NO_EXIST   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL         , F_MAIN_S_RTC_DETECT_CLK   ,0    ,false        , fsm_main_set_time},//二维码去 ---rtc 无配置时间

    { nullptr           ,0                  ,F_MAIN_E_KNOB_CW         , F_MAIN_S_RTC_DETECT_CLK   , F_MAIN_S_RTC_DETECT_CLK   ,0    ,false           ,fsm_main_rtc_adjust_time },//rtc修改时间
    { nullptr           ,0                  ,F_MAIN_E_BTN_CLICKED     , F_MAIN_S_RTC_DETECT_CLK   , F_MAIN_S_CLOCK             ,0    ,false          , fsm_main_rtc_save_and_exit},//rtc保存时间并去主页面
    //TODO:主页面
    // { nullptr           ,0            ,F_MAIN_E_LIDAR_NOT_FOUND  , F_MAIN_S_CLOCK           , F_MAIN_S_CLOCK_AWAY        ,0    ,false        , nullptr},//主页面人走
    // { nullptr           ,0            ,F_MAIN_E_LIDAR_FIND      , F_MAIN_S_CLOCK_AWAY        , F_MAIN_S_CLOCK_BACK       ,0    ,false        , nullptr},//主页面人回---过度
    // { nullptr           ,0            ,F_MAIN_E_LIDAR_FIND      , F_MAIN_S_CLOCK_BACK        , F_MAIN_S_CLOCK            ,0      ,false        , nullptr},//主页面 过度--人在 
    { nullptr           ,0              ,F_MAIN_E_KNOB_CW         , F_MAIN_S_CLOCK             , F_MAIN_S_MENU             ,10    ,true        , fsm_main_in_memu},//主页面 去菜单  
    //菜单
    { nullptr          ,0        ,F_MAIN_E_KNOB_CW             , F_MAIN_S_MENU             , F_MAIN_S_MENU       ,10    ,true              , fsm_menu_next_item},//菜单选择
    //wifi 设置
    { fm_has_memu_state           ,FM_MEMU_WIFI_SC              ,F_MAIN_E_BTN_CLICKED           , F_MAIN_S_MENU             , F_MAIN_S_MEMU_WIFI_SUC        ,0    ,false        , fsm_main_in_wifi_sc},//菜单---有wifi 根据wifi检测判断
    { fm_has_memu_state           ,FM_MEMU_WIFI_FA              ,F_MAIN_E_BTN_CLICKED           , F_MAIN_S_MENU             , F_MAIN_S_MEMU_WIFI_FAILE       ,0    ,false        , fsm_main_in_wifi_fa},//菜单---无wifi
    { nullptr                     ,0                            ,F_MAIN_E_BTN_L_CLICKED       , F_MAIN_S_MEMU_WIFI_SUC        , F_MAIN_S_MEMU_WIFI_FAILE       ,0    ,false          , fsm_main_in_wifi_fa},//手动关闭wifi
    { nullptr                     ,0                            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MEMU_WIFI_SUC       , F_MAIN_S_MENU                ,10    ,true          , fsm_main_in_memu},//有wifi 短按退回菜单
    { nullptr                    ,0                             ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MEMU_WIFI_FAILE        , F_MAIN_S_MENU                ,10    ,true          , fsm_main_in_memu},//无wifi 短按退回菜单 
    //唤醒模式
    { fm_has_memu_state           ,FM_MEMU_WAKE_MOD            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU             , F_MAIN_S_MENU_SLEEP_MODE     ,0    ,false    , fsm_main_in_wake_mode},//菜单选择唤醒模式
    { nullptr                     ,0                           ,F_MAIN_E_KNOB_CW             , F_MAIN_S_MENU_SLEEP_MODE   , F_MAIN_S_MENU_SLEEP_MODE     ,0    ,false    , fsm_wake_mode_next_item},//唤醒模式的二级菜单切换
    { nullptr                     ,0                           ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_SLEEP_MODE   , F_MAIN_S_MENU               ,10    ,true    , fsm_main_in_memu},//唤醒模式---菜单
    //闹钟
    { fm_has_memu_state   ,FM_MEMU_ALARM_SC     ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU             , F_MAIN_S_MENU_ALARM       ,0      ,false        , fsm_main_in_alarm},//菜单--有闹钟
    { fm_has_memu_state   ,FM_MEMU_ALARM_FA     ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU             , F_MAIN_S_NO_MENU_ALARM    ,0      ,false        , fsm_main_in_no_alarm},//菜单--无闹钟
    { nullptr             ,0                 ,F_MAIN_E_KNOB_CW              , F_MAIN_S_MENU_ALARM          , F_MAIN_S_MENU_ALARM      ,0      ,false        , fsm_set_alarm_item},//闹钟设置时间
    { nullptr             ,0                ,F_MAIN_E_BTN_L_CLICKED          , F_MAIN_S_MENU_ALARM         , F_MAIN_S_NO_MENU_ALARM      ,0      ,false        , fsm_main_in_no_alarm},//闹钟--关闭闹钟
    { nullptr             ,0                ,F_MAIN_E_BTN_L_CLICKED          , F_MAIN_S_NO_MENU_ALARM       , F_MAIN_S_MENU_ALARM     ,0      ,false        , fsm_main_in_alarm},//无闹钟--开启闹钟
    { nullptr             ,0                ,F_MAIN_E_BTN_CLICKED          , F_MAIN_S_MENU_ALARM             , F_MAIN_S_MENU     ,10      ,true       , fsm_main_in_memu},//闹钟--菜单
    { nullptr             ,0                ,F_MAIN_E_BTN_CLICKED          , F_MAIN_S_NO_MENU_ALARM          , F_MAIN_S_MENU     ,10      ,true       , fsm_main_in_memu},//无脑闹钟--菜单
    //UNWIND 
    { fm_has_memu_state    ,FM_MEMU_UNWIND            ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU             , F_MAIN_S_MENU_UNWIND       ,0    ,false       , fsm_main_in_unwind},//菜单---歌曲选择
    { nullptr              ,0                         ,F_MAIN_E_KNOB_CW             , F_MAIN_S_MENU_UNWIND       , F_MAIN_S_MENU_UNWIND       ,0    ,false       , fsm_unwind_next_item},//切换歌曲
    { fm_has_h_fd_state     ,FM_H_F_SUC               ,F_MAIN_E_LIDAR_FIND           , F_MAIN_S_MENU_UNWIND       , F_MAIN_S_MENU_UNWIND_PLAYING ,0    ,false       , fsm_main_memu_cat_playing},//歌曲切换---动画
    { nullptr              ,0                         ,F_MAIN_E_ANIM_PLAY_SUC     , F_MAIN_S_MENU_UNWIND_PLAYING  , F_MAIN_S_MENU_UNWIND       ,0    ,false       , fsm_main_in_unwind},//动画---歌曲切换
    { nullptr              ,0                         ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_UNWIND       , F_MAIN_S_MENU              ,10    ,true       , fsm_main_in_memu},//歌曲--菜单
    //声音
    { fm_has_memu_state     ,FM_MEMU_VOL           ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU                 , F_MAIN_S_MENU_VOLUME       ,0    ,false         , fsm_main_in_volume},//菜单选择声音
    { nullptr               ,0                     ,F_MAIN_E_KNOB_CW             , F_MAIN_S_MENU_VOLUME       , F_MAIN_S_MENU_VOLUME       ,0    ,false         , fsm_volume_next_item},//设置声音
    { nullptr               ,0                      ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU_VOLUME       , F_MAIN_S_MENU              ,10    ,true         , fsm_main_in_memu},//声音--菜单
    //亮度
    { fm_has_memu_state      ,FM_MEMU_SC_BR        ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU              , F_MAIN_S_MENU_BRIGHTNESS   ,0    ,false       , fsm_main_in_light},//菜单选择亮度
    { nullptr                 ,0                   ,F_MAIN_E_KNOB_CW           , F_MAIN_S_MENU_BRIGHTNESS     , F_MAIN_S_MENU_BRIGHTNESS   ,0    ,false       , fsm_light_next_item},//设置亮度
    { nullptr                 ,0                    ,F_MAIN_E_BTN_CLICKED        , F_MAIN_S_MENU_BRIGHTNESS    , F_MAIN_S_MENU             ,10    ,true      , fsm_main_in_memu},//亮度--菜单
    //设置时间
    { fm_has_memu_state       ,FM_MEMU_SETTIME      ,F_MAIN_E_BTN_CLICKED         , F_MAIN_S_MENU            , F_MAIN_S_RTC_DETECT_CLK     ,0    ,false     , fsm_main_set_time},//菜单选择设置时间  
    //菜单返回主页面  
    {nullptr                   ,0                   ,F_MAIN_E_TIMEOUT               , F_MAIN_S_MENU                  , F_MAIN_S_CLOCK                ,0    ,false     , fsm_main_to_clock},//菜单---主页面
    {nullptr                   ,0                   ,F_MAIN_E_BTN_L_CLICKED         , F_MAIN_S_MENU                  , F_MAIN_S_CLOCK                ,0    ,false     , fsm_main_to_clock},//菜单---主页面 
    
    //主页面去
    // {nullptr             ,0                     ,F_MAIN_E_BTN_CLICKED     ,F_MAIN_S_CLOCK                    ,F_MAIN_S_MEMU_FINDPERSONC_ANIM    ,0  ,false    ,  fsm_main_lidar_find_playing},//主页面--找人动画
    {nullptr             ,0                     ,F_MAIN_E_BTN_CLICKED     ,F_MAIN_S_CLOCK                     ,F_MAIN_S_BOYA_DATA               ,0  ,false    ,  fsm_main_in_boya_data},//主页面--睡眠数据
    {nullptr             ,0                     ,F_MAIN_E_BTN_CLICKED     ,F_MAIN_S_BOYA_DATA                   ,F_MAIN_S_RadarInfo               ,0  ,false    ,  fsm_main_in_radarinfo},//睡眠数据--雷达数据
    {nullptr             ,0                     ,F_MAIN_E_BTN_CLICKED     ,F_MAIN_S_RadarInfo                 ,F_MAIN_S_GoodMorning_DEMO     ,0  ,false    ,  fsm_main_in_GoodMorning_demo},//雷达数据---早报dome
    {nullptr             ,0                     ,F_MAIN_E_BTN_CLICKED     ,F_MAIN_S_GoodMorning_DEMO          ,F_MAIN_S_REMINDER            ,0  ,false    ,  fsm_main_in_reminder_tomorrow},//早报dome---提醒 
    {nullptr             ,0                     , F_MAIN_E_BTN_CLICKED   ,F_MAIN_S_REMINDER                   ,F_MAIN_S_SLEEPMODE     ,0  ,false    ,  fsm_main_in_sleep_mode},//提醒--入睡提示dome
    {nullptr             ,0                     , F_MAIN_E_BTN_CLICKED   ,F_MAIN_S_SLEEPMODE                   ,F_MAIN_S_NIGHTMODE     ,0  ,false    ,  fsm_main_in_night_mode},//入睡提示dome--夜间模式
    {nullptr             ,0                     , F_MAIN_E_BTN_CLICKED   ,F_MAIN_S_NIGHTMODE                   ,F_MAIN_S_CLOCK     ,0  ,false    ,  fsm_main_to_clock},//夜间模式--主页面

    // //状态卡片  10s退回
    // {nullptr             ,0                     , F_MAIN_E_TIMEOUT    ,F_MAIN_S_BOYA_DATA              ,F_MAIN_S_CLOCK     ,0  ,false    ,  fsm_main_to_clock},//睡眠数据--主页面
    // {nullptr             ,0                     , F_MAIN_E_TIMEOUT    ,F_MAIN_S_RadarInfo              ,F_MAIN_S_CLOCK     ,0  ,false    ,  fsm_main_to_clock},//雷达数据--主页面 
    // {nullptr             ,0                     , F_MAIN_E_TIMEOUT    ,F_MAIN_S_GoodMorning_DEMO       ,F_MAIN_S_CLOCK     ,0  ,false    ,  fsm_main_to_clock},//早报dome--主页面 
    // {nullptr             ,0                     , F_MAIN_E_TIMEOUT   ,F_MAIN_S_REMINDER               ,F_MAIN_S_CLOCK     ,0  ,false    ,  fsm_main_to_clock},//提醒--主页面  
    
     //TODO:状态卡片的给更新事件   睡眠数据的更新函数/雷达数据的更新函数要添加
    {nullptr             ,0                     , F_MAIN_E_BOYA_DATA_UPDATE    ,F_MAIN_S_BOYA_DATA              ,F_MAIN_S_BOYA_DATA     ,0  ,false    ,  nullptr},//睡眠数据的更新函数
    {nullptr             ,0                     , F_MAIN_E_RadarInfo_UPDATE    ,F_MAIN_S_RadarInfo              ,F_MAIN_S_RadarInfo     ,0  ,false    ,  nullptr},//雷达数据的更新函数

    // 其他状态 
    //TODO:ota  更新行为
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_UNINIT_PLAYING                   ,F_MAIN_S_OTA     ,0  ,false    ,    fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_FINDSUC_ANIM                   ,F_MAIN_S_OTA     ,0  ,false    ,      fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_FINDFAIL_ANIM                   ,F_MAIN_S_OTA     ,0  ,false    ,     fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_FINDSUC                   ,F_MAIN_S_OTA     ,0  ,false    ,           fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_FINDFAIL                   ,F_MAIN_S_OTA     ,0  ,false    ,          fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_FINDPERSON                   ,F_MAIN_S_OTA     ,0  ,false    ,        fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_WIFI_GUIDE                   ,F_MAIN_S_OTA     ,0  ,false    ,        fsm_main_in_OTA},//ota 
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_WIFI_CONN                   ,F_MAIN_S_OTA     ,0  ,false    ,         fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_CLOCK                   ,F_MAIN_S_OTA     ,0  ,false    ,             fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_WIFI_CONN_SUC                   ,F_MAIN_S_OTA     ,0  ,false    ,     fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_RTC_DETECT_CLK                   ,F_MAIN_S_OTA     ,0  ,false    ,    fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MENU                   ,F_MAIN_S_OTA     ,0  ,false    ,              fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MEMU_WIFI_SUC                   ,F_MAIN_S_OTA     ,0  ,false    ,     fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MEMU_WIFI_FAILE                  ,F_MAIN_S_OTA     ,0  ,false    ,    fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MENU_SLEEP_MODE                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota 
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MENU_ALARM                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_NO_MENU_ALARM                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MENU_UNWIND                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MENU_VOLUME                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MENU_BRIGHTNESS                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_MEMU_FINDPERSONC_ANIM                  ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_BOYA_DATA                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_RadarInfo                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota 
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_GoodMorning_DEMO                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_REMINDER                   ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota
    {nullptr             ,0                     ,F_MAIN_E_OTA_UPDATE      ,F_MAIN_S_OTA                        ,F_MAIN_S_OTA     ,0  ,false    ,  fsm_main_in_OTA},//ota


};


static uint32_t timeout_ms_tick = 0;
static fsm_handle_t s_fsm_handle = nullptr;

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

void fsm_main_timeout_flush(void) {
    /**
     * @brief 超时处理
     * @details 由主线程定时调用，检查并触发超时事件
     */
    if (s_fsm_handle == nullptr) {
        return;
    }
    if (timeout_ms_tick != 0 && timeout_ms_tick < esp_log_timestamp()) {
        timeout_ms_tick = 0;
        // ESP_LOGW("fsm_user", "timeout! now state : %s",
        //          state_str[fsm_get_current_state(s_main_fsm_handle)]);
        fsm_timeout_trig(s_fsm_handle);
    }
}
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

    // 不再创建独立线程，改为在主线程中调用 fsm_main_timeout_flush()
    return 0;
}

void fsm_main_event_trig(enum fsm_main_event_enum event, void *arg) {
    auto last_state_str = fsm_main_get_current_state_str();
    auto last_state_id = fsm_main_get_current_state();
    auto err = fsm_event_handle(s_fsm_handle, event, arg);
    switch (err)
    {
    case 0:
        ESP_LOGI(TAG, "fsm_main_event_trig %s,%s(id=%d) -> %s(id=%d)", 
                 fsm_event_to_str(event), last_state_str, last_state_id, 
                 fsm_main_get_current_state_str(), fsm_main_get_current_state());
        break;
    case -1:
        // 找不到分支
        ESP_LOGW(TAG, "fsm_main_event_trig %s,%s(id=%d) -> no match (err=-1)", 
                 fsm_event_to_str(event), last_state_str, last_state_id);
        break;
    case -2:
        ESP_LOGW(TAG, "fsm_main_event_trig sem trig");
        break;
    default:
        ESP_LOGE(TAG, "fsm_main_event_trig unknow err %d", err);
        break;
    }
}

uint8_t fsm_main_get_current_state(void) {
    return fsm_get_current_state(s_fsm_handle);
}

const char* fsm_main_get_current_state_str(void) {
    return fsm_state_to_str(fsm_get_current_state(s_fsm_handle));
}

// 雷达检测使能标志位
static bool s_radar_detect_enabled = false;

void fsm_main_set_radar_detect_enabled(bool enabled) {
    s_radar_detect_enabled = enabled;
    ESP_LOGI(TAG, "radar_detect_enabled set to %d", enabled);
}

bool fsm_main_get_radar_detect_enabled(void) {
    return s_radar_detect_enabled;
}
#include "fsm_lib.h"
#include "fsm_main.h"

#include "esp_log.h"
#include "my_utils.h"
#include "my_nvs.h"

static const char *TAG = "fsm_main";


enum fsm_main_state_enum {
    F_MAIN_S_UNINIT,            // 未初始化
    F_MAIN_S_CLOCK,             // 时钟页面
    // 找人
    F_MAIN_S_FINDPERSON,        // 找人中
    F_MAIN_S_FINDSUC,           // 找人成功
    F_MAIN_S_FINDFAIL,          // 找人失败
    // Wifi连接
    F_MAIN_S_WIFI_GUIDE,        // 指引连接wifi
    F_MAIN_S_WIFI_CONN,         // wifi连接中
    F_MAIN_S_WIFI_OFFLINE,      // wifi离线
    F_MAIN_S_WIFI_CONN_SUC,     // wifi连接成功
    F_MAIN_S_WIFI_CONN_FAIL,    // wifi连接失败
};

// 事件名称获取
static const char* fsm_event_to_str(uint8_t event) {
    if (event == F_MAIN_E_TIMEOUT) return "TIMEOUT";
    
    static const char* names[] = {
        [F_MAIN_E_INIT] = "INIT",
        [F_MAIN_E_LIDAR_FIND] = "LIDAR_FIND",
        [F_MAIN_E_LIDAR_UPDATE] = "LIDAR_UPDATE",
        [F_MAIN_E_LIDAR_NOT_FOUND] = "LIDAR_NOT_FOUND",
        [F_MAIN_E_DEV_MOVE] = "DEV_MOVE",
        [F_MAIN_E_WIFI_CMD_TRIG] = "WIFI_CMD_TRIG",
        [F_MAIN_E_WIFI_C_SUC] = "WIFI_C_SUC",
        [F_MAIN_E_WIFI_C_FAIL] = "WIFI_C_FAIL",
        [F_MAIN_E_BTN_CLICKED] = "BTN_CLICKED",
        [F_MAIN_E_BTN_L_CLICKED] = "BTN_L_CLICKED",
    };
    return (event < sizeof(names)/sizeof(names[0]) && names[event]) ? names[event] : "UNKNOWN";
}

// 状态名称获取
static const char* fsm_state_to_str(uint8_t state) {
    static const char* names[] = {
        [F_MAIN_S_UNINIT] = "UNINIT",
        [F_MAIN_S_CLOCK] = "CLOCK",
        [F_MAIN_S_FINDPERSON] = "FINDPERSON",
        [F_MAIN_S_FINDSUC] = "FINDSUC",
        [F_MAIN_S_FINDFAIL] = "FINDFAIL",
        [F_MAIN_S_WIFI_GUIDE] = "WIFI_GUIDE",
        [F_MAIN_S_WIFI_CONN] = "WIFI_CONN",
        [F_MAIN_S_WIFI_OFFLINE] = "WIFI_OFFLINE",
        [F_MAIN_S_WIFI_CONN_SUC] = "WIFI_CONN_SUC",
        [F_MAIN_S_WIFI_CONN_FAIL] = "WIFI_CONN_FAIL",
    };
    return (state < sizeof(names)/sizeof(names[0]) && names[state]) ? names[state] : "UNKNOWN";
}

static struct StateTable fsm_user_table[] = {
    //区分事件组变量       编号         到来的事件               当前的状态            下一个状态         超时  立即执行  将要要执行的函数
    { nullptr            ,0          ,F_MAIN_E_INIT          , F_MAIN_S_UNINIT         , F_MAIN_S_FINDPERSON     ,15   ,false   ,fsm_main_lidar_find },
    { nullptr            ,0          ,F_MAIN_E_LIDAR_UPDATE  , F_MAIN_S_CLOCK          , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_lidar_clock_update },
    // 找人 
    { nullptr            ,0          ,F_MAIN_E_DEV_MOVE      , F_MAIN_S_FINDFAIL       , F_MAIN_S_FINDPERSON     ,15   ,false   ,fsm_main_lidar_find      },
    { nullptr            ,0          ,F_MAIN_E_LIDAR_FIND    , F_MAIN_S_FINDPERSON     , F_MAIN_S_FINDSUC        ,5    ,false   ,fsm_main_lidar_find_suc  },
    { nullptr            ,0          ,F_MAIN_E_TIMEOUT       , F_MAIN_S_FINDPERSON     , F_MAIN_S_FINDFAIL       ,0    ,false   ,fsm_main_lidar_find_fail },
    // 网络相关
    { fm_has_w_c_state   ,FM_W_N_CFG ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_guide },
    { fm_has_w_c_state   ,FM_W_CONN  ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
    { fm_has_w_c_state   ,FM_W_SUC   ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
    { fm_has_w_c_state   ,FM_W_FAI   ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_FINDPERSON     , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },

    { fm_has_w_c_state   ,FM_W_N_CFG ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_guide },
    { fm_has_w_c_state   ,FM_W_CONN  ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
    { fm_has_w_c_state   ,FM_W_SUC   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
    { fm_has_w_c_state   ,FM_W_FAI   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDSUC        , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },

    { fm_has_w_c_state   ,FM_W_N_CFG ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_guide },
    { fm_has_w_c_state   ,FM_W_CONN  ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
    { fm_has_w_c_state   ,FM_W_SUC   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
    { fm_has_w_c_state   ,FM_W_FAI   ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_FINDFAIL       , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },
    //区分事件组变量       编号         到来的事件               当前的状态            下一个状态         超时  立即执行  将要要执行的函数
    // 指引连接wifi
    // wifi 连接 （时间判定）
    // { nullptr            ,0          ,F_MAIN_E_TIMEOUT       , F_MAIN_S_WIFI_CONN      , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },
    // wifi 连接 （事件判定）
    { nullptr            ,0          ,F_MAIN_E_WIFI_CMD_TRIG , F_MAIN_S_WIFI_GUIDE     , F_MAIN_S_WIFI_CONN      ,0    ,false   ,fsm_main_wifi_connecting },
    { nullptr            ,0          ,F_MAIN_E_WIFI_C_SUC    , F_MAIN_S_WIFI_CONN      , F_MAIN_S_WIFI_CONN_SUC  ,0    ,false   ,fsm_main_wifi_conn_suc },
    { nullptr            ,0          ,F_MAIN_E_WIFI_C_FAIL   , F_MAIN_S_WIFI_CONN      , F_MAIN_S_WIFI_CONN_FAIL ,0    ,false   ,fsm_main_wifi_conn_fail },
    // 失败后处理
    { nullptr            ,0          ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_WIFI_CONN_FAIL , F_MAIN_S_WIFI_CONN      ,10   ,false   ,fsm_main_wifi_connecting },
    { nullptr            ,0          ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_WIFI_CONN_FAIL , F_MAIN_S_WIFI_GUIDE     ,0    ,false   ,fsm_main_wifi_forget },
    
    { nullptr            ,0          ,F_MAIN_E_BTN_CLICKED   , F_MAIN_S_WIFI_CONN_SUC  , F_MAIN_S_CLOCK          ,0    ,false   ,fsm_main_to_clock },
    // 离线模式
    { fsm_clock_need_cfg ,0          ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_WIFI_GUIDE     , F_MAIN_S_WIFI_OFFLINE   ,0    ,false   ,fsm_main_in_offline },
    { fsm_clock_need_cfg ,1          ,F_MAIN_E_BTN_L_CLICKED , F_MAIN_S_WIFI_GUIDE     , F_MAIN_S_WIFI_OFFLINE   ,0    ,false   ,fsm_main_in_offline },
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
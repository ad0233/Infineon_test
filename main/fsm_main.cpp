#include "fsm_lib.h"
#include "fsm_main.h"

#include "esp_log.h"
#include "my_utils.h"
#include "my_nvs.h"

static const char *TAG = "fsm_main";

enum fsm_main_event_enum {
    F_MAIN_E_INIT,
    F_MAIN_E_BTN_CLICKED,
    F_MAIN_E_TIMEOUT = 255,
};

enum fsm_main_state_enum {
    F_MAIN_S_UNINIT,
    // 找人
    F_MAIN_S_FINDPERSON,
    F_MAIN_S_FINDSUC,
    F_MAIN_S_FINDFAIL,
    // Wifi连接
    F_MAIN_S_WIFI_GUIDE,
    F_MAIN_S_WIFI_CONN,
};

static struct StateTable fsm_user_table[] = {
    //区分事件组变量       编号     到来的事件                 当前的状态          下一个状态         超时  立即执行  将要要执行的函数
    { nullptr            ,0   ,F_MAIN_E_INIT        , F_MAIN_S_UNINIT     , F_MAIN_S_FINDPERSON  ,15  ,false , fsm_main_do_init    },
    // 找人
    { nullptr            ,0   ,F_MAIN_E_INIT        , F_MAIN_S_FINDPERSON , F_MAIN_S_FINDSUC     ,60  ,false , fsm_main_do_init    },
    { nullptr            ,0   ,F_MAIN_E_INIT        , F_MAIN_S_FINDPERSON , F_MAIN_S_FINDSUC     ,60  ,false , fsm_main_do_init    },
    { nullptr            ,0   ,F_MAIN_E_TIMEOUT     , F_MAIN_S_FINDPERSON , F_MAIN_S_FINDFAIL    ,60  ,false , fsm_main_do_init    },
    // 指引连接wifi
    { fm_has_wifi_config ,0   ,F_MAIN_E_BTN_CLICKED , F_MAIN_S_FINDPERSON , F_MAIN_S_WIFI_GUIDE  ,60  ,false , nullptr    },
    { fm_has_wifi_config ,0   ,F_MAIN_E_BTN_CLICKED , F_MAIN_S_FINDSUC    , F_MAIN_S_WIFI_GUIDE  ,60  ,false , nullptr    },
    { fm_has_wifi_config ,0   ,F_MAIN_E_BTN_CLICKED , F_MAIN_S_FINDFAIL   , F_MAIN_S_WIFI_GUIDE  ,60  ,false , nullptr    },
    // 连接wifi
    { fm_has_wifi_config ,1   ,F_MAIN_E_BTN_CLICKED , F_MAIN_S_FINDPERSON , F_MAIN_S_WIFI_CONN   ,60  ,false , nullptr    },
    { fm_has_wifi_config ,1   ,F_MAIN_E_BTN_CLICKED , F_MAIN_S_FINDSUC    , F_MAIN_S_WIFI_CONN   ,60  ,false , nullptr    },
    { fm_has_wifi_config ,1   ,F_MAIN_E_BTN_CLICKED , F_MAIN_S_FINDFAIL   , F_MAIN_S_WIFI_CONN   ,60  ,false , nullptr    },
};

static uint32_t timeout_ms_tick = 0;

static void fsm_set_timeout_s(uint16_t sec) {
    if (sec) {
        if (sec != 0xffff) {
            timeout_ms_tick = esp_log_timestamp() + sec * 1000;
            // ESP_LOGI("fsm_user", "timeout reset %ds", sec);
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

int fsm_main_init(void) {
    static fsm_handle_t s_fsm_handle = nullptr;
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
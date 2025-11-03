# 有限元状态机


## 用例

``` CPP

enum fsm_event_enum {
    fsm_event_det_cal_start,
    fsm_event_det_meas_start,

    fsm_event_det_finish, // 检测
    
    fsm_event_timeout = 255, // 超时
};

enum fsm_state_enum {
    fsm_state_ready,           // 准备测量状态
    fsm_state_meas,            // 普通测量
    fsm_state_cal,             // 标定状态
};

static struct StateTable fsm_user_table[] = {
    //区分事件组变量  事件组编号     到来的事件                 当前的状态          下一个状态        超时  立即执行  将要要执行的函数
    /***********************************标定过程*************************************************************/
    //标定 进入 指令
    { nullptr       ,0      ,fsm_event_det_cal_start   , fsm_state_ready  , fsm_state_cal    ,60  ,false , fsm_main_cal_start    },
    //标定 结果显示
    { nullptr       ,0      ,fsm_event_det_finish      , fsm_state_cal    , fsm_state_ready  ,60  ,false , fsm_main_cal_finish   },
    { nullptr       ,0      ,fsm_event_timeout         , fsm_state_cal    , fsm_state_ready  ,60  ,false , fsm_main_det_timeout  },
    /***********************************普通测量*************************************************************/
    //测量 进入 指令
    { nullptr       ,0      ,fsm_event_det_meas_start  , fsm_state_ready  , fsm_state_meas   ,60  ,false , fsm_main_meas_start   },
    //测量 显示结果
    { nullptr       ,0      ,fsm_event_det_finish      , fsm_state_meas   , fsm_state_ready  ,60  ,false , fsm_main_meas_finish  },
    { nullptr       ,0      ,fsm_event_timeout         , fsm_state_meas   , fsm_state_ready  ,60  ,false , fsm_main_det_timeout  },
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

extern "C" void fsm_main_timeout_task(void* arg) {
    ( void )arg;
    s_main_fsm_handle =
        fsm_init(fsm_user_table, fsm_state_ready, sizeof(fsm_user_table), fsm_set_timeout_s);
    if (s_main_fsm_handle == nullptr) {
        ESP_LOGE(TAG, "s_main_fsm_handle init error");
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
            fsm_timeout_trig(s_main_fsm_handle);
        }
    }
}
```
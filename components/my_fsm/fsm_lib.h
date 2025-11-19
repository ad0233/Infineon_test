#ifndef FSM_LIB_H
#define FSM_LIB_H

#include <stdint.h>
#include <stdbool.h>

struct StateTable {
    uint8_t (*dir_p)();              // 分支变量地址
    uint8_t dir_v;               // 分支变量值
    uint8_t event;               // 事件
    uint8_t cur_state;           // 当前状态
    uint8_t next_state;          // 下一个状态
    uint16_t timeout_s;          // 0为无超时 , 0xffff(65535)为不刷新超时
    bool just_do_it;             // 立马执行
    void (*act_fun)(void *arg, uint8_t last_state, uint8_t next_state);  // 函数指针
};

typedef struct fsmImpl* fsm_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

fsm_handle_t fsm_init(struct StateTable* pTable,uint8_t cur_state,uint16_t table_size, void (*set_timeout_s)(uint16_t sec));

void fsm_deinit(fsm_handle_t handle);

uint8_t fsm_get_current_state(fsm_handle_t handle);

int fsm_event_handle(fsm_handle_t handle, uint8_t event,void *arg);

void fsm_user_set_table_timeout(fsm_handle_t handle, uint8_t state, uint16_t sec);

int fsm_timeout_trig(fsm_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // FSM_H
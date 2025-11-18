      
#include "fsm_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*状态机类型*/
struct fsmImpl {
    uint8_t sem_lock;
    struct StateTable *stateTable;//状态表
    uint8_t cur_state;            //当前状态
    uint16_t item_num;            //表的项数
    void (*set_timeout_s)(uint16_t sec);
};

static int fsm_check_table(fsm_handle_t handle);
/*状态机注册,给它一个状态表*/
fsm_handle_t fsm_init(struct StateTable* pTable,uint8_t cur_state,uint16_t table_size, void (*set_timeout_s)(uint16_t sec)) {
    fsm_handle_t handle;
    handle = malloc(sizeof(struct fsmImpl));
    if (handle == NULL) {
        return NULL;
    }
    memset(handle,0,sizeof(struct fsmImpl));
    handle->sem_lock = false;
    handle->stateTable = pTable;
    handle->cur_state = cur_state;
    handle->item_num = table_size / sizeof(struct StateTable);
    handle->set_timeout_s = set_timeout_s;

    if(fsm_check_table(handle) == -1) {
        printf("fsm_user fsm_check_table fail\n");
        return NULL;
    }

    return handle;
}

void fsm_user_set_table_timeout(fsm_handle_t handle, uint8_t state, uint16_t sec) {
    struct StateTable* pActTable = handle->stateTable;

    for (uint16_t i = 0; i < handle->item_num; i++) {
        if (state == pActTable[i].next_state) {
            pActTable[i].timeout_s = sec;
            printf("fsm_user table timeout change, event : %d , cur_state : %d , timeout_s : %d\n", pActTable[i].event, pActTable[i].cur_state, pActTable[i].timeout_s);
            break;
        }
    }
}
static int fsm_check_table(fsm_handle_t handle) {
    struct StateTable* pActTable = handle->stateTable;

    for (uint16_t i = 0; i < handle->item_num; ++i) {
        if(pActTable[i].dir_p != NULL) {
            continue;
        }
        uint16_t check_unit = pActTable[i].event << 8 | pActTable[i].cur_state;
        uint8_t check_unit_count = 0;
        for (uint16_t j = 0; j < handle->item_num; ++j) {
            if(check_unit == (pActTable[j].event << 8 | pActTable[j].cur_state)) {
                check_unit_count++;
            }
        }
        if(check_unit_count > 1) {
            printf("fsm_user table exist repeat member(event: %d cur_state: %d)\n", pActTable[i].event, pActTable[i].cur_state);
            return -1;
        }
    }
    return 0;
}

void fsm_timeout_trig(fsm_handle_t handle) {
    fsm_event_handle(handle, 255, (void*)(size_t)fsm_get_current_state(handle));
}

void fsm_deinit(fsm_handle_t handle) {
    free(handle);
}

uint8_t fsm_get_current_state(fsm_handle_t handle) {
    return handle->cur_state;
}

/*状态迁移*/
static void inline fsmStateTransfer(fsm_handle_t handle, uint8_t state) {
    handle->cur_state = state;
}

/*事件处理*/
uint8_t fsm_event_handle(fsm_handle_t handle, uint8_t event,void *arg) {
    struct StateTable* pActTable = handle->stateTable;
    void (*act_fun)() = NULL;  //函数指针初始化为空
    uint8_t next_state;
    uint8_t cur_state = handle->cur_state;
    uint8_t maxNum = handle->item_num;
    uint8_t just_do_it;
    uint8_t sem_lock = handle->sem_lock;
    uint16_t timeout_s = 0;
    uint8_t flag = 0; //标识是否满足条件

    /*获取当前动作函数*/
    for (uint8_t i = 0; i<maxNum; i++) {
        //当且仅当当前状态下来个指定的事件，我才执行它
        if (event == pActTable[i].event && cur_state == pActTable[i].cur_state) {
            act_fun = pActTable[i].act_fun;
            next_state = pActTable[i].next_state;
            timeout_s = pActTable[i].timeout_s;
            just_do_it = pActTable[i].just_do_it;
            if(pActTable[i].dir_p == NULL ) {
                flag = 1;
                break;
            } else {
                if(pActTable[i].dir_p() == pActTable[i].dir_v) {
                    flag = 1;
                    break;
                }
            }
        }
    }

    if (flag) { //如果满足条件了
        if(just_do_it) {
            //跳转到下一个状态
            fsmStateTransfer(handle, next_state);
            /*动作执行*/
            if (act_fun) {
                act_fun(arg);
            }
            handle->set_timeout_s(timeout_s);
            return 0;
        }
        
        if(!sem_lock) {
            sem_lock = 1;
            
            //跳转到下一个状态
            fsmStateTransfer(handle, next_state);
            /*动作执行*/
            if (act_fun) {
                act_fun(arg);
            }
            handle->set_timeout_s(timeout_s);
            sem_lock = 0;
            return 0;
        }
        return -2;
    }
    return -1;
}


    
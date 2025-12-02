#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void my_ui_canvas_init(void);
void my_ui_canvas_update(const uint8_t *data, uint32_t length);
void my_ui_canvas_enter(void);

#ifdef __cplusplus
}
#endif

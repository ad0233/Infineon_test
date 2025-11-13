#ifndef _UI_IMG_MANAGER_H
#define _UI_IMG_MANAGER_H

#include <stdint.h>

uint8_t* _ui_load_binary(char* fname, const uint32_t size);
uint8_t* _ui_load_compressed_binary(char* fname, const uint32_t compsize, const uint32_t size );
uint8_t* _my_ui_load_compressed_binary(const uint8_t *bin, const uint32_t compsize, const uint32_t size );

#ifdef UI_LOAD_IMAGE
#undef UI_LOAD_IMAGE
#endif
#define UI_LOAD_IMAGE _my_ui_load_compressed_binary

#endif

/* Audio stream MP3 decoder
   
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_log.h"
#include "audio_element.h"
#include "mp3_decoder.h"

static const char *TAG = "AUDIO_STREAM_MP3";

audio_element_handle_t create_player_mp3_decoder_stream(void)
{
    ESP_LOGI(TAG, "Create MP3 decoder");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_core = 1;
    mp3_cfg.task_prio = 5;
    mp3_cfg.out_rb_size = 8 * 1024;
    audio_element_handle_t decoder_stream = mp3_decoder_init(&mp3_cfg);
    return decoder_stream;
}


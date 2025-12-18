/* Audio processor example code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
 
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "sdkconfig.h"

#include "audio_recorder.h"
#include "audio_pipeline.h"
#include "recorder_sr.h"
#include "recorder_encoder.h"
#include "audio_element.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "filter_resample.h"
#include "audio_mem.h"
#include "audio_thread.h"
#include "board.h"
#include "es7210.h"

#if defined (CONFIG_AUDIO_SUPPORT_OPUS_DECODER)
#include "raw_opus_encoder.h"
#include "raw_opus_decoder.h"
#elif defined (CONFIG_AUDIO_SUPPORT_AAC_DECODER)
#include "aac_encoder.h"
#include "aac_decoder.h"
#elif defined (CONFIG_AUDIO_SUPPORT_G711A_DECODER)
#include "g711_encoder.h"
#include "g711_decoder.h"
#endif
#include "audio_stream.h"
#include "audio_processor.h"

static const char *TAG = "AUDIO_PROCESSOR";

#if (defined CONFIG_ESP32_S3_KORVO2_V3_BOARD || defined CONFIG_ESP32_S3_BOX_3_BOARD)
#define USE_ES7210_AS_RECORD_DEVICE   (1)
#define USE_ES8311_AS_RECORD_DEVICE   (0)
#elif defined CONFIG_ESP32_S3_KORVO2L_V1_BOARD
#define USE_ES7210_AS_RECORD_DEVICE   (0)
#define USE_ES8311_AS_RECORD_DEVICE   (1)
#else
#define USE_ES7210_AS_RECORD_DEVICE   (1)
#define USE_ES8311_AS_RECORD_DEVICE   (0)
#endif

// aec debug
#if CONFIG_ENABLE_RECORDER_DEBUG
#include <stdio.h>
#include <errno.h>
#include "esp_timer.h"

static bool    record_flag = true;
static FILE   *sfp         = NULL;
static int64_t start_tm    = 0;
#define AEC_DEBUDG_FILE_NAME "/sdcard/rec.pcm"
#define AEC_RECORD_TIME       (30)  // s
#endif  // ENABLE_AEC_DEBUG

#define audio_pipe_safe_free(x, fn) do { \
    if (x) {                             \
        fn(x);                           \
        x = NULL;                        \
    }                                    \
} while (0)

struct recorder_pipeline_t {
    audio_pipeline_handle_t audio_pipeline;
    audio_element_handle_t  i2s_stream_reader;
    audio_element_handle_t  audio_encoder;
    audio_element_handle_t  raw_reader;
    audio_rec_handle_t      recorder_engine;
    pipe_player_state_e     record_state;
};

struct player_pipeline_t {
    audio_pipeline_handle_t audio_pipeline;
    audio_element_handle_t  raw_writer;
    audio_element_handle_t  audio_decoder;
    audio_element_handle_t  i2s_stream_writer;
    audio_element_handle_t  player_rsp;
    pipe_player_state_e     player_state;
    uint64_t                played_bytes;      // 已播放字节数
    audio_element_info_t    audio_info;        // 音频格式信息
};

typedef struct {
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t  fatfs_stream;
    audio_element_handle_t  audio_decoder; // only support for wav; 16k, 16bit, double channel
    audio_element_handle_t  i2s_stream_writer;
    audio_event_iface_handle_t evt;        // 保存事件句柄
    pipe_player_state_e     player_state;
    bool                    running;
    tone_play_callback_t    tone_cb;
    bool                    is_switching;  // 标志位：是否正在切换音频
    audio_element_info_t    audio_info;     // 音频格式信息
    int64_t                 start_time_us;  // 播放开始时间（微秒）
} audio_player_t;

static audio_player_t             *s_audio_player      = NULL;
static struct recorder_pipeline_t *s_recorder_pipeline = NULL;
static struct player_pipeline_t   *s_player_pipeline   = NULL;

#if CONFIG_ENABLE_RECORDER_DEBUG
void aec_debug_data_write(char *data, int len)
{
    if (record_flag) {
        if (sfp == NULL) {
            sfp = fopen(AEC_DEBUDG_FILE_NAME, "wb+");
            if (sfp == NULL) {
                ESP_LOGI(TAG, "Cannot open file, reason: %s\n", strerror(errno));
                return;
            }
        }
        fwrite(data, 1, len, sfp);

        if ((esp_timer_get_time() - start_tm) / 1000000 > AEC_RECORD_TIME) {
            record_flag = false;
            fclose(sfp);
            sfp = NULL;
            ESP_LOGI(TAG, "AEC debug data write done");
        }
    }
}
#endif // ENABLE_AEC_DEBUG

recorder_pipeline_handle_t recorder_pipeline_open(void)
{
    ESP_LOGI(TAG, "%s", __func__);
    recorder_pipeline_handle_t pipeline = audio_calloc(1, sizeof(struct recorder_pipeline_t));
    pipeline->record_state = PIPE_STATE_IDLE;
    s_recorder_pipeline = pipeline;
    
    ESP_LOGI(TAG, "Create audio pipeline for recording");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline->audio_pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline->audio_pipeline);

    ESP_LOGI(TAG, "Create player audio stream");
    pipeline->i2s_stream_reader = create_record_i2s_stream();
    pipeline->raw_reader = create_record_raw_stream();

    ESP_LOGI(TAG, " Register all player elements to audio pipeline");
    audio_pipeline_register(pipeline->audio_pipeline, pipeline->i2s_stream_reader, "record_i2s");
    audio_pipeline_register(pipeline->audio_pipeline, pipeline->raw_reader, "record_raw");

    ESP_LOGI(TAG, " Link all player elements to audio pipeline");
    const char *link_tag[2] = {"record_i2s", "record_raw"};
    audio_pipeline_link(pipeline->audio_pipeline, &link_tag[0], 2);
    return pipeline;
}

esp_err_t recorder_pipeline_run(recorder_pipeline_handle_t pipeline)
{
    ESP_RETURN_ON_FALSE(pipeline != NULL, ESP_FAIL, TAG, "recorder pipeline not initialized");
    audio_pipeline_run(pipeline->audio_pipeline);
    pipeline->record_state = PIPE_STATE_RUNNING;
    return ESP_OK;
};

esp_err_t recorder_pipeline_stop(recorder_pipeline_handle_t pipeline)
{
    ESP_RETURN_ON_FALSE(pipeline != NULL, ESP_FAIL, TAG, "recorder pipeline not initialized");
    audio_pipeline_stop(pipeline->audio_pipeline);
    audio_pipeline_wait_for_stop(pipeline->audio_pipeline);
    pipeline->record_state = PIPE_STATE_IDLE;
    return ESP_OK;
};

esp_err_t recorder_pipeline_close(recorder_pipeline_handle_t pipeline)
{
    ESP_RETURN_ON_FALSE(pipeline != NULL, ESP_FAIL, TAG, "recorder pipeline not initialized");
    audio_pipeline_terminate(pipeline->audio_pipeline);
    audio_pipeline_unregister(pipeline->audio_pipeline, pipeline->i2s_stream_reader);
    audio_pipeline_unregister(pipeline->audio_pipeline, pipeline->raw_reader);

    /* Release all resources */
    audio_pipe_safe_free(pipeline->audio_pipeline, audio_pipeline_deinit);
    audio_pipe_safe_free(pipeline->raw_reader, audio_element_deinit);
    audio_pipe_safe_free(pipeline->i2s_stream_reader, audio_element_deinit);
    audio_pipe_safe_free(pipeline, audio_free);
    return ESP_OK;
};

int recorder_pipeline_get_default_read_size(recorder_pipeline_handle_t pipeline)
{
    #if defined (CONFIG_AUDIO_SUPPORT_OPUS_DECODER)
        return 80;
    #elif defined (CONFIG_AUDIO_SUPPORT_AAC_DECODER)
        return 512;
    #elif defined (CONFIG_AUDIO_SUPPORT_G711A_DECODER)
        return 160;
    #endif
    return -1;
};

audio_element_handle_t recorder_pipeline_get_raw_reader(recorder_pipeline_handle_t pipeline)
{
    return pipeline->raw_reader;
}

audio_pipeline_handle_t recorder_pipeline_get_pipeline(recorder_pipeline_handle_t pipeline)
{
    return pipeline->audio_pipeline;
};

int recorder_pipeline_read(recorder_pipeline_handle_t pipeline,char *buffer, int buf_size)
{
    return raw_stream_read(pipeline->raw_reader, buffer,buf_size);
}

static esp_err_t _player_i2s_write_cb(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    // 累加已播放字节数
    if (s_player_pipeline && len > 0) {
        s_player_pipeline->played_bytes += len;
    }
    return audio_element_output(s_player_pipeline->i2s_stream_writer, buffer, len);
}

static esp_err_t _player_write_nop_cb(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    return len;
}


player_pipeline_handle_t player_pipeline_open(void) 
{
    ESP_LOGI(TAG, "%s", __func__);
    player_pipeline_handle_t player_pipeline = audio_calloc(1, sizeof(struct player_pipeline_t));
    AUDIO_MEM_CHECK(TAG, player_pipeline, goto _exit_open);
    player_pipeline->player_state = PIPE_STATE_IDLE;
    player_pipeline->played_bytes = 0;
    memset(&player_pipeline->audio_info, 0, sizeof(audio_element_info_t));
    s_player_pipeline = player_pipeline;

    ESP_LOGI(TAG, "Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    player_pipeline->audio_pipeline = audio_pipeline_init(&pipeline_cfg);
    AUDIO_MEM_CHECK(TAG, player_pipeline, goto _exit_open);

    ESP_LOGI(TAG, "Create playback audio stream");
    player_pipeline->raw_writer = create_player_raw_stream();
    AUDIO_MEM_CHECK(TAG, player_pipeline->raw_writer, goto _exit_open);
    player_pipeline->audio_decoder = create_player_decoder_stream();
    AUDIO_MEM_CHECK(TAG, player_pipeline->audio_decoder, goto _exit_open);
    player_pipeline->i2s_stream_writer = create_player_i2s_stream(false);
    AUDIO_MEM_CHECK(TAG, player_pipeline->i2s_stream_writer, goto _exit_open);

    ESP_LOGI(TAG, "Register all elements to playback pipeline");
    audio_pipeline_register(player_pipeline->audio_pipeline, player_pipeline->raw_writer, "player_raw");
    audio_pipeline_register(player_pipeline->audio_pipeline, player_pipeline->audio_decoder, "player_dec");

#if (defined ENBALE_AUDIO_STREAM_DUAL_MIC)
    ESP_LOGI(TAG, "ENBALE_AUDIO_STREAM_DUAL_MIC");
#if CONFIG_AUDIO_SUPPORT_G711A_DECODER
    player_pipeline->player_rsp = create_8k_ch1_to_16k_ch2_rsp_stream();
#else
    player_pipeline->player_rsp = create_ch1_to_ch2_rsp_stream();   
#endif
#elif (defined ENABLE_AUDIO_STREAM_SINGLE_MIC)
    ESP_LOGI(TAG, "ENBALE_AUDIO_STREAM_DUAL_MIC");
#if CONFIG_AUDIO_SUPPORT_G711A_DECODER
    player_pipeline->player_rsp = create_8k_ch1_to_16k_ch2_rsp_stream();
#else
    player_pipeline->player_rsp = create_ch1_to_ch2_rsp_stream();   
#endif 
#else
    ESP_LOGE(TAG, "Invalid record device"); 
#endif // USE_ES8311_AS_RECORD_DEVICE
    AUDIO_MEM_CHECK(TAG, player_pipeline->player_rsp, goto _exit_open);

    ESP_LOGI(TAG, "Link playback element together raw-->audio_decoder-->rsp-->i2s_stream-->[codec_chip]");

    audio_element_set_write_cb(player_pipeline->player_rsp, _player_write_nop_cb, NULL);
    audio_pipeline_register(player_pipeline->audio_pipeline, player_pipeline->player_rsp, "player_rsp");
    const char *link_tag[3] = {"player_raw", "player_dec", "player_rsp"};
    audio_pipeline_link(player_pipeline->audio_pipeline, &link_tag[0], 3);

    audio_pipeline_run(player_pipeline->audio_pipeline);
    return player_pipeline;

_exit_open:
    audio_pipe_safe_free(player_pipeline->player_rsp, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->raw_writer, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->audio_decoder, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->i2s_stream_writer, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->audio_pipeline, audio_pipeline_deinit);
    audio_pipe_safe_free(player_pipeline, audio_free);
    return NULL;
}

esp_err_t player_pipeline_run(player_pipeline_handle_t player_pipeline)
{
    ESP_RETURN_ON_FALSE(player_pipeline != NULL, ESP_FAIL, TAG, "player pipeline not initialized");
    if (player_pipeline->player_state == PIPE_STATE_RUNNING) {
        ESP_LOGW(TAG, "player pipe is already running state");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "player pipe start running");
    // 重置播放进度
    player_pipeline->played_bytes = 0;
    // 尝试获取音频格式信息
    audio_element_getinfo(player_pipeline->audio_decoder, &player_pipeline->audio_info);
    audio_element_set_write_cb(player_pipeline->player_rsp, _player_i2s_write_cb, NULL);
    player_pipeline->player_state = PIPE_STATE_RUNNING;
    return ESP_OK;
};

esp_err_t player_pipeline_stop(player_pipeline_handle_t player_pipeline)
{
    ESP_RETURN_ON_FALSE(player_pipeline != NULL, ESP_FAIL, TAG, "player pipeline not initialized");
    if (player_pipeline->player_state == PIPE_STATE_IDLE) {
        ESP_LOGW(TAG, "player pipe is idle state");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "player pipe stop running");
    audio_element_set_write_cb(player_pipeline->player_rsp, _player_write_nop_cb, NULL);
    player_pipeline->player_state = PIPE_STATE_IDLE;
    return ESP_OK;
}

esp_err_t player_pipeline_get_state(player_pipeline_handle_t player_pipeline, pipe_player_state_e *state)
{
    ESP_RETURN_ON_FALSE(player_pipeline != NULL, ESP_FAIL, TAG, "player pipeline not initialized");
    *state = player_pipeline->player_state;
    return ESP_OK;
}

esp_err_t player_pipeline_get_progress(uint32_t *played_ms)
{
    if (played_ms == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 失败时设置为 0
    *played_ms = 0;
    
    // 优先使用 audio_tone (s_audio_player)
    if (s_audio_player != NULL) {
        // 使用基于时间的估算（因为无法安全地拦截 I2S 写入）
        if (s_audio_player->start_time_us > 0 && s_audio_player->player_state == PIPE_STATE_RUNNING) {
            int64_t elapsed_us = esp_timer_get_time() - s_audio_player->start_time_us;
            *played_ms = (uint32_t)(elapsed_us / 1000);
        }
        return ESP_OK;
    }
    
    // 其次使用 player_pipeline
    if (s_player_pipeline != NULL) {
        // 计算播放时间（毫秒）
        if (s_player_pipeline->audio_info.sample_rates > 0) {
            int bytes_per_sample = (s_player_pipeline->audio_info.bits / 8);
            int channels = s_player_pipeline->audio_info.channels;
            int bytes_per_second = s_player_pipeline->audio_info.sample_rates * bytes_per_sample * channels;
            if (bytes_per_second > 0) {
                *played_ms = (uint32_t)((s_player_pipeline->played_bytes * 1000ULL) / bytes_per_second);
            }
        }
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "player pipeline not initialized");
    return ESP_FAIL;
}

esp_err_t player_pipeline_close(player_pipeline_handle_t player_pipeline)
{
    ESP_RETURN_ON_FALSE(player_pipeline != NULL, ESP_FAIL, TAG, "player pipeline not initialized");
    if (player_pipeline->player_state == PIPE_STATE_RUNNING) {
        ESP_LOGW(TAG, "Please stop player pipe first");
        return ESP_FAIL;
    }
    audio_pipeline_terminate(player_pipeline->audio_pipeline);

    audio_pipeline_unregister(player_pipeline->audio_pipeline, player_pipeline->raw_writer);
    audio_pipeline_unregister(player_pipeline->audio_pipeline, player_pipeline->i2s_stream_writer);
    audio_pipeline_unregister(player_pipeline->audio_pipeline, player_pipeline->audio_decoder);
    audio_pipeline_unregister(player_pipeline->audio_pipeline, player_pipeline->player_rsp);

    /* Release all resources */
    audio_pipe_safe_free(player_pipeline->player_rsp, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->raw_writer, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->i2s_stream_writer, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->audio_decoder, audio_element_deinit);
    audio_pipe_safe_free(player_pipeline->audio_pipeline, audio_pipeline_deinit);
    audio_pipe_safe_free(player_pipeline, audio_free);
    return ESP_OK;
};

// Just create a placeholder function
int player_pipeline_get_default_read_size(player_pipeline_handle_t player_pipeline)
{
    return 0;
};

audio_element_handle_t player_pipeline_get_raw_write(player_pipeline_handle_t player_pipeline)
{
    return player_pipeline->raw_writer; 
}

static int input_cb_for_afe(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
#if CONFIG_ENABLE_RECORDER_DEBUG
    aec_debug_data_write((char *)buffer, buf_sz);
#endif // ENABLE_AEC_DEBUG
    return raw_stream_read(s_recorder_pipeline->raw_reader, (char *)buffer, buf_sz);
}

void * audio_record_engine_init(recorder_pipeline_handle_t pipeline, rec_event_cb_t cb)
{
    recorder_sr_cfg_t recorder_sr_cfg = get_default_audio_record_config();
#if defined (CONFIG_CONTINUOUS_CONVERSATION_MODE)
    recorder_sr_cfg.afe_cfg->wakenet_init = false;
    recorder_sr_cfg.afe_cfg->se_init = false;
    recorder_sr_cfg.afe_cfg->vad_init = false;
#endif // CONFIG_CONTINUOUS_CONVERSATION_MODE

    recorder_encoder_cfg_t recorder_encoder_cfg = { 0 };

#if defined (CONFIG_AUDIO_SUPPORT_G711A_DECODER)
    rsp_filter_cfg_t filter_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    filter_cfg.src_ch = 1;
    filter_cfg.src_rate = 16000;
    filter_cfg.dest_ch = 1;
    filter_cfg.dest_rate = 8000;
    filter_cfg.complexity = 0;
    filter_cfg.stack_in_ext = true;
    filter_cfg.max_indata_bytes = 1024;
    recorder_encoder_cfg.resample = rsp_filter_init(&filter_cfg);
#endif // CONFIG_AUDIO_SUPPORT_G711A_DECODER

    recorder_encoder_cfg.encoder = create_record_encoder_stream();

    audio_rec_cfg_t cfg = AUDIO_RECORDER_DEFAULT_CFG();
    cfg.read = (recorder_data_read_t)&input_cb_for_afe;
    cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &cfg.sr_iface);
    cfg.event_cb = cb;
    cfg.vad_off = 1000;
    cfg.wakeup_end = 120000;

#if defined (CONFIG_CONTINUOUS_CONVERSATION_MODE)
    cfg.vad_start = 0;
#endif // CONFIG_CONTINUOUS_CONVERSATION_MODE
    cfg.encoder_handle = recorder_encoder_create(&recorder_encoder_cfg, &cfg.encoder_iface);
    pipeline->recorder_engine = audio_recorder_create(&cfg);
#if defined (CONFIG_CONTINUOUS_CONVERSATION_MODE)
    vTaskDelay(pdMS_TO_TICKS(200));
    audio_recorder_trigger_start(pipeline->recorder_engine);
#endif // CONFIG_CONTINUOUS_CONVERSATION_MODE
    return pipeline->recorder_engine;
}

static void audio_player_state_task(void *arg)
{
    audio_event_iface_handle_t evt = (audio_event_iface_handle_t) arg;

    s_audio_player->running = true;
    while (s_audio_player->running) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
            && msg.source == (void *) s_audio_player->audio_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(s_audio_player->audio_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from wav decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            // 保存音频格式信息
            s_audio_player->audio_info = music_info;

            // 设置I2S流的采样率、位数和声道数，使其与解码器输出匹配
            audio_element_setinfo(s_audio_player->i2s_stream_writer, &music_info);
            i2s_stream_set_clk(s_audio_player->i2s_stream_writer, 
                               music_info.sample_rates, 
                               music_info.bits, 
                               music_info.channels);
            continue;
        }
        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) s_audio_player->i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGI(TAG, "[ * ] Stop event received");
            // 注意：这里不要调用 audio_tone_stop()，因为会设置 is_switching 标志
            // 直接更新状态即可
            s_audio_player->player_state = PIPE_STATE_IDLE;
            s_audio_player->is_switching = false;  // 清除切换标志
            s_audio_player->tone_cb(AEL_STATUS_STATE_FINISHED);
        }
    }
}

esp_err_t audio_tone_init(tone_play_callback_t callback)
{
    s_audio_player = (audio_player_t *)audio_calloc(1, sizeof(audio_player_t));
    AUDIO_MEM_CHECK(TAG, s_audio_player, goto _exit_open);
    s_audio_player->is_switching = false;  // 初始化切换标志
    s_audio_player->start_time_us = 0;
    memset(&s_audio_player->audio_info, 0, sizeof(audio_element_info_t));

    ESP_LOGI(TAG, "Create audio pipeline for audio player");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_audio_player->pipeline = audio_pipeline_init(&pipeline_cfg);
    AUDIO_MEM_CHECK(TAG, s_audio_player->pipeline, goto _exit_open);

    ESP_LOGI(TAG, "Create audio player audio stream");
    s_audio_player->fatfs_stream = create_audio_player_fatfs_stream();
    AUDIO_MEM_CHECK(TAG, s_audio_player->fatfs_stream, goto _exit_open);
    s_audio_player->i2s_stream_writer = create_player_i2s_stream(true);
    AUDIO_MEM_CHECK(TAG, s_audio_player->i2s_stream_writer, goto _exit_open);
    s_audio_player->audio_decoder = create_player_wav_decoder_stream();
    AUDIO_MEM_CHECK(TAG, s_audio_player->audio_decoder, goto _exit_open);

    ESP_LOGI(TAG, "Register all elements to playback pipeline");
    audio_pipeline_register(s_audio_player->pipeline, s_audio_player->fatfs_stream, "audio_player_fatfs");
    audio_pipeline_register(s_audio_player->pipeline, s_audio_player->audio_decoder, "audio_player_dec");
    audio_pipeline_register(s_audio_player->pipeline, s_audio_player->i2s_stream_writer, "audio_player_i2s");

    ESP_LOGI(TAG, "Link playback element together fatfs-->audio_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"audio_player_fatfs", "audio_player_dec", "audio_player_i2s"};
    audio_pipeline_link(s_audio_player->pipeline, &link_tag[0], 3);

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    s_audio_player->evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(s_audio_player->pipeline, s_audio_player->evt);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), s_audio_player->evt);

    s_audio_player->tone_cb = callback;
    audio_thread_create(NULL, "audio_player_state_task", audio_player_state_task, (void *)s_audio_player->evt, 5 * 1024, 15, true, 1);

    return ESP_OK;

_exit_open:
    audio_pipe_safe_free(s_audio_player->fatfs_stream, audio_element_deinit);
    audio_pipe_safe_free(s_audio_player->i2s_stream_writer, audio_element_deinit);
    audio_pipe_safe_free(s_audio_player->audio_decoder, audio_element_deinit);
    audio_pipe_safe_free(s_audio_player->pipeline, audio_pipeline_deinit);
    audio_pipe_safe_free(s_audio_player, audio_free);
    return ESP_FAIL;
}

esp_err_t audio_tone_play(const char *uri)
{
    ESP_RETURN_ON_FALSE(s_audio_player != NULL, ESP_FAIL, TAG, "audio tone not initialized");
    
    // 防止重复调用：如果正在切换，等待一小段时间后重试
    if (s_audio_player->is_switching) {
        ESP_LOGW(TAG, "Audio is switching, wait a bit and retry...");
        // 等待最多100ms，每次10ms
        int retry_count = 0;
        const int max_retry = 10;
        while (s_audio_player->is_switching && retry_count < max_retry) {
            vTaskDelay(pdMS_TO_TICKS(10));
            retry_count++;
        }
        // 如果还是切换中，强制清除标志（可能是卡住了）
        if (s_audio_player->is_switching) {
            ESP_LOGW(TAG, "Audio switching timeout, force clear flag");
            s_audio_player->is_switching = false;
        }
    }
    
    // 设置切换标志
    s_audio_player->is_switching = true;
    
    // 无论状态如何，都先停止并强制终止管道，确保 ADF 内部状态机彻底回归初始态
    audio_pipeline_stop(s_audio_player->pipeline);
    audio_pipeline_wait_for_stop(s_audio_player->pipeline);
    audio_pipeline_terminate(s_audio_player->pipeline);
    
    // 强制清除事件队列中所有残留消息，必须在 terminate 之后执行
    audio_event_iface_msg_t msg;
    while (audio_event_iface_listen(s_audio_player->evt, &msg, 0) == ESP_OK) {
        // 持续读取直到队列为空
    }

    // 重置管道
    audio_pipeline_reset_ringbuffer(s_audio_player->pipeline);
    audio_pipeline_reset_elements(s_audio_player->pipeline);
    s_audio_player->player_state = PIPE_STATE_IDLE;
    ESP_LOGI(TAG, "Audio pipeline fully reset to IDLE");
    
    ESP_LOGI(TAG, "audio_tone_play: %s", uri);
    
    // 重置播放进度
    memset(&s_audio_player->audio_info, 0, sizeof(audio_element_info_t));
    s_audio_player->start_time_us = 0;  // 在 pipeline 真正启动后设置
    
    // 设置新的URI
    audio_element_set_uri(s_audio_player->fatfs_stream, uri);
    
    // 等待一下，确保所有操作完成
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // 启动新的音频
    esp_err_t ret = audio_pipeline_run(s_audio_player->pipeline);
    if (ret == ESP_OK) {
        s_audio_player->player_state = PIPE_STATE_RUNNING;
        s_audio_player->start_time_us = esp_timer_get_time();  // 记录开始时间
        ESP_LOGI(TAG, "Audio pipeline started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to run audio pipeline: %s", esp_err_to_name(ret));
        s_audio_player->player_state = PIPE_STATE_IDLE;
    }
    
    // 给系统一点时间处理初始事件
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 清除切换标志
    s_audio_player->is_switching = false;
    
    return ret;
}

esp_err_t audio_tone_stop(void)
{
    ESP_RETURN_ON_FALSE(s_audio_player != NULL, ESP_FAIL, TAG, "audio tone not initialized");
    if (s_audio_player->player_state == PIPE_STATE_IDLE) {
        return ESP_OK;  // 已经是IDLE状态，直接返回成功
    }
    
    // 设置切换标志，防止在停止过程中被新的播放请求打断
    s_audio_player->is_switching = true;
    
    // 停止管道
    audio_pipeline_stop(s_audio_player->pipeline);
    
    // 非阻塞等待：最多等待100ms
    int wait_count = 0;
    const int max_wait = 10;  // 最多等待10次，每次10ms
    while (s_audio_player->player_state == PIPE_STATE_RUNNING && wait_count < max_wait) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;
    }
    
    // 如果还没停止，强制终止
    if (s_audio_player->player_state == PIPE_STATE_RUNNING) {
        ESP_LOGW(TAG, "Force terminate audio pipeline");
        audio_pipeline_terminate(s_audio_player->pipeline);
    }
    
    // 重置管道
    audio_pipeline_reset_ringbuffer(s_audio_player->pipeline);
    audio_pipeline_reset_elements(s_audio_player->pipeline);
    s_audio_player->player_state = PIPE_STATE_IDLE;
    
    // 清除切换标志
    s_audio_player->is_switching = false;
    
    return ESP_OK;
}

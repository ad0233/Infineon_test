/*******************************************************************************
 * Simplified Radar Presence Detection Implementation
 * 
 * This is a simplified implementation of xensiv-radar-presence using
 * sensor-dsp and esp-dsp libraries. Compatible with RISC-V architecture.
 *******************************************************************************/

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "xensiv_radar_presence.h"
#include "ifx_sensor_dsp.h"
#include "esp_dsp.h"
#include "esp_log.h"

static const char *TAG = "radar_presence_impl";

// Internal context structure
typedef struct {
    xensiv_radar_presence_config_t config;
    xensiv_radar_presence_cb_t callback;
    void *callback_data;
    
    // State machine
    xensiv_radar_presence_state_t current_state;
    uint32_t macro_confirm_count;
    uint32_t absence_count;
    uint32_t last_macro_time;
    uint32_t last_micro_time;
    uint32_t last_compare_time;
    
    // Buffers
    float32_t *window;
    cfloat32_t *range_fft;
    float32_t *power_spectrum;
    float32_t *macro_history;
    float32_t *micro_history;
    
    // History for micro movement detection
    cfloat32_t *doppler_buffer;
    float32_t *doppler_power;
    uint32_t doppler_frame_count;
    
    // Memory allocation functions
    void* (*malloc_func)(size_t);
    void (*free_func)(void*);
} radar_presence_context_t;

static void* default_malloc(size_t size) {
    return malloc(size);
}

static void default_free(void* ptr) {
    free(ptr);
}

static void* (*g_malloc_func)(size_t) = default_malloc;
static void (*g_free_func)(void*) = default_free;

void xensiv_radar_presence_set_malloc_free(void* (*malloc_func)(size_t size),
                                           void (*free_func)(void* ptr)) {
    if (malloc_func) g_malloc_func = malloc_func;
    if (free_func) g_free_func = free_func;
}

static float32_t calculate_power(cfloat32_t *complex_data, int32_t index) {
    float32_t real = CREAL_F32(complex_data[index]);
    float32_t imag = CIMAG_F32(complex_data[index]);
    return sqrtf(real * real + imag * imag);
}

static float32_t find_max_power_in_range(float32_t *power, int32_t min_bin, int32_t max_bin, int32_t *max_index) {
    float32_t max_val = 0.0f;
    int32_t idx = min_bin;
    
    for (int32_t i = min_bin; i <= max_bin && i < 64; i++) {
        if (power[i] > max_val) {
            max_val = power[i];
            idx = i;
        }
    }
    
    if (max_index) *max_index = idx;
    return max_val;
}

static void trigger_event(radar_presence_context_t *ctx, 
                         xensiv_radar_presence_state_t state,
                         int32_t range_bin,
                         uint32_t timestamp) {
    if (ctx->callback && state != ctx->current_state) {
        xensiv_radar_presence_event_t event = {
            .timestamp = timestamp,
            .range_bin = range_bin,
            .state = state
        };
        ctx->callback((xensiv_radar_presence_handle_t)ctx, &event, ctx->callback_data);
    }
    ctx->current_state = state;
}

int32_t xensiv_radar_presence_alloc(xensiv_radar_presence_handle_t *handle,
                                    const xensiv_radar_presence_config_t *config) {
    if (!handle || !config) {
        return XENSIV_RADAR_PRESENCE_MEM_ERROR;
    }
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)g_malloc_func(sizeof(radar_presence_context_t));
    if (!ctx) {
        return XENSIV_RADAR_PRESENCE_MEM_ERROR;
    }
    
    memset(ctx, 0, sizeof(radar_presence_context_t));
    memcpy(&ctx->config, config, sizeof(xensiv_radar_presence_config_t));
    
    int32_t num_samples = config->num_samples_per_chirp;
    int32_t num_range_bins = num_samples / 2;
    
    // Allocate buffers
    ctx->window = (float32_t*)g_malloc_func(num_samples * sizeof(float32_t));
    ctx->range_fft = (cfloat32_t*)g_malloc_func(num_range_bins * sizeof(cfloat32_t));
    ctx->power_spectrum = (float32_t*)g_malloc_func(num_range_bins * sizeof(float32_t));
    ctx->macro_history = (float32_t*)g_malloc_func(num_range_bins * sizeof(float32_t));
    ctx->micro_history = (float32_t*)g_malloc_func(num_range_bins * sizeof(float32_t));
    
    // Allocate doppler buffer for micro movement
    if (config->micro_fft_size > 0) {
        ctx->doppler_buffer = (cfloat32_t*)g_malloc_func(config->micro_fft_size * num_range_bins * sizeof(cfloat32_t));
        ctx->doppler_power = (float32_t*)g_malloc_func(config->micro_fft_size * sizeof(float32_t));
    }
    
    if (!ctx->window || !ctx->range_fft || !ctx->power_spectrum || 
        !ctx->macro_history || !ctx->micro_history) {
        xensiv_radar_presence_free((xensiv_radar_presence_handle_t)ctx);
        return XENSIV_RADAR_PRESENCE_MEM_ERROR;
    }
    
    // Generate window
    ifx_window_hann_f32(ctx->window, num_samples);
    
    // Initialize state
    ctx->current_state = XENSIV_RADAR_PRESENCE_STATE_ABSENCE;
    ctx->macro_confirm_count = 0;
    ctx->absence_count = 0;
    ctx->doppler_frame_count = 0;
    
    // Initialize FFT
    if (dsps_fft2r_init_fc32(NULL, num_samples) != ESP_OK) {
        ESP_LOGE(TAG, "FFT initialization failed");
        xensiv_radar_presence_free((xensiv_radar_presence_handle_t)ctx);
        return XENSIV_RADAR_PRESENCE_FFT_LEN_ERROR;
    }
    
    *handle = (xensiv_radar_presence_handle_t)ctx;
    return XENSIV_RADAR_PRESENCE_OK;
}

void xensiv_radar_presence_free(xensiv_radar_presence_handle_t handle) {
    if (!handle) return;
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    
    if (ctx->window) g_free_func(ctx->window);
    if (ctx->range_fft) g_free_func(ctx->range_fft);
    if (ctx->power_spectrum) g_free_func(ctx->power_spectrum);
    if (ctx->macro_history) g_free_func(ctx->macro_history);
    if (ctx->micro_history) g_free_func(ctx->micro_history);
    if (ctx->doppler_buffer) g_free_func(ctx->doppler_buffer);
    if (ctx->doppler_power) g_free_func(ctx->doppler_power);
    
    g_free_func(ctx);
}

void xensiv_radar_presence_set_callback(xensiv_radar_presence_handle_t handle,
                                        xensiv_radar_presence_cb_t callback,
                                        void* data) {
    if (!handle) return;
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    ctx->callback = callback;
    ctx->callback_data = data;
}

int32_t xensiv_radar_presence_process_frame(xensiv_radar_presence_handle_t handle,
                                             float32_t* frame,
                                             XENSIV_RADAR_PRESENCE_TIMESTAMP time_ms) {
    if (!handle || !frame) {
        return XENSIV_RADAR_PRESENCE_MEM_ERROR;
    }
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    int32_t num_samples = ctx->config.num_samples_per_chirp;
    int32_t num_range_bins = num_samples / 2;
    
    // Apply window
    for (int32_t i = 0; i < num_samples; i++) {
        frame[i] *= ctx->window[i];
    }
    
    // Perform Range FFT (real input -> complex output)
    ifx_range_fft_f32(frame, ctx->range_fft, true, NULL, num_samples, 1);
    
    // Calculate power spectrum
    for (int32_t i = 0; i < num_range_bins; i++) {
        ctx->power_spectrum[i] = calculate_power(ctx->range_fft, i);
    }
    
    // Find max power in detection range
    int32_t max_range_bin;
    float32_t max_power = find_max_power_in_range(ctx->power_spectrum,
                                                  ctx->config.min_range_bin,
                                                  ctx->config.max_range_bin,
                                                  &max_range_bin);
    
    // State machine logic
    uint32_t time_since_compare = time_ms - ctx->last_compare_time;
    
    if (time_since_compare >= ctx->config.macro_compare_interval_ms) {
        ctx->last_compare_time = time_ms;
        
        // Macro movement detection
        bool macro_detected = (max_power > ctx->config.macro_threshold);
        
        if (macro_detected) {
            ctx->macro_confirm_count++;
            ctx->last_macro_time = time_ms;
            
            if (ctx->macro_confirm_count > ctx->config.macro_movement_confirmations) {
                if (ctx->current_state == XENSIV_RADAR_PRESENCE_STATE_ABSENCE) {
                    trigger_event(ctx, XENSIV_RADAR_PRESENCE_STATE_MACRO_PRESENCE, 
                                max_range_bin, time_ms);
                }
            }
        } else {
            ctx->macro_confirm_count = 0;
            
            // Check if macro movement timeout
            if (ctx->current_state == XENSIV_RADAR_PRESENCE_STATE_MACRO_PRESENCE) {
                uint32_t time_since_macro = time_ms - ctx->last_macro_time;
                if (time_since_macro > ctx->config.macro_movement_validity_ms) {
                    trigger_event(ctx, XENSIV_RADAR_PRESENCE_STATE_ABSENCE, 
                                0, time_ms);
                }
            }
        }
        
        // Micro movement detection (simplified)
        if (ctx->config.mode == XENSIV_RADAR_PRESENCE_MODE_MICRO_IF_MACRO ||
            ctx->config.mode == XENSIV_RADAR_PRESENCE_MODE_MICRO_AND_MACRO) {
            
            if (ctx->current_state == XENSIV_RADAR_PRESENCE_STATE_MACRO_PRESENCE ||
                ctx->current_state == XENSIV_RADAR_PRESENCE_STATE_MICRO_PRESENCE) {
                
                // Simple micro detection: check if power is below macro threshold but above micro
                bool micro_detected = (max_power < ctx->config.macro_threshold) &&
                                     (max_power > ctx->config.micro_threshold);
                
                if (micro_detected) {
                    ctx->last_micro_time = time_ms;
                    if (ctx->current_state == XENSIV_RADAR_PRESENCE_STATE_MACRO_PRESENCE) {
                        trigger_event(ctx, XENSIV_RADAR_PRESENCE_STATE_MICRO_PRESENCE,
                                    max_range_bin, time_ms);
                    }
                } else {
                    // Check micro timeout
                    uint32_t time_since_micro = time_ms - ctx->last_micro_time;
                    if (time_since_micro > ctx->config.micro_movement_validity_ms) {
                        if (ctx->current_state == XENSIV_RADAR_PRESENCE_STATE_MICRO_PRESENCE) {
                            trigger_event(ctx, XENSIV_RADAR_PRESENCE_STATE_ABSENCE,
                                        0, time_ms);
                        }
                    }
                }
            }
        }
    }
    
    return XENSIV_RADAR_PRESENCE_OK;
}

int32_t xensiv_radar_presence_get_config(xensiv_radar_presence_handle_t handle,
                                         xensiv_radar_presence_config_t* config) {
    if (!handle || !config) {
        return XENSIV_RADAR_PRESENCE_MEM_ERROR;
    }
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    memcpy(config, &ctx->config, sizeof(xensiv_radar_presence_config_t));
    return XENSIV_RADAR_PRESENCE_OK;
}

int32_t xensiv_radar_presence_set_config(xensiv_radar_presence_handle_t handle,
                                         const xensiv_radar_presence_config_t* config) {
    if (!handle || !config) {
        return XENSIV_RADAR_PRESENCE_MEM_ERROR;
    }
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    memcpy(&ctx->config, config, sizeof(xensiv_radar_presence_config_t));
    return XENSIV_RADAR_PRESENCE_OK;
}

void xensiv_radar_presence_reset(xensiv_radar_presence_handle_t handle) {
    if (!handle) return;
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    ctx->current_state = XENSIV_RADAR_PRESENCE_STATE_ABSENCE;
    ctx->macro_confirm_count = 0;
    ctx->absence_count = 0;
    ctx->doppler_frame_count = 0;
}

float32_t xensiv_radar_presence_get_bin_length(const xensiv_radar_presence_handle_t handle) {
    if (!handle) return 0.0f;
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    // Calculate bin length: c / (2 * bandwidth * num_samples_per_chirp)
    // Simplified: return approximate bin length based on bandwidth
    float32_t bin_length = IFX_LIGHT_SPEED_M_S / (2.0f * ctx->config.bandwidth * ctx->config.num_samples_per_chirp);
    return bin_length;
}

cfloat32_t* xensiv_radar_presence_get_macro_fft_buffer(const xensiv_radar_presence_handle_t handle) {
    if (!handle) return NULL;
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    return ctx->range_fft;
}

bool xensiv_radar_presence_get_max_macro(const xensiv_radar_presence_handle_t handle,
                                         float* macro, int* index) {
    if (!handle || !macro || !index) return false;
    
    radar_presence_context_t *ctx = (radar_presence_context_t*)handle;
    int32_t max_idx;
    float32_t max_val = find_max_power_in_range(ctx->power_spectrum,
                                                ctx->config.min_range_bin,
                                                ctx->config.max_range_bin,
                                                &max_idx);
    
    *macro = max_val;
    *index = max_idx;
    return true;
}

bool xensiv_radar_presence_get_max_micro(const xensiv_radar_presence_handle_t handle,
                                         float* micro, int* index) {
    // Simplified: return same as macro for now
    return xensiv_radar_presence_get_max_macro(handle, micro, index);
}


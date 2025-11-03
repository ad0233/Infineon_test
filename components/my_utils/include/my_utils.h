/**
 * @file my_utils.h
 * @brief Utility functions with PSRAM optimization for FreeRTOS
 */

#ifndef MY_UTILS_H
#define MY_UTILS_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a task with stack allocated in PSRAM
 *
 * @param pvTaskCode Task function
 * @param pcName Task name
 * @param usStackDepth Stack size in bytes
 * @param pvParameters Task parameters
 * @param uxPriority Task priority
 * @param pvCreatedTask Handle to the created task (can be NULL)
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t my_thread_create(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask);

/**
 * @brief Create a task pinned to specific core with stack allocated in PSRAM
 *
 * @param pvTaskCode Task function
 * @param pcName Task name
 * @param usStackDepth Stack size in bytes
 * @param pvParameters Task parameters
 * @param uxPriority Task priority
 * @param pvCreatedTask Handle to the created task (can be NULL)
 * @param xCoreID Core ID (0 or 1, or tskNO_AFFINITY)
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t my_thread_create_pinned(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask,
    BaseType_t xCoreID);

/**
 * @brief Create a queue with storage allocated in PSRAM
 *
 * @param uxQueueLength Maximum number of items in the queue
 * @param uxItemSize Size of each item in bytes
 * @return Queue handle on success, NULL on failure
 */
QueueHandle_t my_queue_create(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);

/**
 * @brief Create a stream buffer with storage allocated in PSRAM
 *
 * @param xBufferSizeBytes Size of the buffer in bytes
 * @param xTriggerLevelBytes Number of bytes that must be in buffer before read unblocks
 * @return Stream buffer handle on success, NULL on failure
 */
StreamBufferHandle_t my_stream_buffer_create(size_t xBufferSizeBytes, size_t xTriggerLevelBytes);

/**
 * @brief Create a message buffer with storage allocated in PSRAM
 *
 * @param xBufferSizeBytes Size of the buffer in bytes
 * @return Message buffer handle on success, NULL on failure
 */
MessageBufferHandle_t my_message_buffer_create(size_t xBufferSizeBytes);

#ifdef __cplusplus
}
#endif

#endif /* MY_UTILS_H */


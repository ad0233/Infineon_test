/**
 * @file my_utils.c
 * @brief Utility functions with PSRAM optimization for FreeRTOS
 */

#include "my_utils.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MY_UTILS";

esp_err_t my_thread_create(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask)
{
    if (pvTaskCode == NULL || usStackDepth == 0) {
        ESP_LOGE(TAG, "Invalid parameters for thread creation");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate stack in PSRAM
    StackType_t *pxStackBuffer = (StackType_t *)heap_caps_malloc(usStackDepth, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pxStackBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate stack in PSRAM for task %s", pcName ? pcName : "unnamed");
        return ESP_ERR_NO_MEM;
    }

    // Allocate task control block in internal memory for better performance
    StaticTask_t *pxTaskBuffer = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pxTaskBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate TCB for task %s", pcName ? pcName : "unnamed");
        heap_caps_free(pxStackBuffer);
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t handle = xTaskCreateStatic(
        pvTaskCode,
        pcName,
        usStackDepth / sizeof(StackType_t),
        pvParameters,
        uxPriority,
        pxStackBuffer,
        pxTaskBuffer);

    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create task %s", pcName ? pcName : "unnamed");
        heap_caps_free(pxStackBuffer);
        heap_caps_free(pxTaskBuffer);
        return ESP_FAIL;
    }

    if (pvCreatedTask != NULL) {
        *pvCreatedTask = handle;
    }

    ESP_LOGI(TAG, "Created task %s with %lu bytes stack in PSRAM", pcName ? pcName : "unnamed", usStackDepth);
    return ESP_OK;
}

esp_err_t my_thread_create_pinned(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask,
    BaseType_t xCoreID)
{
    if (pvTaskCode == NULL || usStackDepth == 0) {
        ESP_LOGE(TAG, "Invalid parameters for thread creation");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate stack in PSRAM
    StackType_t *pxStackBuffer = (StackType_t *)heap_caps_malloc(usStackDepth, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pxStackBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate stack in PSRAM for task %s", pcName ? pcName : "unnamed");
        return ESP_ERR_NO_MEM;
    }

    // Allocate task control block in internal memory for better performance
    StaticTask_t *pxTaskBuffer = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pxTaskBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate TCB for task %s", pcName ? pcName : "unnamed");
        heap_caps_free(pxStackBuffer);
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
        pvTaskCode,
        pcName,
        usStackDepth / sizeof(StackType_t),
        pvParameters,
        uxPriority,
        pxStackBuffer,
        pxTaskBuffer,
        xCoreID);

    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create task %s on core %d", pcName ? pcName : "unnamed", xCoreID);
        heap_caps_free(pxStackBuffer);
        heap_caps_free(pxTaskBuffer);
        return ESP_FAIL;
    }

    if (pvCreatedTask != NULL) {
        *pvCreatedTask = handle;
    }

    ESP_LOGI(TAG, "Created task %s with %lu bytes stack in PSRAM on core %d", 
             pcName ? pcName : "unnamed", usStackDepth, xCoreID);
    return ESP_OK;
}

QueueHandle_t my_queue_create(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
    if (uxQueueLength == 0 || uxItemSize == 0) {
        ESP_LOGE(TAG, "Invalid queue parameters");
        return NULL;
    }

    // Calculate required storage size
    size_t xQueueStorageSize = uxQueueLength * uxItemSize;
    
    // Allocate queue storage in PSRAM
    uint8_t *pucQueueStorage = (uint8_t *)heap_caps_malloc(xQueueStorageSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pucQueueStorage == NULL) {
        ESP_LOGE(TAG, "Failed to allocate queue storage in PSRAM");
        return NULL;
    }

    // Allocate queue structure in internal memory
    StaticQueue_t *pxQueueBuffer = (StaticQueue_t *)heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pxQueueBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate queue control block");
        heap_caps_free(pucQueueStorage);
        return NULL;
    }

    QueueHandle_t handle = xQueueCreateStatic(
        uxQueueLength,
        uxItemSize,
        pucQueueStorage,
        pxQueueBuffer);

    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        heap_caps_free(pucQueueStorage);
        heap_caps_free(pxQueueBuffer);
        return NULL;
    }

    ESP_LOGI(TAG, "Created queue with %u items of %u bytes in PSRAM", uxQueueLength, uxItemSize);
    return handle;
}

StreamBufferHandle_t my_stream_buffer_create(size_t xBufferSizeBytes, size_t xTriggerLevelBytes)
{
    if (xBufferSizeBytes == 0) {
        ESP_LOGE(TAG, "Invalid stream buffer size");
        return NULL;
    }

    // Allocate buffer storage in PSRAM
    uint8_t *pucStreamBufferStorageArea = (uint8_t *)heap_caps_malloc(xBufferSizeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pucStreamBufferStorageArea == NULL) {
        ESP_LOGE(TAG, "Failed to allocate stream buffer storage in PSRAM");
        return NULL;
    }

    // Allocate stream buffer structure in internal memory
    StaticStreamBuffer_t *pxStreamBufferStruct = (StaticStreamBuffer_t *)heap_caps_malloc(
        sizeof(StaticStreamBuffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pxStreamBufferStruct == NULL) {
        ESP_LOGE(TAG, "Failed to allocate stream buffer control block");
        heap_caps_free(pucStreamBufferStorageArea);
        return NULL;
    }

    StreamBufferHandle_t handle = xStreamBufferCreateStatic(
        xBufferSizeBytes,
        xTriggerLevelBytes,
        pucStreamBufferStorageArea,
        pxStreamBufferStruct);

    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create stream buffer");
        heap_caps_free(pucStreamBufferStorageArea);
        heap_caps_free(pxStreamBufferStruct);
        return NULL;
    }

    ESP_LOGI(TAG, "Created stream buffer with %u bytes in PSRAM", xBufferSizeBytes);
    return handle;
}

MessageBufferHandle_t my_message_buffer_create(size_t xBufferSizeBytes)
{
    if (xBufferSizeBytes == 0) {
        ESP_LOGE(TAG, "Invalid message buffer size");
        return NULL;
    }

    // Allocate buffer storage in PSRAM
    uint8_t *pucMessageBufferStorageArea = (uint8_t *)heap_caps_malloc(xBufferSizeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pucMessageBufferStorageArea == NULL) {
        ESP_LOGE(TAG, "Failed to allocate message buffer storage in PSRAM");
        return NULL;
    }

    // Allocate message buffer structure in internal memory
    StaticStreamBuffer_t *pxMessageBufferStruct = (StaticStreamBuffer_t *)heap_caps_malloc(
        sizeof(StaticStreamBuffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pxMessageBufferStruct == NULL) {
        ESP_LOGE(TAG, "Failed to allocate message buffer control block");
        heap_caps_free(pucMessageBufferStorageArea);
        return NULL;
    }

    MessageBufferHandle_t handle = xMessageBufferCreateStatic(
        xBufferSizeBytes,
        pucMessageBufferStorageArea,
        pxMessageBufferStruct);

    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create message buffer");
        heap_caps_free(pucMessageBufferStorageArea);
        heap_caps_free(pxMessageBufferStruct);
        return NULL;
    }

    ESP_LOGI(TAG, "Created message buffer with %u bytes in PSRAM", xBufferSizeBytes);
    return handle;
}


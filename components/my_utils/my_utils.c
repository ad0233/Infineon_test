/**
 * @file my_utils.c
 * @brief Utility functions with PSRAM optimization for FreeRTOS
 */

#include "my_utils.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "MY_UTILS";

// Task cleanup manager
typedef struct {
    StackType_t *stack;
    StaticTask_t *tcb;
} task_cleanup_t;

static QueueHandle_t cleanup_queue = NULL;
static SemaphoreHandle_t cleanup_mutex = NULL;

// Cleanup manager task
static void task_cleanup_manager(void *arg) {
    task_cleanup_t cleanup;
    while (1) {
        if (xQueueReceive(cleanup_queue, &cleanup, portMAX_DELAY)) {
            if (cleanup.stack != NULL) {
                heap_caps_free(cleanup.stack);
            }
            if (cleanup.tcb != NULL) {
                heap_caps_free(cleanup.tcb);
            }
            ESP_LOGD(TAG, "Cleaned up task resources");
        }
    }
}

// Ensure cleanup manager is initialized (singleton pattern)
static esp_err_t ensure_cleanup_manager(void) {
    if (cleanup_queue != NULL) {
        return ESP_OK;  // Already initialized
    }

    // Create mutex for thread-safe initialization
    if (cleanup_mutex == NULL) {
        cleanup_mutex = xSemaphoreCreateMutex();
        if (cleanup_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(cleanup_mutex, portMAX_DELAY);

    // Double-check after acquiring lock
    if (cleanup_queue == NULL) {
        cleanup_queue = xQueueCreate(10, sizeof(task_cleanup_t));
        if (cleanup_queue == NULL) {
            xSemaphoreGive(cleanup_mutex);
            return ESP_ERR_NO_MEM;
        }

        BaseType_t ret = xTaskCreate(
            task_cleanup_manager,
            "task_cleanup",
            1024 * 4,
            NULL,
            2,
            NULL);

        if (ret != pdPASS) {
            vQueueDelete(cleanup_queue);
            cleanup_queue = NULL;
            xSemaphoreGive(cleanup_mutex);
            return ESP_ERR_NO_MEM;
        }

        ESP_LOGI(TAG, "Task cleanup manager initialized");
    }

    xSemaphoreGive(cleanup_mutex);
    return ESP_OK;
}

esp_err_t my_task_create_psram(
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

    // Save pointers to thread local storage for later cleanup
    vTaskSetThreadLocalStoragePointer(handle, 0, pxTaskBuffer);
    vTaskSetThreadLocalStoragePointer(handle, 1, pxStackBuffer);

    if (pvCreatedTask != NULL) {
        *pvCreatedTask = handle;
    }

    ESP_LOGI(TAG, "Created task %s with %u bytes stack in PSRAM", pcName ? pcName : "unnamed", usStackDepth);
    return ESP_OK;
}

esp_err_t my_task_create_pinned_psram(
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

    // Save pointers to thread local storage for later cleanup
    vTaskSetThreadLocalStoragePointer(handle, 0, pxTaskBuffer);
    vTaskSetThreadLocalStoragePointer(handle, 1, pxStackBuffer);

    if (pvCreatedTask != NULL) {
        *pvCreatedTask = handle;
    }

    ESP_LOGI(TAG, "Created task %s with %u bytes stack in PSRAM on core %d", 
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

esp_err_t my_thread_delete(TaskHandle_t xTaskToDelete)
{
    if (xTaskToDelete == NULL) {
        ESP_LOGE(TAG, "Invalid task handle for deletion");
        return ESP_ERR_INVALID_ARG;
    }

    // Ensure cleanup manager is running
    esp_err_t ret = ensure_cleanup_manager();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize cleanup manager");
        return ret;
    }

    // Get stored pointers from thread local storage
    StaticTask_t *pxTaskBuffer = (StaticTask_t *)pvTaskGetThreadLocalStoragePointer(xTaskToDelete, 0);
    StackType_t *pxStackBuffer = (StackType_t *)pvTaskGetThreadLocalStoragePointer(xTaskToDelete, 1);

    // Delete the task first
    vTaskDelete(xTaskToDelete);

    // Queue cleanup resources
    task_cleanup_t cleanup = {
        .stack = pxStackBuffer,
        .tcb = pxTaskBuffer
    };

    if (xQueueSend(cleanup_queue, &cleanup, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "Failed to queue task cleanup, resources may leak");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Task deleted and queued for cleanup");
    return ESP_OK;
}

uint32_t get_utc_timestamp_s(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        return (uint32_t)tv.tv_sec;
    }
    // 如果获取失败，返回0
    return 0;
}


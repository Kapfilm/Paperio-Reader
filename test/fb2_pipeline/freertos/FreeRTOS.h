#pragma once
#include <cstddef>
#include <cstdint>
using StackType_t = uint32_t;
using TaskHandle_t = void*;
using SemaphoreHandle_t = void*;
using StreamBufferHandle_t = void*;
constexpr int pdPASS=1;
constexpr int portMAX_DELAY=0;
#define pdMS_TO_TICKS(x) (x)
inline void vTaskDelay(int) {}
inline void vTaskDelete(void*) {}
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 4096; }
inline int xTaskCreate(void (*)(void*), const char*, int, void*, int, TaskHandle_t*) { return 0; }
inline void* xSemaphoreCreateBinary() { return nullptr; }
inline int xSemaphoreGive(void*) { return 0; }
inline int xSemaphoreTake(void*, int) { return 0; }
inline void vSemaphoreDelete(void*) {}
inline void* xStreamBufferCreate(size_t,size_t) { return nullptr; }
inline size_t xStreamBufferReceive(void*,void*,size_t,int) { return 0; }
inline size_t xStreamBufferSend(void*,const void*,size_t,int) { return 0; }
inline size_t xStreamBufferBytesAvailable(void*) { return 0; }
inline void vStreamBufferDelete(void*) {}

/*
 * rtosutil.c – ESP-IDF 5.5.3 GPTimer version
 */

#include <stdio.h>
#include <string.h>

#include "jsinteractive.h"
#include "jstimer.h"
#include "rtosutil_idf5.h"

#include "driver/gptimer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* --------------------------------------------------------------------------
 * QUEUE SUPPORT
 * -------------------------------------------------------------------------- */

void queues_init(){
  for(int i = 0; i < queueMax; i++){
    RTOSqueues[i].name = NULL;
    RTOSqueues[i].handle = NULL;
  }
}

int queue_indexByName(char *queueName){
  for(int i = 0; i < queueMax; i++){
    if(RTOSqueues[i].handle &&
       strcmp(queueName,RTOSqueues[i].name) == 0){
      return i;
    }
  }
  return -1;
}

QueueHandle_t queue_handleByName(char *queueName){
  int idx = queue_indexByName(queueName);
  if(idx < 0) return NULL;
  return RTOSqueues[idx].handle;
}

int queue_init(char *queueName,int length,int sizeOfEntry){
  for(int i = 0; i < queueMax; i++){
    if(RTOSqueues[i].handle == NULL){
      RTOSqueues[i].name = queueName;
      RTOSqueues[i].handle = xQueueCreate(length,sizeOfEntry);
      return i;
    }
  }
  return -1;
}

int queue_read(int idx){
  char data;
  if(xQueueReceive(RTOSqueues[idx].handle,&data,0)){
    return data;
  }
  return -1;
}

void queue_writeChar(int idx,char c){
  if(!xQueueSend(RTOSqueues[idx].handle,&c,1)){
    printf("SerialTaskOverflow\n");
  }
}

/* --------------------------------------------------------------------------
 * TASK SUPPORT
 * -------------------------------------------------------------------------- */

void tasks_init(){
  for(int i = 0; i < taskMax; i++){
    RTOStasks[i].name = NULL;
    RTOStasks[i].handle = NULL;
  }
}

int task_indexByName(char *taskName){
  for(int i = 0; i < taskMax; i++){
    if(RTOStasks[i].handle &&
       strcmp(taskName,RTOStasks[i].name) == 0){
      return i;
    }
  }
  return -1;
}

TaskHandle_t task_handleByName(char *taskName){
  int idx = task_indexByName(taskName);
  if(idx < 0) return NULL;
  return RTOStasks[idx].handle;
}

int task_init(TaskFunction_t taskCode,
              char *taskName,
              unsigned short stackDepth,
              UBaseType_t priority,
              BaseType_t coreId){

  for(int i = 0; i < taskMax; i++){
    if(RTOStasks[i].handle == NULL){
      RTOStasks[i].name = taskName;
      xTaskCreatePinnedToCore(taskCode,
                              taskName,
                              stackDepth,
                              NULL,
                              priority,
                              &RTOStasks[i].handle,
                              coreId);
      return i;
    }
  }
  return -1;
}

void task_notify(int idx){
  xTaskNotifyGive(RTOStasks[idx].handle);
}

void taskWaitNotify(){
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
}

/* --------------------------------------------------------------------------
 * GPTIMER IMPLEMENTATION (IDF 5.5.3)
 * -------------------------------------------------------------------------- */

typedef struct {
  char *name;
  gptimer_handle_t handle;
  int taskToNotifyIdx;
} ESP32TimerEntry;

ESP32Timer ESP32Timers[timerMax];

/* ---------------- ISR ---------------- */

static bool IRAM_ATTR espruino_gptimer_isr(
        gptimer_handle_t timer,
        const gptimer_alarm_event_data_t *edata,
        void *user_ctx)
{
  jstUtilTimerInterruptHandler();
  return false;
}

static bool IRAM_ATTR test_gptimer_isr(
        gptimer_handle_t timer,
        const gptimer_alarm_event_data_t *edata,
        void *user_ctx)
{
  printf("x\n");
  return false;
}

/* ---------------- Init ---------------- */

void timers_Init(){
  for(int i = 0; i < timerMax; i++){
    ESP32Timers[i].name = NULL;
    ESP32Timers[i].handle = NULL;
  }
}

int timer_Init(char *timerName,int group,int index,int isr_idx){

  for(int i = 0; i < timerMax; i++){

    if(ESP32Timers[i].name == NULL){

      ESP32Timers[i].name = timerName;

      gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1 µs resolution
      };

      gptimer_new_timer(&config, &ESP32Timers[i].handle);

      gptimer_event_callbacks_t cbs = {0};

      if(isr_idx == 0){
        cbs.on_alarm = espruino_gptimer_isr;
      } else {
        cbs.on_alarm = test_gptimer_isr;
      }

      gptimer_register_event_callbacks(
          ESP32Timers[i].handle,
          &cbs,
          (void*) i);

      gptimer_enable(ESP32Timers[i].handle);

      return i;
    }
  }
  return -1;
}

/* ---------------- Start ---------------- */

void timer_Start(int idx,uint64_t duration){

  if (duration < 1) duration = 1;

  gptimer_stop(ESP32Timers[idx].handle);
  gptimer_set_raw_count(ESP32Timers[idx].handle, 0);

  gptimer_alarm_config_t alarm = {
    .alarm_count = duration,
    .reload_count = 0,
    .flags.auto_reload_on_alarm = true
  };

  gptimer_set_alarm_action(ESP32Timers[idx].handle, &alarm);
  gptimer_start(ESP32Timers[idx].handle);
}

/* ---------------- Reschedule ---------------- */

void timer_Reschedule(int idx,uint64_t duration){

  if (duration < 1) duration = 1;

  gptimer_stop(ESP32Timers[idx].handle);
  gptimer_set_raw_count(ESP32Timers[idx].handle, 0);

  gptimer_alarm_config_t alarm = {
    .alarm_count = duration,
    .reload_count = 0,
    .flags.auto_reload_on_alarm = true
  };

  gptimer_set_alarm_action(ESP32Timers[idx].handle, &alarm);
  gptimer_start(ESP32Timers[idx].handle);
}

/* ---------------- List ---------------- */

void timer_List(){
  for(int i = 0; i < timerMax; i++){
    if(ESP32Timers[i].name == NULL){
      printf("timer %d free\n",i);
    } else {
      printf("timer %s active\n",ESP32Timers[i].name);
    }
  }
}
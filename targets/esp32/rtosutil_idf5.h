/*
 * rtosutil.h – GPTimer based version for ESP-IDF 5.x
 */

#ifndef RTOSUTIL_H
#define RTOSUTIL_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gptimer.h"

/* --------------------------------------------------------------------------
 * QUEUES
 * -------------------------------------------------------------------------- */

#define queueMax 10

struct RTOSqueue {
  char *name;
  QueueHandle_t handle;
};

extern struct RTOSqueue RTOSqueues[queueMax];

void queues_init(void);
int queue_indexByName(char *queueName);
QueueHandle_t queue_handleByName(char *queueName);
int queue_init(char *queueName,int length,int sizeOfEntry);
int queue_read(int idx);
void queue_writeChar(int idx,char c);
void queue_list(void);

/* --------------------------------------------------------------------------
 * TASKS
 * -------------------------------------------------------------------------- */

#define taskMax 10

struct RTOStask {
  char *name;
  TaskHandle_t handle;
  int rx;
};

extern struct RTOStask RTOStasks[taskMax];

void tasks_init(void);
int task_indexByName(char *taskName);
TaskHandle_t task_handleByName(char *taskName);
int task_getCurrentIndex(void);
char *task_getCurrentName(void);

int task_init(TaskFunction_t taskCode,
              char *taskName,
              unsigned short stackDepth,
              UBaseType_t priority,
              BaseType_t coreId);

void task_list(void);
void task_Suspend(int idx);
void task_Resume(int idx);
void task_notify(int idx);
void taskWaitNotify(void);

/* --------------------------------------------------------------------------
 * GPTIMER (IDF 5.x)
 * -------------------------------------------------------------------------- */

#ifndef CONFIG_IDF_TARGET_ESP32C3
#define timerMax 2
#else
#define timerMax 1
#endif

typedef struct {
  char *name;
  gptimer_handle_t handle;
  uint64_t duration;
  int taskToNotifyIdx;
} ESP32Timer;

extern ESP32Timer ESP32Timers[timerMax];

void timers_Init(void);
int timer_indexByName(char *timerName);

/* group/index bleiben aus API-Kompatibilität,
   werden intern aber ignoriert */
int timer_Init(char *timerName,int group,int index,int isr_idx);

void timer_Start(int idx,uint64_t duration);
void timer_Reschedule(int idx,uint64_t duration);
void timer_List(void);

/* -------------------------------------------------------------------------- */

void console_readToQueue(void);

#endif /* RTOSUTIL_H */
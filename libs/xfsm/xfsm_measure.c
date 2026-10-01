/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "xfsm_measure.h"

#include "jsutils.h"

#include <limits.h>
#include <string.h>

/*
 * Stores measurements for XFSM test builds. The compiler and actor runtime
 * call the sampling functions at known points so tests can measure peak JsVar
 * use and C stack use. Normal firmware does not expose these measurements.
 */
typedef struct {
  /* Extra JsVar blocks used while createMachine() is running. */
  unsigned int construction_base_blocks;
  unsigned int construction_peak_blocks;
  unsigned int construction_end_blocks;
  unsigned int diagnostic_peak_blocks;
  /* Free C stack at the start and lowest measured point of an actor call. */
  size_t operation_base_free;
  size_t operation_min_free;
  /* Optional extra JsVar blocks used during an actor call. */
  unsigned int operation_base_blocks;
  unsigned int operation_peak_blocks;
  unsigned int operation_end_blocks;
  unsigned int maximum_operation_peak_blocks;
  /* Results for the last actor call and the highest result seen. */
  size_t last_stack_bytes;
  size_t maximum_stack_bytes;
  unsigned int last_operation;
  /* Sampling state and whether actor JsVar sampling was requested. */
  bool construction_active;
  bool operation_active;
  bool operation_memory;
} XfcMeasurements;

static XfcMeasurements xfcMeasurements;

static unsigned int xfcDelta(unsigned int value, unsigned int base) {
  return value >= base ? value - base : 0;
}

void xfcMeasureConstructionBegin(void) {
  xfcMeasurements.construction_base_blocks = jsvGetMemoryUsage();
  xfcMeasurements.construction_peak_blocks = 0;
  xfcMeasurements.construction_end_blocks = 0;
  xfcMeasurements.construction_active = true;
  xfcMeasureMemorySample();
}

void xfcMeasureMemorySample(void) {
  unsigned int delta;
  if (!xfcMeasurements.construction_active) return;
  delta = xfcDelta(jsvGetMemoryUsage(),
                   xfcMeasurements.construction_base_blocks);
  if (delta > xfcMeasurements.construction_peak_blocks)
    xfcMeasurements.construction_peak_blocks = delta;
}

void xfcMeasureConstructionEnd(bool failed) {
  xfcMeasureMemorySample();
  xfcMeasurements.construction_end_blocks =
      xfcDelta(jsvGetMemoryUsage(), xfcMeasurements.construction_base_blocks);
  if (failed)
    xfcMeasurements.diagnostic_peak_blocks =
        xfcMeasurements.construction_peak_blocks;
  xfcMeasurements.construction_active = false;
}

void xfcMeasureOperationBegin(unsigned int operation) {
  size_t free_stack = jsuGetFreeStack();
  xfcMeasurements.operation_base_free = free_stack;
  xfcMeasurements.operation_min_free = free_stack;
  xfcMeasurements.operation_base_blocks =
      xfcMeasurements.operation_memory ? jsvGetMemoryUsage() : 0;
  xfcMeasurements.operation_peak_blocks = 0;
  xfcMeasurements.operation_end_blocks = 0;
  xfcMeasurements.last_stack_bytes = 0;
  xfcMeasurements.last_operation = operation;
  /* Platforms without a free-stack reading simply disable operation samples. */
  xfcMeasurements.operation_active = free_stack != SIZE_MAX;
}

void xfcMeasureStackSample(void) {
  unsigned int used_blocks;
  size_t free_stack;
  if (!xfcMeasurements.operation_active) return;
  if (xfcMeasurements.operation_memory) {
    used_blocks = xfcDelta(jsvGetMemoryUsage(),
                          xfcMeasurements.operation_base_blocks);
    if (used_blocks > xfcMeasurements.operation_peak_blocks)
      xfcMeasurements.operation_peak_blocks = used_blocks;
  }
  free_stack = jsuGetFreeStack();
  if (free_stack < xfcMeasurements.operation_min_free)
    xfcMeasurements.operation_min_free = free_stack;
}

void xfcMeasureOperationEnd(void) {
  size_t used;
  if (!xfcMeasurements.operation_active) return;
  xfcMeasureStackSample();
  if (xfcMeasurements.operation_memory)
    xfcMeasurements.operation_end_blocks =
        xfcDelta(jsvGetMemoryUsage(), xfcMeasurements.operation_base_blocks);
  if (xfcMeasurements.operation_peak_blocks >
      xfcMeasurements.maximum_operation_peak_blocks)
    xfcMeasurements.maximum_operation_peak_blocks =
        xfcMeasurements.operation_peak_blocks;
  used = xfcMeasurements.operation_base_free >=
                 xfcMeasurements.operation_min_free
             ? xfcMeasurements.operation_base_free -
                   xfcMeasurements.operation_min_free
             : 0;
  xfcMeasurements.last_stack_bytes = used;
  if (used > xfcMeasurements.maximum_stack_bytes)
    xfcMeasurements.maximum_stack_bytes = used;
  xfcMeasurements.operation_active = false;
}

JsVar *xfcMeasureGet(bool reset, bool operation_memory) {
  JsVar *result;
  size_t free_stack;
  if (reset) {
    memset(&xfcMeasurements, 0, sizeof(xfcMeasurements));
    xfcMeasurements.operation_memory = operation_memory;
  }
  result = jsvNewObject();
  if (!result) return 0;
  free_stack = jsuGetFreeStack();
  jsvObjectSetIntChild(result, "constructionPeakBlocks",
                       (JsVarInt)xfcMeasurements.construction_peak_blocks);
  jsvObjectSetIntChild(result, "constructionEndBlocks",
                       (JsVarInt)xfcMeasurements.construction_end_blocks);
  jsvObjectSetIntChild(result, "diagnosticPeakBlocks",
                       (JsVarInt)xfcMeasurements.diagnostic_peak_blocks);
  jsvObjectSetIntChild(result, "operationPeakBlocks",
                       (JsVarInt)xfcMeasurements.operation_peak_blocks);
  jsvObjectSetIntChild(result, "operationEndBlocks",
                       (JsVarInt)xfcMeasurements.operation_end_blocks);
  jsvObjectSetIntChild(
      result, "maximumOperationPeakBlocks",
      (JsVarInt)xfcMeasurements.maximum_operation_peak_blocks);
  jsvObjectSetIntChild(result, "lastStackBytes",
                       (JsVarInt)xfcMeasurements.last_stack_bytes);
  jsvObjectSetIntChild(result, "maximumStackBytes",
                       (JsVarInt)xfcMeasurements.maximum_stack_bytes);
  jsvObjectSetIntChild(result, "lastOperation",
                       (JsVarInt)xfcMeasurements.last_operation);
  jsvObjectSetIntChild(result, "currentBlocks",
                       (JsVarInt)jsvGetMemoryUsage());
  jsvObjectSetIntChild(result, "blockSize", (JsVarInt)sizeof(JsVar));
  if (free_stack != SIZE_MAX)
    jsvObjectSetIntChild(result, "freeStackBytes", (JsVarInt)free_stack);
  return result;
}

int xfcMeasureMemoryUsage(void) { return (int)jsvGetMemoryUsage(); }

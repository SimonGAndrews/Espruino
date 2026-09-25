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

typedef struct {
  unsigned int construction_base_blocks;
  unsigned int construction_peak_blocks;
  unsigned int construction_end_blocks;
  unsigned int diagnostic_peak_blocks;
  size_t operation_base_free;
  size_t operation_min_free;
  size_t last_stack_bytes;
  size_t maximum_stack_bytes;
  unsigned int last_operation;
  bool construction_active;
  bool operation_active;
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
  xfcMeasurements.last_stack_bytes = 0;
  xfcMeasurements.last_operation = operation;
  xfcMeasurements.operation_active = free_stack != SIZE_MAX;
}

void xfcMeasureStackSample(void) {
  size_t free_stack;
  if (!xfcMeasurements.operation_active) return;
  free_stack = jsuGetFreeStack();
  if (free_stack < xfcMeasurements.operation_min_free)
    xfcMeasurements.operation_min_free = free_stack;
}

void xfcMeasureOperationEnd(void) {
  size_t used;
  if (!xfcMeasurements.operation_active) return;
  xfcMeasureStackSample();
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

JsVar *xfcMeasureGet(bool reset) {
  JsVar *result;
  size_t free_stack;
  if (reset) memset(&xfcMeasurements, 0, sizeof(xfcMeasurements));
  result = jsvNewObject();
  if (!result) return 0;
  free_stack = jsuGetFreeStack();
  jsvObjectSetIntChild(result, "constructionPeakBlocks",
                       (JsVarInt)xfcMeasurements.construction_peak_blocks);
  jsvObjectSetIntChild(result, "constructionEndBlocks",
                       (JsVarInt)xfcMeasurements.construction_end_blocks);
  jsvObjectSetIntChild(result, "diagnosticPeakBlocks",
                       (JsVarInt)xfcMeasurements.diagnostic_peak_blocks);
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

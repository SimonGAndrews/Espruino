/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef XFSM_MEASURE_H
#define XFSM_MEASURE_H

#include <stdbool.h>

#include "jsvar.h"

#ifdef XFC_MEASURE

void xfcMeasureConstructionBegin(void);
void xfcMeasureMemorySample(void);
void xfcMeasureConstructionEnd(bool failed);
void xfcMeasureOperationBegin(unsigned int operation);
void xfcMeasureStackSample(void);
void xfcMeasureOperationEnd(void);
JsVar *xfcMeasureGet(bool reset, bool operation_memory);
int xfcMeasureMemoryUsage(void);

#else

#define xfcMeasureConstructionBegin() ((void)0)
#define xfcMeasureMemorySample() ((void)0)
#define xfcMeasureConstructionEnd(failed) ((void)(failed))
#define xfcMeasureOperationBegin(operation) ((void)(operation))
#define xfcMeasureStackSample() ((void)0)
#define xfcMeasureOperationEnd() ((void)0)

#endif

#endif

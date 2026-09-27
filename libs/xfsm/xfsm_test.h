/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef XFSM_TEST_H
#define XFSM_TEST_H

#include <stdbool.h>

#include "jsvar.h"

typedef enum {
  XFC_TEST_FAULT_NONE = 0,
  XFC_TEST_FAULT_COMPILE_WORKSPACE,
  XFC_TEST_FAULT_COMPILE_ARENA,
  XFC_TEST_FAULT_CREATE_ACTOR,
  XFC_TEST_FAULT_START_CONTEXT,
  XFC_TEST_FAULT_START_EVENT,
  XFC_TEST_FAULT_SEND_EVENT,
  XFC_TEST_FAULT_ASSIGN_CONTEXT,
  XFC_TEST_FAULT_COMPLETION_EVENT,
  XFC_TEST_FAULT_NOTIFY_SNAPSHOT,
  XFC_TEST_FAULT_GET_SNAPSHOT,
  XFC_TEST_FAULT_STOP_EVENT,
  XFC_TEST_FAULT_SUBSCRIBE
} XfcTestFault;

#ifdef XFC_TEST
bool xfcTestTakeFault(XfcTestFault fault);
void xfcTestSetFault(JsVar *name);
#else
#define xfcTestTakeFault(fault) false
#endif

#endif

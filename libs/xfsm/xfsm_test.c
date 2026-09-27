/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "xfsm_test.h"

#include <stddef.h>

#include "jsparse.h"

typedef struct {
  const char *name;
  XfcTestFault fault;
} XfcTestFaultName;

static XfcTestFault xfcPendingFault;

bool xfcTestTakeFault(XfcTestFault fault) {
  if (xfcPendingFault != fault) return false;
  xfcPendingFault = XFC_TEST_FAULT_NONE;
  return true;
}

void xfcTestSetFault(JsVar *name) {
  static const XfcTestFaultName names[] = {
      {"compile.workspace", XFC_TEST_FAULT_COMPILE_WORKSPACE},
      {"compile.arena", XFC_TEST_FAULT_COMPILE_ARENA},
      {"createActor", XFC_TEST_FAULT_CREATE_ACTOR},
      {"start.context", XFC_TEST_FAULT_START_CONTEXT},
      {"start.event", XFC_TEST_FAULT_START_EVENT},
      {"send.event", XFC_TEST_FAULT_SEND_EVENT},
      {"assign.context", XFC_TEST_FAULT_ASSIGN_CONTEXT},
      {"completion.event", XFC_TEST_FAULT_COMPLETION_EVENT},
      {"notify.snapshot", XFC_TEST_FAULT_NOTIFY_SNAPSHOT},
      {"getSnapshot", XFC_TEST_FAULT_GET_SNAPSHOT},
      {"stop.event", XFC_TEST_FAULT_STOP_EVENT},
      {"subscribe", XFC_TEST_FAULT_SUBSCRIBE}};
  size_t index;
  xfcPendingFault = XFC_TEST_FAULT_NONE;
  if (!jsvIsString(name)) {
    jsExceptionHere(JSET_TYPEERROR, "XFC E_CONFIG_TYPE @ _failNext.point");
    return;
  }
  for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
    if (jsvIsStringEqual(name, names[index].name)) {
      xfcPendingFault = names[index].fault;
      return;
    }
  }
  jsExceptionHere(JSET_TYPEERROR, "XFC E_CONFIG_TYPE @ _failNext.point");
}

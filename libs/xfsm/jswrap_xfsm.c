/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * JavaScript interface for the XFSM native state-machine engine.
 *
 * Espruino's wrapper generator reads the JSON declarations in this file. Each
 * function passes the call directly to the XFSM compiler or actor runtime.
 * ----------------------------------------------------------------------------
 */

#include "jsparse.h"
#include "jswrap_xfsm.h"
#include "xfsm.h"
#include "xfsm_compile.h"
#include "xfsm_measure.h"
#include "xfsm_runtime.h"
#include "xfsm_test.h"

/*JSON{
  "type" : "library",
  "class" : "XFSM",
  "ifdef" : "USE_XFSM"
}
Native finite-state machine and statechart engine.
*/

/*JSON{
  "type" : "staticmethod",
  "class" : "XFSM",
  "name" : "createMachine",
  "generate" : "jswrap_xfsm_createMachine",
  "params" : [
    ["config", "JsVar", "Machine configuration"],
    ["options", "JsVar", "Action and guard implementations"]
  ],
  "return" : ["JsVar", "An opaque compiled machine"]
}
Compile a Profile 1 machine configuration.
*/
JsVar *jswrap_xfsm_createMachine(JsVar *config, JsVar *options) {
  return xfcCompileMachine(config, options);
}

/*JSON{
  "type" : "staticmethod",
  "class" : "XFSM",
  "name" : "createActor",
  "generate" : "jswrap_xfsm_createActor",
  "params" : [
    ["machine", "JsVar", "A compiled XFSM machine"],
    ["options", "JsVar", "Reserved; Profile 1 accepts only undefined"]
  ],
  "return" : ["JsVar", "A new actor"]
}
Create an actor for a compiled Profile 1 machine.
*/
JsVar *jswrap_xfsm_createActor(JsVar *machine, JsVar *options) {
  return xfcCreateActor(machine, options);
}

/*JSON{
  "type" : "staticmethod",
  "class" : "XFSM",
  "name" : "assign",
  "generate" : "jswrap_xfsm_assign",
  "params" : [
    ["assignment", "JsVar", "Assignment function or property map"]
  ],
  "return" : ["JsVar", "An opaque assignment descriptor"]
}
Create a Profile 1 context-assignment descriptor.
*/
JsVar *jswrap_xfsm_assign(JsVar *assignment) {
  return xfcCreateAssignmentDescriptor(assignment);
}

#ifdef XFC_MEASURE
/*JSON{
  "type" : "staticmethod",
  "class" : "XFSM",
  "name" : "_measure",
  "generate" : "jswrap_xfsm_measure",
  "ifdef" : "XFC_MEASURE",
  "params" : [
    ["reset", "bool", "Reset counters before returning them"],
    ["operationMemory", "bool", "Sample JsVar use at runtime checkpoints"]
  ],
  "return" : ["JsVar", "Build-only XFSM measurement counters"]
}
Return build-only resource measurements. This method is absent from normal
firmware and is not part of the XFSM API.
*/
JsVar *jswrap_xfsm_measure(bool reset, bool operation_memory) {
  return xfcMeasureGet(reset, operation_memory);
}

/*JSON{
  "type" : "staticmethod",
  "class" : "XFSM",
  "name" : "_memoryUsage",
  "generate" : "jswrap_xfsm_memoryUsage",
  "ifdef" : "XFC_MEASURE",
  "return" : ["int", "Current Espruino variable-block usage"]
}
Return the current variable-block usage without allocating a result object.
This method is absent from normal firmware and is not part of the XFSM API.
*/
int jswrap_xfsm_memoryUsage(void) { return xfcMeasureMemoryUsage(); }
#endif

#ifdef XFC_TEST
/*JSON{
  "type" : "staticmethod",
  "class" : "XFSM",
  "name" : "_failNext",
  "generate" : "jswrap_xfsm_failNext",
  "ifdef" : "XFC_TEST",
  "params" : [
    ["point", "JsVar", "Private one-shot allocation-failure point"]
  ]
}
Select one deterministic allocation failure for the private test build. This
method is absent from normal firmware and is not part of the XFSM API.
*/
void jswrap_xfsm_failNext(JsVar *point) { xfcTestSetFault(point); }
#endif

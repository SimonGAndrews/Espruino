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
 * JavaScript interface for the XFSM native state-machine engine
 * ----------------------------------------------------------------------------
 */

#include "jsparse.h"
#include "jswrap_xfsm.h"
#include "xfsm.h"

/*JSON{
  "type" : "library",
  "class" : "XFSM",
  "ifdef" : "USE_XFSM"
}
Native finite-state machine and statechart engine.
*/

static JsVar *jswrap_xfsm_notImplemented(void) {
  jsExceptionHere(JSET_ERROR, "XFSM: %s", xfsmGetImplementationStatus());
  return 0;
}

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
  (void)config;
  (void)options;
  return jswrap_xfsm_notImplemented();
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
  (void)machine;
  (void)options;
  return jswrap_xfsm_notImplemented();
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
  (void)assignment;
  return jswrap_xfsm_notImplemented();
}

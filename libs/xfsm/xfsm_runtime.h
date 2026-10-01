/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef XFSM_RUNTIME_H
#define XFSM_RUNTIME_H

#include <stdbool.h>

#include "jsvar.h"

/*
 * Create an actor with its own state and context for a compiled machine. Input
 * arguments remain owned by the caller; a result is returned locked.
 */
JsVar *xfcCreateActor(JsVar *machine, JsVar *options);

/*
 * Hidden type markers distinguish XFSM machines and assign descriptors from
 * ordinary JavaScript objects. Creating a marker may allocate a hidden token.
 */
bool xfcBrandMachine(JsVar *machine);
bool xfcIsMachine(JsVar *machine);
bool xfcBrandAssignment(JsVar *descriptor);
bool xfcIsAssignment(JsVar *descriptor);

#endif

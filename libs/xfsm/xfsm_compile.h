/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef XFSM_COMPILE_H
#define XFSM_COMPILE_H

#include "jsvar.h"

/*
 * Compile the XState configuration and options passed to createMachine(). The
 * returned XFSM machine owns its compiled data block and the JavaScript values
 * used by it. On failure no partial machine is returned and an XFSM error is
 * raised. Input arguments remain owned by the caller; a result is returned
 * locked in the normal Espruino C API style.
 */
JsVar *xfcCompileMachine(JsVar *config, JsVar *options);

/*
 * Wrap an assign function or property map for use in createMachine(). The
 * input remains owned by the caller. A result is returned locked.
 */
JsVar *xfcCreateAssignmentDescriptor(JsVar *assignment);

#endif

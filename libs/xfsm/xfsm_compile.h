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

JsVar *xfcCompileMachine(JsVar *config, JsVar *options);
JsVar *xfcCreateAssignmentDescriptor(JsVar *assignment);

#endif

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

JsVar *xfcCreateActor(JsVar *machine, JsVar *options);

bool xfcBrandMachine(JsVar *machine);
bool xfcIsMachine(JsVar *machine);

#endif

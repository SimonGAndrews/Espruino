/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * Iterators for Variables
 * ----------------------------------------------------------------------------
 */
#include "jsvariterator.h"
#include "jsparse.h"
#include "jsinteractive.h"

/**
 Iterate over the contents of the content of a variable, calling callback for each.
 Used in `.write` methods, E.toString/toUint8Array and others
 Contents may be:
 * numeric -> output
 * a string -> output each character
 * array/arraybuffer -> call itself on each element
 * {data:..., count:...} -> call itself object.count times, on object.data
 * {callback:...} -> call the given function, call itself on return value
 */
bool jsvIterateCallback(
    JsVar *data,
    jsvIterateCallbackFn callback,
    void *callbackData
  ) {
  bool ok = true;
  // Handle the data being a single numeric.
  if (jsvIsNumeric(data)) {
    callback((int)jsvGetInteger(data), callbackData);
  }
  // Handle the data being an object.
  else if (jsvIsObject(data)) {
    JsVar *callbackVar = jsvObjectGetChildIfExists(data, "callback");
    if (jsvIsFunction(callbackVar)) {
      JsVar *result = jspExecuteFunction(callbackVar,0,0,NULL);
      jsvUnLock(callbackVar);
      if (result) {
        bool r = jsvIterateCallback(result, callback, callbackData);
        jsvUnLock(result);
        return r;
      }
      return true;
    }
    jsvUnLock(callbackVar);
    JsVar *countVar = jsvObjectGetChildIfExists(data, "count");
    JsVar *dataVar = jsvObjectGetChildIfExists(data, "data");
    if (countVar && dataVar && jsvIsNumeric(countVar)) {
      int n = (int)jsvGetInteger(countVar);
      while (ok && n-- > 0) {
        ok = jsvIterateCallback(dataVar, callback, callbackData);
      }
    } else {
      jsExceptionHere(JSET_TYPEERROR, "If specifying an object, it must be of the form {data : ..., count : N} or {callback : fn} - got %j", data);
      ok = false;
    }
    jsvUnLock2(countVar, dataVar);
  }
  // Handle the data being a string
  else if (jsvIsString(data)) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, data, 0);
    while (jsvStringIteratorHasChar(&it) && ok) {
      char ch = jsvStringIteratorGetCharAndNext(&it);
      callback(ch, callbackData);
    }
    jsvStringIteratorFree(&it);
  }
  // Handle the data being an array buffer
  else if (jsvIsArrayBuffer(data)) {
    JsvArrayBufferIterator it;
    jsvArrayBufferIteratorNew(&it, data, 0);
    if (JSV_ARRAYBUFFER_GET_SIZE(it.type) == 1 && !JSV_ARRAYBUFFER_IS_SIGNED(it.type)) {
      JsvStringIterator *sit = &it.it;
      // faster for single byte arrays - read using the string iterator.
      size_t len = jsvGetArrayBufferLength(data);
      while (len--) {
        callback((int)(unsigned char)jsvStringIteratorGetChar(sit), callbackData);
        jsvStringIteratorNextInline(sit);
      }
    } else {
      while (jsvArrayBufferIteratorHasElement(&it)) {
        callback((int)jsvArrayBufferIteratorGetIntegerValue(&it), callbackData);
        jsvArrayBufferIteratorNext(&it);
      }
    }
    jsvArrayBufferIteratorFree(&it);
  }
  // Handle the data being iterable
  else if (jsvIsIterable(data)) {
    JsvIterator it;
    jsvIteratorNew(&it, data, JSIF_EVERY_ARRAY_ELEMENT);
    while (jsvIteratorHasElement(&it) && ok) {
      JsVar *el = jsvIteratorGetValue(&it);
      ok = jsvIterateCallback(el, callback, callbackData);
      jsvUnLock(el);
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  } else {
    jsExceptionHere(JSET_TYPEERROR, "Expecting Number or iterable, got %t", data);
    ok = false;
  }
  return ok;
}

bool jsvIterateBufferCallback(
    JsVar *data,
    jsvIterateBufferCallbackFn callback,
    void *callbackData
  ) {
  bool ok = true;
  // Handle the data being a single numeric.
  if (jsvIsNumeric(data)) {
    unsigned char ch = (unsigned char)jsvGetInteger(data);
    callback(&ch, 1, callbackData);
  }
  // Handle the data being an object.
  else if (jsvIsObject(data)) {
    JsVar *callbackVar = jsvObjectGetChildIfExists(data, "callback");
    if (jsvIsFunction(callbackVar)) {
      JsVar *result = jspExecuteFunction(callbackVar,0,0,NULL);
      jsvUnLock(callbackVar);
      if (result) {
        bool r = jsvIterateBufferCallback(result, callback, callbackData);
        jsvUnLock(result);
        return r;
      }
      return true;
    }
    jsvUnLock(callbackVar);
    JsVar *countVar = jsvObjectGetChildIfExists(data, "count");
    JsVar *dataVar = jsvObjectGetChildIfExists(data, "data");
    if (countVar && dataVar && jsvIsNumeric(countVar)) {
      int n = (int)jsvGetInteger(countVar);
      while (ok && n-- > 0) {
        ok = jsvIterateBufferCallback(dataVar, callback, callbackData);
      }
    } else {
      jsExceptionHere(JSET_TYPEERROR, "If specifying an object, it must be of the form {data : ..., count : N} or {callback : fn} - got %j", data);
      ok = false;
    }
    jsvUnLock2(countVar, dataVar);
  }
  // Handle the data being a string
  else if (jsvIsString(data)) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, data, 0);
    while (jsvStringIteratorHasChar(&it) && ok) {
      unsigned char *data;
      unsigned int len;
      jsvStringIteratorGetPtrAndNext(&it,&data,&len);
      callback(data, len, callbackData);
    }
    jsvStringIteratorFree(&it);
  }
  // Handle the data being an array buffer
  else if (jsvIsArrayBuffer(data)) {
    JsvArrayBufferIterator it;
    jsvArrayBufferIteratorNew(&it, data, 0);
    if (JSV_ARRAYBUFFER_GET_SIZE(it.type) == 1 && !JSV_ARRAYBUFFER_IS_SIGNED(it.type)) {
      JsvStringIterator *sit = &it.it;
      // faster for single byte arrays - read using the string iterator.
      size_t len = jsvGetArrayBufferLength(data);
      while (len) {
        unsigned char *data;
        unsigned int dataLen;
        jsvStringIteratorGetPtrAndNext(sit,&data,&dataLen);
        if (dataLen>len) dataLen=(unsigned int)len;
        callback(data, dataLen, callbackData);
        len -= dataLen;
      }
    } else {
      while (jsvArrayBufferIteratorHasElement(&it)) {
        unsigned char ch = (unsigned char)jsvArrayBufferIteratorGetIntegerValue(&it);
        callback(&ch, 1, callbackData);
        jsvArrayBufferIteratorNext(&it);
      }
    }
    jsvArrayBufferIteratorFree(&it);
  }
  // Handle the data being iterable
  else if (jsvIsIterable(data)) {
    JsvIterator it;
    jsvIteratorNew(&it, data, JSIF_EVERY_ARRAY_ELEMENT);
    while (jsvIteratorHasElement(&it) && ok) {
      JsVar *el = jsvIteratorGetValue(&it);
      ok = jsvIterateBufferCallback(el, callback, callbackData);
      jsvUnLock(el);
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  } else {
    jsExceptionHere(JSET_TYPEERROR, "Expecting Number or iterable, got %t", data);
    ok = false;
  }
  return ok;
}

static void jsvIterateCallbackCountCb(
    unsigned char *data, unsigned int len,
    void *callbackData //!< A pointer to an int that counts how many times we were called.
  ) {
  NOT_USED(data);
  uint32_t *count = (uint32_t*)callbackData;
  (*count) += len;
}

/**
 * Determine how many items are in this variable that will be iterated over.
 * \return The number of iterations we will call for this variable.
 */
uint32_t jsvIterateCallbackCount(JsVar *var) {
  // Actually iterate over the variable where the callback function merely increments a counter
  // that is initially zero.  The result will be the number of times the callback for iteration
  // was invoked and hence the iteration count of the variable.
  uint32_t count = 0;
  jsvIterateBufferCallback(var, jsvIterateCallbackCountCb, (void *)&count);
  return count;
}


typedef struct { unsigned char *buf; unsigned int idx, length; } JsvIterateCallbackToBytesData;
static void jsvIterateCallbackToBytesCb(int data, void *userData) {
  JsvIterateCallbackToBytesData *cbData = (JsvIterateCallbackToBytesData*)userData;
  if (cbData->idx < cbData->length)
    cbData->buf[cbData->idx] = (unsigned char)data;
  cbData->idx++;
}
/** Write all data in array to the data pointer (of size dataSize bytes) */
unsigned int jsvIterateCallbackToBytes(JsVar *var, unsigned char *data, unsigned int dataSize) {
  // TODO: use jsvIterateBufferCallback and memcpy for a bit more speed
  JsvIterateCallbackToBytesData cbData;
  cbData.buf = (unsigned char *)data;
  cbData.idx = 0;
  cbData.length = dataSize;
  jsvIterateCallback(var, jsvIterateCallbackToBytesCb, (void*)&cbData);
  return cbData.idx;
}

// --------------------------------------------------------------------------------------------

// If charIdx doesn't fit in the current stringext, go forward along the string
static void jsvStringIteratorCatchUp(JsvStringIterator *it) {
  while (it->charIdx>0 && it->charIdx >= it->charsInVar) {
    jsvStringIteratorLoadInline(it);
  }
}

void jsvStringIteratorNew(JsvStringIterator *it, JsVar *str, size_t startIdx) {
  assert(jsvHasCharacterData(str));
#ifdef ESPR_UNICODE_SUPPORT
  it->isUTF8 = jsvIsUTF8String(str);
  if (it->isUTF8) { // if it's UTF8, skip the UTF8 tag and go straight to the data
    it->var =  jsvGetUTF8BackingString(str);
    assert(jsvHasCharacterData(it->var));
  } else
#endif
    it->var = jsvLockAgain(str);
  it->varIndex = 0;
  it->charsInVar = jsvGetCharactersInVar(it->var);
#ifdef SPIFLASH_BASE
  if (jsvIsFlashString(it->var)) {
    /* We need to handle flash strings separately as we want
    to preload a buffer of data */
    it->charsInVar = 0;
    it->charIdx = startIdx; // if it's not UTF8 we can just load up the bit we want immediately
    return jsvStringIteratorLoadFlashString(it);
  }
#endif
  jsvStringIteratorUpdatePtr(it);
  it->charIdx = startIdx;
  jsvStringIteratorCatchUp(it);
}

void jsvStringIteratorNewUTF8(JsvStringIterator *it, JsVar *str, size_t startIdx) {
#ifdef ESPR_UNICODE_SUPPORT
  jsvStringIteratorNew(it, str, 0);
  if (it->isUTF8) {
    it->charIdx = 0;
    while (startIdx) {
      jsvStringIteratorNextUTF8(it);
      startIdx--;
    }
  } else
  it->charIdx = startIdx;
  jsvStringIteratorCatchUp(it);
#else
 jsvStringIteratorNew(it, str, startIdx);
#endif
}

/// Update the pointer on a String iterator (should not normally be needed except when allocating a new iterator, but we may call it if we think the pointer in the var may have changed, eg during compaction)
void jsvStringIteratorUpdatePtr(JsvStringIterator *it) {
  if (jsvIsFlatString(it->var)) {
    it->ptr = jsvGetFlatStringPointer(it->var);
  } else if (jsvIsNativeString(it->var)) {
    it->ptr = (char*)it->var->varData.nativeStr.ptr;
#ifdef SPIFLASH_BASE
  } else if (jsvIsFlashString(it->var)) {
    /* don't do anything for flash strings - they are
    handled specially in jsvStringIteratorNew */
#endif
  } else if (it->var)
    it->ptr = &it->var->varData.str[0];
  else
    it->ptr = 0;
}

void jsvStringIteratorClone(JsvStringIterator *dstit, JsvStringIterator *it) {
  *dstit = *it;
  if (dstit->var) {
    jsvLockAgain(dstit->var);
#ifdef SPIFLASH_BASE
    if (it->ptr == it->flashStringBuffer)
      dstit->ptr = dstit->flashStringBuffer;
#endif
  }
}

char jsvStringIteratorGetCharAndNext(JsvStringIterator *it) {
  char ch = jsvStringIteratorGetChar(it);
  jsvStringIteratorNextInline(it);
  return ch;
}

// Get the unicode codepoint, and also return the unicode string in unicodeStr (if non-null) and length in unicodeStrLen (if non-null)
int jsvStringIteratorGetUTF8CharAndNext(JsvStringIterator *it) {
  if (!jsvStringIteratorHasChar(it)) {
    jsvStringIteratorNext(it); // we still need to increment our position even if we don't have a char
    return -1;
  }
#ifdef ESPR_UNICODE_SUPPORT
  unsigned char c = (unsigned char)jsvStringIteratorGetCharAndNext(it);
  if (!it->isUTF8)
    return c;
  int cp=c; // Code point defaults to ASCII
  int ra=0; // Read ahead
  if (c>127 && c<=0xF4) {
    if ((c&0xE0)==0xC0) { // 2-byte code starts with 0b110xxxxx
      cp=c&0x1F;ra=1;
    } else if ((c&0xF0)==0xE0) { // 3-byte code starts with 0b1110xxxx
      cp=c&0x0F;ra=2;
    } else if ((c&0xF8)==0xF0) { // 4-byte code starts with 0b11110xxx
      cp=c&0x07;ra=3;
    }
    while (ra--) {
      c = (unsigned char)jsvStringIteratorGetCharAndNext(it);
      cp=(cp<<6)|((unsigned char)c & 0x3F);
    }
  }
  return cp;
#else
  return (unsigned char)jsvStringIteratorGetCharAndNext(it);
#endif
}


/// Gets the current (>=0) character (or -1)
int jsvStringIteratorGetCharOrMinusOne(JsvStringIterator *it) {
  if (!it->ptr || it->charIdx>=it->charsInVar) return -1;
  return (int)(unsigned char)READ_FLASH_UINT8(&it->ptr[it->charIdx]);
}


void jsvStringIteratorSetChar(JsvStringIterator *it, char c) {
  if (jsvStringIteratorHasChar(it))
    it->ptr[it->charIdx] = c;
}

void jsvStringIteratorSetCharAndNext(JsvStringIterator *it, char c) {
  if (jsvStringIteratorHasChar(it))
    it->ptr[it->charIdx] = c;
  jsvStringIteratorNextInline(it);
}

void jsvStringIteratorNext(JsvStringIterator *it) {
  jsvStringIteratorNextInline(it);
}

void jsvStringIteratorNextUTF8(JsvStringIterator *it) {
#ifdef ESPR_UNICODE_SUPPORT
  if (!it->isUTF8)
    return jsvStringIteratorNext(it);
  unsigned int l = jsUTF8LengthFromChar(jsvStringIteratorGetChar(it));
  while (l--) jsvStringIteratorNext(it);
#else
  return jsvStringIteratorNext(it);
#endif
}

/// Returns a pointer to the next block of data and its length, and moves on to the data after
void jsvStringIteratorGetPtrAndNext(JsvStringIterator *it, unsigned char **data, unsigned int *len) {
  assert(jsvStringIteratorHasChar(it));
  *data = (unsigned char *)&it->ptr[it->charIdx];
  *len = (unsigned int)(it->charsInVar - it->charIdx);
  it->charIdx = it->charsInVar - 1; // jsvStringIteratorNextInline will increment
  jsvStringIteratorNextInline(it);
}

void jsvStringIteratorGotoEnd(JsvStringIterator *it) {
  assert(it->var);
  while (jsvGetLastChild(it->var)) {
    JsVar *next = jsvLock(jsvGetLastChild(it->var));
    jsvUnLock(it->var);
    it->var = next;
    it->varIndex += it->charsInVar;
    it->charsInVar = jsvGetCharactersInVar(it->var);
  }
  it->ptr = &it->var->varData.str[0];
  if (it->charsInVar) it->charIdx = it->charsInVar-1;
  else it->charIdx = 0;
}

/// Go to the given (non-UTF8) position in the string iterator. Needs the string again in case we're going back and need to start from the beginning
void jsvStringIteratorGoto(JsvStringIterator *it, JsVar *str, size_t idx) {
  if (idx>=it->varIndex) {
    it->charIdx = idx - it->varIndex;
    jsvStringIteratorCatchUp(it);
  } else {
    jsvStringIteratorFree(it);
    jsvStringIteratorNew(it, str, idx);
  }
}

/// Go to the given UTF8 position in the string iterator. Needs the string again in case we're going back and need to start from the beginning
void jsvStringIteratorGotoUTF8(JsvStringIterator *it, JsVar *str, size_t idx) {
#ifdef ESPR_UNICODE_SUPPORT
  if (!it->isUTF8 && idx>=it->varIndex) {
    it->charIdx = idx - it->varIndex;
    jsvStringIteratorCatchUp(it);
  } else {
    jsvStringIteratorFree(it);
    // TODO: could we just skip forwards if we knew the current UTF8 position?
    jsvStringIteratorNewUTF8(it, str, idx);
  }
#else
  jsvStringIteratorGoto(it, str, idx);
#endif
}

void jsvStringIteratorAppend(JsvStringIterator *it, char ch) {
  if (!it->var) return;
  if (it->charsInVar>0) {
    assert(it->charIdx+1 == it->charsInVar /* check at end */);
    it->charIdx++;
  } else
    assert(it->charIdx == 0);
  /* Note: jsvGetMaxCharactersInVar will return the wrong length when
   * applied to flat strings, but we don't care because the length will
   * be smaller than charIdx, which will force a new string to be
   * appended onto the end  */
  if (it->charIdx >= jsvGetMaxCharactersInVar(it->var)) {
    assert(jsvHasStringExt(it->var)); // we can't add to something if we can't add a StringExt!
    if (!jsvHasStringExt(it->var)) return; // safety check!
    assert(!jsvGetLastChild(it->var));
    JsVar *next = jsvNewWithFlags(JSV_STRING_EXT_0);
    if (!next) {
      jsvUnLock(it->var);
      it->var = 0;
      it->ptr = 0;
      it->charIdx = 0;
      return; // out of memory
    }
    // we don't ref, because  StringExts are never reffed as they only have one owner (and ALWAYS have an owner)
    jsvSetLastChild(it->var, jsvGetRef(next));
    jsvUnLock(it->var);
    it->var = next;
    it->ptr = &next->varData.str[0];
    it->varIndex += it->charIdx;
    it->charIdx = 0; // it's new, so empty
  }
  it->ptr[it->charIdx] = ch;
  it->charsInVar = it->charIdx+1;
  jsvSetCharactersInVar(it->var, it->charsInVar);
}

void jsvStringIteratorAppendString(JsvStringIterator *it, JsVar *str, size_t startIdx, int maxLength) {
  JsvStringIterator sit;
  jsvStringIteratorNew(&sit, str, startIdx);
  while (jsvStringIteratorHasChar(&sit) && maxLength>0) {
    jsvStringIteratorAppend(it, jsvStringIteratorGetCharAndNext(&sit));
    maxLength--;
  }
  jsvStringIteratorFree(&sit);
}

// --------------------------------------------------------------------------------------------

void jsvObjectIteratorNew(JsvObjectIterator *it, JsVar *obj) {
  assert(!obj || jsvHasChildren(obj));
  it->var = jsvHasChildren(obj) ? jsvLockSafe(jsvGetFirstChild(obj)) : 0;
}

/// Clone the iterator
void jsvObjectIteratorClone(JsvObjectIterator *dstit, JsvObjectIterator *it) {
  *dstit = *it;
  jsvLockAgainSafe(dstit->var);
}

/// Move to next item
void jsvObjectIteratorNext(JsvObjectIterator *it) {
  if (it->var) {
    JsVarRef next = jsvGetNextSibling(it->var);
    jsvUnLock(it->var);
    it->var = jsvLockSafe(next);
  }
}

void jsvObjectIteratorSetValue(JsvObjectIterator *it, JsVar *value) {
  if (!it->var) return; // end of object
  jsvSetValueOfName(it->var, value);
}

void jsvObjectIteratorRemoveAndGotoNext(JsvObjectIterator *it, JsVar *parent) {
  if (it->var) {
    JsVarRef next = jsvGetNextSibling(it->var);
    jsvRemoveChildAndUnLock(parent, it->var);
    it->var = jsvLockSafe(next);
  }
}

// --------------------------------------------------------------------------------------------
void   jsvArrayBufferIteratorNew(JsvArrayBufferIterator *it, JsVar *arrayBuffer, size_t index) {
  assert(jsvIsArrayBuffer(arrayBuffer));
  it->index = index;
  it->type = arrayBuffer->varData.arraybuffer.type;
  it->byteLength = arrayBuffer->varData.arraybuffer.length * JSV_ARRAYBUFFER_GET_SIZE(it->type);
  it->byteOffset = arrayBuffer->varData.arraybuffer.byteOffset;
  JsVar *arrayBufferData = jsvGetArrayBufferBackingString(arrayBuffer, NULL);

  it->byteLength += it->byteOffset; // because we'll check if we have more bytes using this
  it->byteOffset = it->byteOffset + index*JSV_ARRAYBUFFER_GET_SIZE(it->type);
  if (it->byteOffset>=(it->byteLength+1-JSV_ARRAYBUFFER_GET_SIZE(it->type))) {
    jsvUnLock(arrayBufferData);
    it->type = ARRAYBUFFERVIEW_UNDEFINED;
    return;
  }
  jsvStringIteratorNew(&it->it, arrayBufferData, (size_t)it->byteOffset);
  jsvUnLock(arrayBufferData);
  it->hasAccessedElement = false;
}

/// Clone the iterator
ALWAYS_INLINE void jsvArrayBufferIteratorClone(JsvArrayBufferIterator *dstit, JsvArrayBufferIterator *it) {
  *dstit = *it;
  jsvStringIteratorClone(&dstit->it, &it->it);
}

static void jsvArrayBufferIteratorGetValueData(JsvArrayBufferIterator *it, char *data) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  assert(!it->hasAccessedElement); // we just haven't implemented this case yet
  int i,dataLen = (int)JSV_ARRAYBUFFER_GET_SIZE(it->type);
  for (i=0;i<dataLen;i++) {
    data[i] = jsvStringIteratorGetChar(&it->it);
    if (dataLen!=1) jsvStringIteratorNext(&it->it);
  }
  if (dataLen!=1) it->hasAccessedElement = true;
}

static JsVarInt jsvArrayBufferIteratorDataToInt(JsvArrayBufferIterator *it, char *data) {
  unsigned int dataLen = JSV_ARRAYBUFFER_GET_SIZE(it->type);
  unsigned int bits = 8*dataLen;
  JsVarInt mask = (JsVarInt)((1ULL << bits)-1);
  JsVarInt v = *(int*)data;
  v = v & mask;
  if (JSV_ARRAYBUFFER_IS_SIGNED(it->type) && (v&(JsVarInt)(1UL<<(bits-1))))
    v |= ~mask;
  return v;
}

static JsVarFloat jsvArrayBufferIteratorDataToFloat(JsvArrayBufferIterator *it, char *data) {
  unsigned int dataLen = JSV_ARRAYBUFFER_GET_SIZE(it->type);
  JsVarFloat v = 0;
  if (dataLen==4) v = *(float*)data;
  else if (dataLen==8) v = *(double*)data;
  else assert(0);
  return v;
}

JsVar *jsvArrayBufferIteratorGetValue(JsvArrayBufferIterator *it, bool bigEndian) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return 0;
  char data[8] __attribute__ ((aligned (4)));
  jsvArrayBufferIteratorGetValueData(it, data);
  if (bigEndian)
      reverseBytes(data, JSV_ARRAYBUFFER_GET_SIZE(it->type));
  if (JSV_ARRAYBUFFER_IS_FLOAT(it->type)) {
    return jsvNewFromFloat(jsvArrayBufferIteratorDataToFloat(it, data));
  } else {
    JsVarInt i = jsvArrayBufferIteratorDataToInt(it, data);
    if (it->type == ARRAYBUFFERVIEW_UINT32)
      return jsvNewFromLongInteger((long long)(uint32_t)i);
    return jsvNewFromInteger(i);
  }
}

JsVar *jsvArrayBufferIteratorGetValueAndRewind(JsvArrayBufferIterator *it) {
  JsvStringIterator oldIt;
  jsvStringIteratorClone(&oldIt, &it->it);
  JsVar *v = jsvArrayBufferIteratorGetValue(it, false/*little endian*/);
  jsvStringIteratorFree(&it->it);
  it->it = oldIt;
  it->hasAccessedElement = false;
  return v;
}

JsVarInt jsvArrayBufferIteratorGetIntegerValue(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return 0;
  char data[8] __attribute__ ((aligned (4)));
  jsvArrayBufferIteratorGetValueData(it, data);
  if (JSV_ARRAYBUFFER_IS_FLOAT(it->type)) {
    return (JsVarInt)jsvArrayBufferIteratorDataToFloat(it, data);
  } else {
    return jsvArrayBufferIteratorDataToInt(it, data);
  }
}

JsVarFloat jsvArrayBufferIteratorGetFloatValue(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return 0;
  char data[8] __attribute__ ((aligned (4)));
  jsvArrayBufferIteratorGetValueData(it, data);
  if (JSV_ARRAYBUFFER_IS_FLOAT(it->type)) {
    return jsvArrayBufferIteratorDataToFloat(it, data);
  } else {
    return (JsVarFloat)jsvArrayBufferIteratorDataToInt(it, data);
  }
}

static void jsvArrayBufferIteratorIntToData(char *data, unsigned int dataLen, int type, JsVarInt v) {
  if (JSV_ARRAYBUFFER_IS_CLAMPED(type)) {
    assert(dataLen==1 && !JSV_ARRAYBUFFER_IS_SIGNED(type)); // all we support right now
    if (v<0) v=0;
    if (v>255) v=255;
  }
  // we don't care about sign when writing (or any extra bytes!) - as it gets truncated
  if (dataLen==8) *(long long*)data = (long long)v;
  else *(int*)data = (int)v;
}

static void jsvArrayBufferIteratorFloatToData(char *data,  unsigned int dataLen, int type, JsVarFloat v) {
  NOT_USED(type);
  if (dataLen==4) { *(float*)data = (float)v; }
  else if (dataLen==8) { *(double*)data = (double)v; }
  else assert(0);
}

void jsvArrayBufferIteratorSetIntegerValue(JsvArrayBufferIterator *it, JsVarInt v) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  assert(!it->hasAccessedElement); // we just haven't implemented this case yet
  char data[8] __attribute__ ((aligned (4)));
  unsigned int i,dataLen = JSV_ARRAYBUFFER_GET_SIZE(it->type);

  if (JSV_ARRAYBUFFER_IS_FLOAT(it->type)) {
    jsvArrayBufferIteratorFloatToData(data, dataLen, it->type, (JsVarFloat)v);
  } else {
    jsvArrayBufferIteratorIntToData(data, dataLen, it->type, v);
  }

  for (i=0;i<dataLen;i++) {
    jsvStringIteratorSetChar(&it->it, data[i]);
    if (dataLen!=1) jsvStringIteratorNext(&it->it);
  }
  if (dataLen!=1) it->hasAccessedElement = true;
}

void jsvArrayBufferIteratorSetValue(JsvArrayBufferIterator *it, JsVar *value, bool bigEndian) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  assert(!it->hasAccessedElement); // we just haven't implemented this case yet
  char data[8] __attribute__ ((aligned (4)));
  int i,dataLen = (int)JSV_ARRAYBUFFER_GET_SIZE(it->type);

  if (JSV_ARRAYBUFFER_IS_FLOAT(it->type)) {
    jsvArrayBufferIteratorFloatToData(data, (unsigned)dataLen, it->type, jsvGetFloat(value));
  } else {
    jsvArrayBufferIteratorIntToData(data, (unsigned)dataLen, it->type, jsvGetInteger(value));
  }

  if (bigEndian)
    reverseBytes(data, dataLen);

  for (i=0;i<dataLen;i++) {
    jsvStringIteratorSetChar(&it->it, data[i]);
    if (dataLen!=1) jsvStringIteratorNext(&it->it);
  }
  if (dataLen!=1) it->hasAccessedElement = true;
}

void jsvArrayBufferIteratorSetByteValue(JsvArrayBufferIterator *it, char c) {
  if (JSV_ARRAYBUFFER_GET_SIZE(it->type)!=1) {
    assert(0);
    return;
  }
  jsvStringIteratorSetChar(&it->it, c);
}

void jsvArrayBufferIteratorSetValueAndRewind(JsvArrayBufferIterator *it, JsVar *value) {
  JsvStringIterator oldIt;
  jsvStringIteratorClone(&oldIt, &it->it);
  jsvArrayBufferIteratorSetValue(it, value, false/*little endian*/);
  jsvStringIteratorFree(&it->it);
  jsvStringIteratorClone(&it->it, &oldIt);
  jsvStringIteratorFree(&oldIt);
  it->hasAccessedElement = false;
}


JsVar* jsvArrayBufferIteratorGetIndex(JsvArrayBufferIterator *it) {
  return jsvNewFromInteger((JsVarInt)it->index);
}

bool jsvArrayBufferIteratorHasElement(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return false;
  if (it->hasAccessedElement) return true;
  return (it->byteOffset+JSV_ARRAYBUFFER_GET_SIZE(it->type)) <= it->byteLength;
}

void   jsvArrayBufferIteratorNext(JsvArrayBufferIterator *it) {
  it->index++;
  it->byteOffset += JSV_ARRAYBUFFER_GET_SIZE(it->type);
  if (!it->hasAccessedElement) {
    unsigned int dataLen = JSV_ARRAYBUFFER_GET_SIZE(it->type);
    while (dataLen--)
      jsvStringIteratorNext(&it->it);
  } else
    it->hasAccessedElement = false;
}

void   jsvArrayBufferIteratorFree(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  jsvStringIteratorFree(&it->it);
}

#ifdef ESPR_UNICODE_SUPPORT
// For jsvIterator, get the next UTF8 char
static void jsvIteratorUTF8Next(JsvIterator *it) {
  if (!jsvStringIteratorHasChar(&it->it.unicode.str)) {
    it->it.unicode.currentCh = -1;
    return;
  }
  it->it.unicode.currentCh = jsvStringIteratorGetUTF8CharAndNext(&it->it.unicode.str);
}
#endif

// --------------------------------------------------------------------------------------------
/* General Purpose iterator, for Strings, Arrays, Objects, Typed Arrays */

void jsvIteratorNew(JsvIterator *it, JsVar *obj, JsvIteratorFlags flags) {
  if (jsvIsArray(obj) || jsvIsObject(obj) || jsvIsFunction(obj) || jsvIsGetterOrSetter(obj)) {
    it->type = JSVI_OBJECT;
    if (jsvIsArray(obj) && (flags&JSIF_EVERY_ARRAY_ELEMENT)) {
      it->type = JSVI_FULLARRAY;
      it->it.obj.index = 0;
      it->it.obj.var = jsvLockAgain(obj);
    }
    jsvObjectIteratorNew(&it->it.obj.it, obj);
  } else if (jsvIsArrayBuffer(obj)) {
    it->type = JSVI_ARRAYBUFFER;
    jsvArrayBufferIteratorNew(&it->it.buf, obj, 0);
#ifdef ESPR_UNICODE_SUPPORT
  } else if (jsvIsUTF8String(obj)) {
    it->type = JSVI_UNICODE;
    it->it.unicode.index = 0;
    jsvStringIteratorNew(&it->it.unicode.str, jsvGetUTF8BackingString(obj), 0);
    jsvIteratorUTF8Next(it); // read a char as the current char
#endif
  } else if (jsvHasCharacterData(obj)) {
    it->type = JSVI_STRING;
    jsvStringIteratorNew(&it->it.str, obj, 0);
  } else {
    it->type = JSVI_NONE;
    assert(0);
  }
}

JsVar *jsvIteratorGetKey(JsvIterator *it) {
  // FIXME: *which* things expect a variable name?
  switch (it->type) {
  case JSVI_FULLARRAY : return jsvNewFromInteger(it->it.obj.index);
  case JSVI_OBJECT : return jsvObjectIteratorGetKey(&it->it.obj.it);
  case JSVI_STRING : return jsvMakeIntoVariableName(jsvNewFromInteger((JsVarInt)jsvStringIteratorGetIndex(&it->it.str)), 0); // some things expect a variable name
  case JSVI_ARRAYBUFFER : return jsvMakeIntoVariableName(jsvArrayBufferIteratorGetIndex(&it->it.buf), 0); // some things expect a veriable name
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE: return jsvMakeIntoVariableName(jsvNewFromInteger((JsVarInt)it->it.unicode.index), 0);
#endif
  default: assert(0); return 0;
  }
}

JsVar *jsvIteratorGetValue(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: if (jsvIsIntegerish(it->it.obj.it.var) &&
                           jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
                         return jsvObjectIteratorGetValue(&it->it.obj.it);
                       return 0;
  case JSVI_OBJECT : return jsvObjectIteratorGetValue(&it->it.obj.it);
  case JSVI_STRING : { char buf = jsvStringIteratorGetChar(&it->it.str); return jsvNewStringOfLength(1, &buf); }
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorGetValueAndRewind(&it->it.buf);
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE: {
    char uBuf[4];
    unsigned int l = (unsigned int)jsUTF8Encode(it->it.unicode.currentCh, uBuf);
    return jsvNewStringOfLength(l, uBuf);
  }
#endif
  default: assert(0); return 0;
  }
}

JsVarInt jsvIteratorGetIntegerValue(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: {
    if (jsvIsNameInt(it->it.obj.it.var) &&
        jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
      return (JsVarInt)jsvGetFirstChildSigned(it->it.obj.it.var);
    if (jsvIsIntegerish(it->it.obj.it.var) &&
        jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
      return jsvGetIntegerAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
    return 0;
  }
  case JSVI_OBJECT : {
    // fast path for arrays of ints
    if (jsvIsNameInt(it->it.obj.it.var))
      return (JsVarInt)jsvGetFirstChildSigned(it->it.obj.it.var);
    return jsvGetIntegerAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
  }
  case JSVI_STRING : return (JsVarInt)jsvStringIteratorGetChar(&it->it.str);
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorGetIntegerValue(&it->it.buf);
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE: return it->it.unicode.currentCh;
#endif
  default: assert(0); return 0;
  }
}

JsVarFloat jsvIteratorGetFloatValue(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: if (jsvIsIntegerish(it->it.obj.it.var) &&
                           jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
                         return jsvGetFloatAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
                       return NAN;
  case JSVI_OBJECT : return jsvGetFloatAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorGetFloatValue(&it->it.buf);
  default: return (JsVarFloat)jsvIteratorGetIntegerValue(it); // fallback to asking for integer value
  }
}

JsVar *jsvIteratorSetValue(JsvIterator *it, JsVar *value) {
  switch (it->type) {
  case JSVI_FULLARRAY:  if (jsvIsIntegerish(it->it.obj.it.var) &&
                             jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
                          jsvObjectIteratorSetValue(&it->it.obj.it, value);
                        jsvSetArrayItem(it->it.obj.var, it->it.obj.index, value);
                        break;
  case JSVI_OBJECT : jsvObjectIteratorSetValue(&it->it.obj.it, value); break;
  case JSVI_STRING : jsvStringIteratorSetChar(&it->it.str, (char)(jsvIsString(value) ? value->varData.str[0] : (char)jsvGetInteger(value))); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorSetValueAndRewind(&it->it.buf, value); break;
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE: break; // can't set a unicode value
#endif
  default: assert(0); break;
  }
  return value;
}

bool jsvIteratorHasElement(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: return it->it.obj.index < jsvGetArrayLength(it->it.obj.var);
  case JSVI_OBJECT : return jsvObjectIteratorHasValue(&it->it.obj.it);
  case JSVI_STRING : return jsvStringIteratorHasChar(&it->it.str);
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorHasElement(&it->it.buf);
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE : return it->it.unicode.currentCh>=0;
#endif
  default: assert(0); return 0;
  }
}

void jsvIteratorNext(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: it->it.obj.index++;
                       if (jsvIsIntegerish(it->it.obj.it.var) &&
                           jsvGetInteger(it->it.obj.it.var)<it->it.obj.index)
                         jsvObjectIteratorNext(&it->it.obj.it);
                       break;
  case JSVI_OBJECT : jsvObjectIteratorNext(&it->it.obj.it); break;
  case JSVI_STRING : jsvStringIteratorNext(&it->it.str); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorNext(&it->it.buf); break;
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE : it->it.unicode.index++; jsvIteratorUTF8Next(it); break;
#endif
  default: assert(0); break;
  }
}

void jsvIteratorFree(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: jsvUnLock(it->it.obj.var);
                       jsvObjectIteratorFree(&it->it.obj.it);
       break;
  case JSVI_OBJECT : jsvObjectIteratorFree(&it->it.obj.it); break;
  case JSVI_STRING : jsvStringIteratorFree(&it->it.str); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorFree(&it->it.buf); break;
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE : jsvStringIteratorFree(&it->it.unicode.str); break;
#endif
  default: assert(0); break;
  }
}

void jsvIteratorClone(JsvIterator *dstit, JsvIterator *it) {
  dstit->type = it->type;
  switch (it->type) {
  case JSVI_FULLARRAY: dstit->it.obj.index = it->it.obj.index;
                       dstit->it.obj.var = jsvLockAgain(it->it.obj.var);
                       jsvObjectIteratorClone(&dstit->it.obj.it, &it->it.obj.it);
                       break;
  case JSVI_OBJECT : jsvObjectIteratorClone(&dstit->it.obj.it, &it->it.obj.it); break;
  case JSVI_STRING : jsvStringIteratorClone(&dstit->it.str, &it->it.str); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorClone(&dstit->it.buf, &it->it.buf); break;
#ifdef ESPR_UNICODE_SUPPORT
  case JSVI_UNICODE : jsvStringIteratorClone(&dstit->it.str, &it->it.str);
                      dstit->it.unicode.currentCh = it->it.unicode.currentCh;
                      dstit->it.unicode.index = it->it.unicode.index;
                      break;
#endif
  default: assert(0); break;
  }
}

/* Copyright (c) 2012-2018 LevelDOWN contributors
 * See list at <https://github.com/level/leveldown#contributing>
 * MIT License <https://github.com/level/leveldown/blob/master/LICENSE.md>
 */
#ifndef LD_LEVELDOWN_H
#define LD_LEVELDOWN_H

#include <node.h>
#include <node_buffer.h>
#include <leveldb/slice.h>
#include <nan.h>

static inline size_t StringOrBufferLength(v8::Local<v8::Value> obj) {
  Nan::HandleScope scope;

  return (!obj->ToObject().IsEmpty()
    && node::Buffer::HasInstance(obj->ToObject()))
    ? node::Buffer::Length(obj->ToObject())
    : obj->ToString()->Utf8Length();
}

static inline leveldb::Slice MakeSlice(v8::Local<v8::Value> from) {
  if (from->IsNull() || from->IsUndefined()) return leveldb::Slice();

  v8::Local<v8::Object> o = from->ToObject();
  char* data = node::Buffer::Data(o);
  size_t size = node::Buffer::Length(o);

  return leveldb::Slice(data, size);
}

static inline leveldb::Slice MakeSliceFromString(v8::Local<v8::Value> from) {
  v8::Local<v8::String> str = from->ToString();
  size_t size = str->Utf8Length();
  char* data = new char[size];
  str->WriteUtf8(data, -1, NULL, v8::String::NO_NULL_TERMINATION);

  return leveldb::Slice(data, size);
}

#define LD_STRING_OR_BUFFER_TO_COPY(to, from, name)                            \
  size_t to ## Sz_;                                                            \
  char* to ## Ch_;                                                             \
  if (!from->ToObject().IsEmpty()                                              \
      && node::Buffer::HasInstance(from->ToObject())) {                        \
    to ## Sz_ = node::Buffer::Length(from->ToObject());                        \
    to ## Ch_ = new char[to ## Sz_];                                           \
    memcpy(to ## Ch_, node::Buffer::Data(from->ToObject()), to ## Sz_);        \
  } else {                                                                     \
    v8::Local<v8::String> to ## Str = from->ToString();                        \
    to ## Sz_ = to ## Str->Utf8Length();                                       \
    to ## Ch_ = new char[to ## Sz_];                                           \
    to ## Str->WriteUtf8(                                                      \
        to ## Ch_                                                              \
      , -1                                                                     \
      , NULL, v8::String::NO_NULL_TERMINATION                                  \
    );                                                                         \
  }

#define LD_RUN_CALLBACK(resource, callback, argc, argv)                 \
  Nan::AsyncResource ar(resource);                                      \
  ar.runInAsyncScope(Nan::GetCurrentContext()->Global(),                \
                     callback, argc, argv);

/* LD_METHOD_SETUP_COMMON setup the following objects:
 *  - Database* database
 *  - v8::Local<v8::Object> optionsObj (may be empty)
 *  - Nan::Persistent<v8::Function> callback (won't be empty)
 * Will throw/return if there isn't a callback in arg 0 or 1
 */
#define LD_METHOD_SETUP_COMMON(name, optionPos, callbackPos)                   \
  if (info.Length() == 0)                                                      \
    return Nan::ThrowError(#name "() requires a callback argument");           \
  leveldown::Database* database =                                              \
    Nan::ObjectWrap::Unwrap<leveldown::Database>(info.This());                 \
  v8::Local<v8::Object> optionsObj;                                            \
  v8::Local<v8::Function> callback;                                            \
  if (optionPos == -1 && info[callbackPos]->IsFunction()) {                    \
    callback = info[callbackPos].As<v8::Function>();                           \
  } else if (optionPos != -1 && info[callbackPos - 1]->IsFunction()) {         \
    callback = info[callbackPos - 1].As<v8::Function>();                       \
  } else if (optionPos != -1                                                   \
        && info[optionPos]->IsObject()                                         \
        && info[callbackPos]->IsFunction()) {                                  \
    optionsObj = info[optionPos].As<v8::Object>();                             \
    callback = info[callbackPos].As<v8::Function>();                           \
  } else {                                                                     \
    return Nan::ThrowError(#name "() requires a callback argument");           \
  }

#define LD_METHOD_SETUP_COMMON_ONEARG(name) LD_METHOD_SETUP_COMMON(name, -1, 0)

#endif

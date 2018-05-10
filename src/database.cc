/* Copyright (c) 2012-2018 LevelDOWN contributors
 * See list at <https://github.com/level/leveldown#contributing>
 * MIT License <https://github.com/level/leveldown/blob/master/LICENSE.md>
 */

#include <node.h>
#include <node_buffer.h>

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

#include "leveldown.h"
#include "database.h"
#include "async.h"
#include "database_async.h"
#include "batch.h"
#include "iterator.h"
#include "common.h"

namespace leveldown {

static Nan::Persistent<v8::FunctionTemplate> database_constructor;

Database::Database (const v8::Local<v8::Value>& from)
  : location(*Nan::Utf8String(from))
    // strange because - inexplicably - *operator is defined by Utf8String to cast to 'const char*'
  , currentIteratorId(0)
  , pendingCloseWorker(nullptr)
{};

/* Calls from worker threads, NO V8 HERE *****************************/

leveldb::Status Database::OpenDatabase (
        const leveldb::Options& options
    ) {

  leveldb::DB* pdb;
  auto status = leveldb::DB::Open(options, location, &pdb);
  db.reset(pdb);
  return status;
}

leveldb::Status Database::PutToDatabase (
        const leveldb::WriteOptions& options
      , leveldb::Slice key
      , leveldb::Slice value
    ) {
  return db->Put(options, key, value);
}

leveldb::Status Database::GetFromDatabase (
        const leveldb::ReadOptions& options
      , leveldb::Slice key
      , std::string& value
    ) {
  return db->Get(options, key, &value);
}

leveldb::Status Database::GetManyFromDatabase (
    const leveldb::ReadOptions &options
  , const std::vector<leveldb::Slice> &keys
  , std::vector<std::string> &values
) {

  for (size_t i = 0; i < keys.size(); i++) {
    auto status = db->Get(options, keys[i], &values[i]);
    if (!status.ok()) return status;
  }

  return leveldb::Status::OK();
}

leveldb::Status Database::DeleteFromDatabase (
        const leveldb::WriteOptions& options
      , leveldb::Slice key
    ) {
  return db->Delete(options, key);
}

leveldb::Status Database::WriteBatchToDatabase (
        const leveldb::WriteOptions& options
      , leveldb::WriteBatch* batch
    ) {
  return db->Write(options, batch);
}

uint64_t Database::ApproximateSizeFromDatabase (const leveldb::Range* range) {
  uint64_t size;
  db->GetApproximateSizes(range, 1, &size);
  return size;
}

void Database::CompactRangeFromDatabase (const leveldb::Slice* start,
                                         const leveldb::Slice* end) {
  db->CompactRange(start, end);
}

void Database::GetPropertyFromDatabase (
      const leveldb::Slice& property
    , std::string* value) {

  db->GetProperty(property, value);
}

leveldb::Iterator* Database::NewIterator (const leveldb::ReadOptions& options) {
  return db->NewIterator(options);
}

const leveldb::Snapshot* Database::NewSnapshot () {
  return db->GetSnapshot();
}

void Database::ReleaseSnapshot (const leveldb::Snapshot* snapshot) {
  return db->ReleaseSnapshot(snapshot);
}

void Database::ReleaseIterator (uint32_t id) {
  // called each time an Iterator is End()ed, in the main thread
  // we have to remove our reference to it and if it's the last iterator
  // we have to invoke a pending CloseWorker if there is one
  // if there is a pending CloseWorker it means that we're waiting for
  // iterators to end before we can close them
  iterators.erase(id);
  if (iterators.empty() && pendingCloseWorker != nullptr) {
    Nan::AsyncQueueWorker(reinterpret_cast<AsyncWorker*>(pendingCloseWorker));
    pendingCloseWorker = nullptr;
  }
}

void Database::CloseDatabase () {
  db.reset();
  blockCache.reset();
  filterPolicy.reset();
}

/* V8 exposed functions *****************************/

NAN_METHOD(LevelDOWN) {
  v8::Local<v8::String> location = info[0].As<v8::String>();
  info.GetReturnValue().Set(Database::NewInstance(location));
}

void Database::Init () {
  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(Database::New);
  database_constructor.Reset(tpl);
  tpl->SetClassName(Nan::New("Database").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Nan::SetPrototypeMethod(tpl, "open", Database::Open);
  Nan::SetPrototypeMethod(tpl, "close", Database::Close);
  Nan::SetPrototypeMethod(tpl, "put", Database::Put);
  Nan::SetPrototypeMethod(tpl, "putMany", Database::PutMany);
  Nan::SetPrototypeMethod(tpl, "get", Database::Get);
  Nan::SetPrototypeMethod(tpl, "getMany", Database::GetMany);
  Nan::SetPrototypeMethod(tpl, "del", Database::Delete);
  Nan::SetPrototypeMethod(tpl, "batch", Database::Batch);
  Nan::SetPrototypeMethod(tpl, "approximateSize", Database::ApproximateSize);
  Nan::SetPrototypeMethod(tpl, "compactRange", Database::CompactRange);
  Nan::SetPrototypeMethod(tpl, "getProperty", Database::GetProperty);
  Nan::SetPrototypeMethod(tpl, "iterator", Database::Iterator);
}

NAN_METHOD(Database::New) {
  Database* obj = new Database(info[0]);
  obj->Wrap(info.This());

  info.GetReturnValue().Set(info.This());
}

v8::Local<v8::Value> Database::NewInstance (v8::Local<v8::String> &location) {
  Nan::EscapableHandleScope scope;

  Nan::MaybeLocal<v8::Object> maybeInstance;
  v8::Local<v8::Object> instance;

  v8::Local<v8::FunctionTemplate> constructorHandle =
      Nan::New<v8::FunctionTemplate>(database_constructor);

  v8::Local<v8::Value> argv[] = { location };
  maybeInstance = Nan::NewInstance(constructorHandle->GetFunction(), 1, argv);

  if (maybeInstance.IsEmpty())
      Nan::ThrowError("Could not create new Database instance");
  else
    instance = maybeInstance.ToLocalChecked();
  return scope.Escape(instance);
}

NAN_METHOD(Database::Open) {
  LD_METHOD_SETUP_COMMON(open, 0, 1)

  bool createIfMissing = BooleanOptionValue(optionsObj, "createIfMissing", true);
  bool errorIfExists = BooleanOptionValue(optionsObj, "errorIfExists");
  bool compression = BooleanOptionValue(optionsObj, "compression", true);

  uint32_t cacheSize = UInt32OptionValue(optionsObj, "cacheSize", 8 << 20);
  uint32_t writeBufferSize = UInt32OptionValue(
      optionsObj
    , "writeBufferSize"
    , 4 << 20
  );
  uint32_t blockSize = UInt32OptionValue(optionsObj, "blockSize", 4096);
  uint32_t maxOpenFiles = UInt32OptionValue(optionsObj, "maxOpenFiles", 1000);
  uint32_t blockRestartInterval = UInt32OptionValue(
      optionsObj
    , "blockRestartInterval"
    , 16
  );
  uint32_t maxFileSize = UInt32OptionValue(optionsObj, "maxFileSize", 2 << 20);

  database->blockCache.reset(leveldb::NewLRUCache(cacheSize));
  database->filterPolicy.reset(leveldb::NewBloomFilterPolicy(10));

  OpenWorker* worker = new OpenWorker(
      database
    , new Nan::Callback(callback)
    , database->blockCache.get()
    , database->filterPolicy.get()
    , createIfMissing
    , errorIfExists
    , compression
    , writeBufferSize
    , blockSize
    , maxOpenFiles
    , blockRestartInterval
    , maxFileSize
  );
  // persist to prevent accidental GC
  v8::Local<v8::Object> _this = info.This();
  worker->SaveToPersistent("database", _this);
  Nan::AsyncQueueWorker(worker);
}

// for an empty callback to iterator.end()
NAN_METHOD(EmptyMethod) {
}

NAN_METHOD(Database::Close) {
  LD_METHOD_SETUP_COMMON_ONEARG(close)

  CloseWorker* worker = new CloseWorker(
      database
    , new Nan::Callback(callback)
  );
  // persist to prevent accidental GC
  v8::Local<v8::Object> _this = info.This();
  worker->SaveToPersistent("database", _this);

  if (!database->iterators.empty()) {
    // yikes, we still have iterators open! naughty naughty.
    // we have to queue up a CloseWorker and manually close each of them.
    // the CloseWorker will be invoked once they are all cleaned up
    database->pendingCloseWorker = worker;

    for (
        std::map< uint32_t, leveldown::Iterator * >::iterator it
            = database->iterators.begin()
      ; it != database->iterators.end()
      ; ++it) {

        // for each iterator still open, first check if it's already in
        // the process of ending (ended==true means an async End() is
        // in progress), if not, then we call End() with an empty callback
        // function and wait for it to hit ReleaseIterator() where our
        // CloseWorker will be invoked

        leveldown::Iterator *iterator = it->second;

        if (!iterator->ended) {
          v8::Local<v8::Function> end =
              v8::Local<v8::Function>::Cast(iterator->handle()->Get(
                  Nan::New<v8::String>("end").ToLocalChecked()));
          v8::Local<v8::Value> argv[] = {
              Nan::New<v8::FunctionTemplate>(EmptyMethod)->GetFunction() // empty callback
          };
          Nan::AsyncResource ar("leveldown:iterator.end");
          ar.runInAsyncScope(iterator->handle(), end, 1, argv);
        }
    }
  } else {
    Nan::AsyncQueueWorker(worker);
  }
}

NAN_METHOD(Database::Put) {
  LD_METHOD_SETUP_COMMON(put, 2, 3)

  leveldb::Slice key = MakeSlice(info[0].As<v8::Object>());
  leveldb::Slice value = MakeSlice(info[1].As<v8::Object>());

  bool sync = BooleanOptionValue(optionsObj, "sync");

  WriteWorker* worker = new WriteWorker(
      database
    , new Nan::Callback(callback)
    , key
    , value
    , sync
  );

  // persist to prevent accidental GC
  v8::Local<v8::Object> _this = info.This();
  worker->SaveToPersistent("database", _this);
  Nan::AsyncQueueWorker(worker);
}

NAN_METHOD(Database::Get) {
  LD_METHOD_SETUP_COMMON(get, 1, 2)

  leveldb::Slice key = MakeSlice(info[0].As<v8::Object>());

  bool asBuffer = BooleanOptionValue(optionsObj, "asBuffer", true);
  bool fillCache = BooleanOptionValue(optionsObj, "fillCache", true);

  ReadWorker* worker = new ReadWorker(
      database
    , new Nan::Callback(callback)
    , key
    , asBuffer
    , fillCache
  );
  // persist to prevent accidental GC
  v8::Local<v8::Object> _this = info.This();
  worker->SaveToPersistent("database", _this);
  Nan::AsyncQueueWorker(worker);
}

NAN_METHOD(Database::GetMany) {
  LD_METHOD_SETUP_COMMON(get, 1, 2)

  auto array = v8::Local<v8::Array>::Cast(info[0]);
  const int len = array->Length();

  std::vector<leveldb::Slice> keys;
  keys.reserve(len);

  for(int i = 0; i < len; i++) {
    auto obj = v8::Local<v8::Object>::Cast(array->Get(i));
    keys.push_back(MakeSlice(obj));
  }

  auto worker = new ReadManyWorker(
      database
    , new Nan::Callback(callback)
    , std::move(keys)
  );
  // persist to prevent accidental GC
  auto _this = info.This();
  worker->SaveToPersistent("database", _this);
  Nan::AsyncQueueWorker(worker);
}

NAN_METHOD(Database::Delete) {
  LD_METHOD_SETUP_COMMON(del, 1, 2)

  leveldb::Slice key = MakeSlice(info[0].As<v8::Object>());

  bool sync = BooleanOptionValue(optionsObj, "sync");

  DeleteWorker* worker = new DeleteWorker(
      database
    , new Nan::Callback(callback)
    , key
    , sync
  );
  // persist to prevent accidental GC
  v8::Local<v8::Object> _this = info.This();
  worker->SaveToPersistent("database", _this);
  Nan::AsyncQueueWorker(worker);
}

NAN_METHOD(Database::Batch) {
  if ((info.Length() == 0 || info.Length() == 1) && !info[0]->IsArray()) {
    v8::Local<v8::Object> optionsObj;
    if (info.Length() > 0 && info[0]->IsObject()) {
      optionsObj = info[0].As<v8::Object>();
    }
    info.GetReturnValue().Set(Batch::NewInstance(info.This(), optionsObj));
    return;
  }

  LD_METHOD_SETUP_COMMON(batch, 1, 2);

  bool sync = BooleanOptionValue(optionsObj, "sync");

  v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(info[0]);

  leveldb::WriteBatch* batch = new leveldb::WriteBatch();
  bool hasData = false;

  for (unsigned int i = 0; i < array->Length(); i++) {
    if (!array->Get(i)->IsObject())
      continue;

    v8::Local<v8::Object> obj = v8::Local<v8::Object>::Cast(array->Get(i));
    v8::Local<v8::Value> keyBuffer = obj->Get(Nan::New("key").ToLocalChecked());
    v8::Local<v8::Value> type = obj->Get(Nan::New("type").ToLocalChecked());

    if (type->StrictEquals(Nan::New("del").ToLocalChecked())) {
      leveldb::Slice key = MakeSlice(keyBuffer);

      batch->Delete(key);
      if (!hasData)
        hasData = true;
    } else if (type->StrictEquals(Nan::New("put").ToLocalChecked())) {
      v8::Local<v8::Value> valueBuffer = obj->Get(Nan::New("value").ToLocalChecked());

      leveldb::Slice key = MakeSlice(keyBuffer);
      leveldb::Slice value = MakeSlice(valueBuffer);

      batch->Put(key, value);
      if (!hasData)
        hasData = true;
    }
  }

  // don't allow an empty batch through
  if (hasData) {
    BatchWorker* worker = new BatchWorker(
        database
      , new Nan::Callback(callback)
      , batch
      , sync
    );
    // persist to prevent accidental GC
    v8::Local<v8::Object> _this = info.This();
    worker->SaveToPersistent("database", _this);
    Nan::AsyncQueueWorker(worker);
  } else {
    LD_RUN_CALLBACK("leveldown:db.batch", callback, 0, nullptr);
  }
}

NAN_METHOD(Database::PutMany) {
  LD_METHOD_SETUP_COMMON(batch, 1, 2);

  bool sync = BooleanOptionValue(optionsObj, "sync");

  auto array = v8::Local<v8::Array>::Cast(info[0]);
  auto batch = new leveldb::WriteBatch();
  const auto len = array->Length();

  for (unsigned int i = 0; i < len; i++) {
    auto key_value = v8::Local<v8::Array>::Cast(array->Get(i));

    leveldb::Slice key = MakeSlice(key_value->Get(0));
    leveldb::Slice value = MakeSlice(key_value->Get(1));

    batch->Put(key, value);
  }

  // don't allow an empty batch through
  if (len > 0) {
    BatchWorker* worker = new BatchWorker(
        database
      , new Nan::Callback(callback)
      , batch
      , sync
    );
    // persist to prevent accidental GC
    v8::Local<v8::Object> _this = info.This();
    worker->SaveToPersistent("database", _this);
    Nan::AsyncQueueWorker(worker);
  } else {
    LD_RUN_CALLBACK("leveldown:db.batch", callback, 0, nullptr);
  }
}


NAN_METHOD(Database::ApproximateSize) {
  leveldb::Slice start = MakeSlice(info[0].As<v8::Object>());
  leveldb::Slice end = MakeSlice(info[1].As<v8::Object>());

  LD_METHOD_SETUP_COMMON(approximateSize, -1, 2)

  ApproximateSizeWorker* worker  = new ApproximateSizeWorker(
      database
    , new Nan::Callback(callback)
    , start
    , end
  );
  // persist to prevent accidental GC
  v8::Local<v8::Object> _this = info.This();
  worker->SaveToPersistent("database", _this);
  Nan::AsyncQueueWorker(worker);
}

NAN_METHOD(Database::CompactRange) {
  leveldb::Slice start = MakeSlice(info[0].As<v8::Object>());
  leveldb::Slice end = MakeSlice(info[1].As<v8::Object>());

  LD_METHOD_SETUP_COMMON(compactRange, -1, 2)

  CompactRangeWorker* worker  = new CompactRangeWorker(
      database
    , new Nan::Callback(callback)
    , start
    , end
  );
  // persist to prevent accidental GC
  v8::Local<v8::Object> _this = info.This();
  worker->SaveToPersistent("database", _this);
  Nan::AsyncQueueWorker(worker);
}

NAN_METHOD(Database::GetProperty) {
  leveldb::Slice property = MakeSliceFromString(info[0].As<v8::Object>());
  leveldown::Database* database =
      Nan::ObjectWrap::Unwrap<leveldown::Database>(info.This());

  std::string* value = new std::string();
  database->GetPropertyFromDatabase(property, value);
  v8::Local<v8::String> returnValue
      = Nan::New<v8::String>(value->c_str(), value->length()).ToLocalChecked();
  delete value;
  delete[] property.data();

  info.GetReturnValue().Set(returnValue);
}

NAN_METHOD(Database::Iterator) {
  Database* database = Nan::ObjectWrap::Unwrap<Database>(info.This());

  v8::Local<v8::Object> optionsObj;
  if (info.Length() > 0 && info[0]->IsObject()) {
    optionsObj = v8::Local<v8::Object>::Cast(info[0]);
  }

  // each iterator gets a unique id for this Database, so we can
  // easily store & lookup on our `iterators` map
  uint32_t id = database->currentIteratorId++;
  Nan::TryCatch try_catch;
  v8::Local<v8::Object> iteratorHandle = Iterator::NewInstance(
      info.This()
    , Nan::New<v8::Number>(id)
    , optionsObj
  );
  if (try_catch.HasCaught()) {
    // NB: node::FatalException can segfault here if there is no room on stack.
    return Nan::ThrowError("Fatal Error in Database::Iterator!");
  }

  leveldown::Iterator *iterator =
      Nan::ObjectWrap::Unwrap<leveldown::Iterator>(iteratorHandle);

  database->iterators[id] = iterator;

  // register our iterator
  /*
  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  obj->Set(Nan::New("iterator"), iteratorHandle);
  Nan::Persistent<v8::Object> persistent;
  persistent.Reset(nan_isolate, obj);
  database->iterators.insert(std::pair< uint32_t, Nan::Persistent<v8::Object> & >
      (id, persistent));
  */

  info.GetReturnValue().Set(iteratorHandle);
}


} // namespace leveldown

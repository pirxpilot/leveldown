/* Copyright (c) 2012-2018 LevelDOWN contributors
 * See list at <https://github.com/level/leveldown#contributing>
 * MIT License <https://github.com/level/leveldown/blob/master/LICENSE.md>
 */

#ifndef LD_DATABASE_H
#define LD_DATABASE_H

#include <map>
#include <vector>
#include <node.h>

#include <leveldb/cache.h>
#include <leveldb/db.h>
#include <leveldb/filter_policy.h>
#include <nan.h>

#include "leveldown.h"
#include "iterator.h"

namespace leveldown {

NAN_METHOD(LevelDOWN);

struct Reference {
  Nan::Persistent<v8::Object> handle;
  leveldb::Slice slice;

  Reference(v8::Local<v8::Value> obj, leveldb::Slice slice) : slice(slice) {
    v8::Local<v8::Object> _obj = Nan::New<v8::Object>();
    _obj->Set(Nan::New("obj").ToLocalChecked(), obj);
    handle.Reset(_obj);
  };
};

class Database : public Nan::ObjectWrap {
public:
  static void Init ();
  static v8::Local<v8::Value> NewInstance (v8::Local<v8::String> &location);

  leveldb::Status OpenDatabase (const leveldb::Options& options) {
    leveldb::DB* pdb;
    auto status = leveldb::DB::Open(options, location, &pdb);
    db.reset(pdb);
    return status;
  }

  leveldb::Status PutToDatabase (
      const leveldb::WriteOptions& options
    , leveldb::Slice key
    , leveldb::Slice value
  ) {
    return db->Put(options, key, value);
  }

  leveldb::Status GetFromDatabase (
      const leveldb::ReadOptions& options
    , leveldb::Slice key
    , std::string* value
  ) {
    return db->Get(options, key, value);
  }

  leveldb::Status DeleteFromDatabase (
      const leveldb::WriteOptions& options
    , leveldb::Slice key
  ) {
    return db->Delete(options, key);
  }

  leveldb::Status WriteBatchToDatabase (
      const leveldb::WriteOptions& options
    , leveldb::WriteBatch* batch
  ) {
    return db->Write(options, batch);
  }

  uint64_t ApproximateSizeFromDatabase (const leveldb::Range* range) {
    uint64_t size;
    db->GetApproximateSizes(range, 1, &size);
    return size;
  }

  void CompactRangeFromDatabase (
      const leveldb::Slice* start
    , const leveldb::Slice* end
  ) {
    db->CompactRange(start, end);
  }

  void GetPropertyFromDatabase (
      const leveldb::Slice& property
    , std::string* value
  ) {
    db->GetProperty(property, value);
  }

  leveldb::Iterator* NewIterator (const leveldb::ReadOptions& options) {
    return db->NewIterator(options);
  }

  const leveldb::Snapshot* NewSnapshot () {
    return db->GetSnapshot();
  }

  void ReleaseSnapshot (const leveldb::Snapshot* snapshot) {
    return db->ReleaseSnapshot(snapshot);
  }

  void CloseDatabase () {
    db.reset();
    blockCache.reset();
    filterPolicy.reset();
  }

  // called each time an Iterator is End()ed, in the main thread
  // we have to remove our reference to it and if it's the last iterator
  // we have to invoke a pending CloseWorker if there is one
  // if there is a pending CloseWorker it means that we're waiting for
  // iterators to end before we can close them
  void ReleaseIterator (uint32_t id) {
    iterators.erase(id);
    if (iterators.empty() && pendingCloseWorker != nullptr) {
      Nan::AsyncQueueWorker(pendingCloseWorker);
      pendingCloseWorker = nullptr;
    }
  }

private:
  Database (const char* location)
    : location(location)
    , currentIteratorId(0)
    , pendingCloseWorker(nullptr)
  {}


  std::string location;
  std::unique_ptr<leveldb::DB> db;
  uint32_t currentIteratorId;
  std::unique_ptr<leveldb::Cache> blockCache;
  std::unique_ptr<const leveldb::FilterPolicy> filterPolicy;

  Nan::AsyncWorker *pendingCloseWorker;
  std::map< uint32_t, leveldown::Iterator * > iterators;

  static void WriteDoing(uv_work_t *req);
  static void WriteAfter(uv_work_t *req);

  static NAN_METHOD(New);
  static NAN_METHOD(Open);
  static NAN_METHOD(Close);
  static NAN_METHOD(Put);
  static NAN_METHOD(PutMany);
  static NAN_METHOD(Delete);
  static NAN_METHOD(Get);
  static NAN_METHOD(GetMany);
  static NAN_METHOD(Batch);
  static NAN_METHOD(Write);
  static NAN_METHOD(Iterator);
  static NAN_METHOD(ApproximateSize);
  static NAN_METHOD(CompactRange);
  static NAN_METHOD(GetProperty);
};

} // namespace leveldown

#endif

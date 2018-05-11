/* Copyright (c) 2012-2018 LevelDOWN contributors
 * See list at <https://github.com/level/leveldown#contributing>
 * MIT License <https://github.com/level/leveldown/blob/master/LICENSE.md>
 */

#ifndef LD_DATABASE_ASYNC_H
#define LD_DATABASE_ASYNC_H

#include <vector>
#include <node.h>

#include <leveldb/cache.h>

#include "async.h"

namespace leveldown {

class OpenWorker : public AsyncWorker {
public:
  OpenWorker(Database *database,
             Nan::Callback *callback,
             leveldb::Cache* blockCache,
             const leveldb::FilterPolicy* filterPolicy,
             bool createIfMissing,
             bool errorIfExists,
             bool compression,
             uint32_t writeBufferSize,
             uint32_t blockSize,
             uint32_t maxOpenFiles,
             uint32_t blockRestartInterval,
             uint32_t maxFileSize);

  virtual void Execute();

private:
  leveldb::Options options;
};

class CloseWorker : public AsyncWorker {
public:
  CloseWorker(Database *database, Nan::Callback *callback);

  virtual void Execute();
  virtual void WorkComplete();
};

class IOWorker : public AsyncWorker {
public:
  IOWorker(Database *database,
           Nan::Callback *callback,
           const char *resource_name,
           leveldb::Slice key);

protected:
  leveldb::Slice key;
};

class ReadWorker : public IOWorker {
public:
  ReadWorker(Database *database,
             Nan::Callback *callback,
             leveldb::Slice key,
             bool asBuffer,
             bool fillCache);

  virtual void Execute();
  virtual void HandleOKCallback();

private:
  bool asBuffer;
  leveldb::ReadOptions options;
  std::string value;
};

class ReadManyWorker : public AsyncWorker {
public:
  ReadManyWorker(Database *database,
                 Nan::Callback *callback,
                 std::vector<leveldb::Slice> &&keys_):
    AsyncWorker(database, callback, "leveldown:db.getMany"),
    keys(std::move(keys_)),
    values(keys.size()) {};

  virtual void Execute();
  virtual void HandleOKCallback();

private:
  leveldb::ReadOptions options;
  const std::vector<leveldb::Slice> keys;
  std::vector<std::string> values;
  std::vector<unsigned int> missing;
};

class DeleteWorker : public IOWorker {
public:
  DeleteWorker(Database *database,
               Nan::Callback *callback,
               leveldb::Slice key,
               bool sync,
               const char *resource_name = "leveldown:db.del");

  virtual void Execute();

protected:
  leveldb::WriteOptions options;
};

class WriteWorker : public DeleteWorker {
public:
  WriteWorker(Database *database,
              Nan::Callback *callback,
              leveldb::Slice key,
              leveldb::Slice value,
              bool sync);

  virtual void Execute();

private:
  leveldb::Slice value;
};

class BatchWorker : public AsyncWorker {
public:
  BatchWorker(Database *database,
              Nan::Callback *callback,
              leveldb::WriteBatch* batch,
              bool sync);

  virtual void Execute();

private:
  leveldb::WriteOptions options;
  std::unique_ptr<leveldb::WriteBatch> batch;
};

class ApproximateSizeWorker : public AsyncWorker {
public:
  ApproximateSizeWorker(Database *database,
                        Nan::Callback *callback,
                        leveldb::Slice start,
                        leveldb::Slice end);

  virtual void Execute();
  virtual void HandleOKCallback();

  private:
    leveldb::Range range;
    uint64_t size;
};

class CompactRangeWorker : public AsyncWorker {
public:
  CompactRangeWorker(Database *database,
                     Nan::Callback *callback,
                     leveldb::Slice start,
                     leveldb::Slice end);

  virtual void Execute();
  virtual void HandleOKCallback();

  private:
    leveldb::Slice rangeStart;
    leveldb::Slice rangeEnd;
};


} // namespace leveldown

#endif

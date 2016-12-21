#include "storage/db.h"

#include <leveldb/options.h>
#include <leveldb/status.h>

#include "skywalker/logging.h"

namespace {
const uint64_t kMinChosenKey = UINTMAX_MAX;
const uint64_t kMasterVariables = (UINTMAX_MAX - 1);
const uint64_t kMembership = (UINTMAX_MAX - 2);
}

namespace skywalker {

DB::DB()
    : db_(nullptr) {
}

DB::~DB() {
  delete db_;
}

int DB::Open(uint32_t group_id, const std::string& name) {
  leveldb::Options options;
  options.create_if_missing = true;
  options.write_buffer_size = 1024 * 1024 + group_id * 10 * 1024;
  leveldb::Status status = leveldb::DB::Open(options, name, &db_);
  if (!status.ok()) {
    SWLog(ERROR, "DB::Open - %s\n", status.ToString().c_str());
    return -1;
  }
  return 0;
}

int DB::Put(const WriteOptions& options,
            uint64_t instance_id,
            const std::string& value) {
  char key[8];
  memcpy(key, &instance_id, sizeof(uint64_t));
  leveldb::WriteOptions op;
  op.sync = options.sync;
  leveldb::Status status = db_->Put(op, key, value);
  if (!status.ok()) {
    SWLog(ERROR, "DB::Put - %s\n", status.ToString().c_str());
    return -1;
  }
  return 0;
}

int DB::Delete(const WriteOptions& options, uint64_t instance_id) {
  char key[8];
  memcpy(key, &instance_id, sizeof(uint64_t));
  leveldb::WriteOptions op;
  op.sync = options.sync;
  leveldb::Status status = db_->Delete(op, key);
  if (!status.ok()) {
    SWLog(ERROR, "DB::Delete - %s\n", status.ToString().c_str());
    return -1;
  }
  return 0;
}

int DB::Get(uint64_t instance_id, std::string* value) {
  char key[8];
  int ret = 0;
  memcpy(key, &instance_id, sizeof(uint64_t));
  leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, value);
  if (!status.ok()) {
    if (status.IsNotFound()) {
      ret = 1;
    } else {
      ret = -1;
      SWLog(ERROR, "DB::Get - %s\n", status.ToString().c_str());
    }
  }
  return ret;
}

int DB::GetMaxInstanceId(uint64_t* instance_id) {
  int ret = 1;
  leveldb::Iterator* it = db_->NewIterator(leveldb::ReadOptions());
  it->SeekToLast();
  while (it->Valid()) {
    memcpy(instance_id, it->key().data(), sizeof(uint64_t));
    if(*instance_id == kMasterVariables ||
       *instance_id == kMinChosenKey ||
       *instance_id == kMembership) {
      it->Prev();
    } else {
      ret = 0;
      break;
    }
  }
  delete it;
  return ret;
}

int DB::SetMinChosenInstanceId(uint64_t id) {
  char value[8];
  memcpy(value, &id, sizeof(uint64_t));
  return Put(WriteOptions(), kMinChosenKey, value);
}

int DB::GetMinChosenInstanceId(uint64_t* id) {
  std::string value;
  int ret = Get(kMinChosenKey, &value);
  if (ret == 0) {
    memcpy(id, &*(value.data()), sizeof(uint64_t));
  }
  return ret;
}

int DB::SetMembership(const Membership& m) {
  std::string s;
  if (!m.SerializeToString(&s)) {
    SWLog(ERROR, "DB::SetMembership - m.SerializeToString failed!\n");
    return -1;
  }
  return Put(WriteOptions(), kMembership, s);
}

int DB::GetMembership(Membership* m) {
  std::string s;
  int ret = Get(kMembership, &s);
  if (ret != 0) {
    return ret;
  }
  if (m->ParseFromString(s)) {
    return 0;
  } else {
    SWLog(ERROR, "DB::GetMembership - m.ParseFromString failed!\n");
    return -1;
  }
}

int DB::SetMasterVariables(const std::string& s) {
  return Put(WriteOptions(), kMasterVariables, s);
}

int DB::GetMasterVariavles(std::string* s) {
  return Get(kMasterVariables, s);
}

}  // namespace skywalker

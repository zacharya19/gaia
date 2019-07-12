// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "mr/impl/local_context.h"

namespace mr3 {

using namespace std;

namespace detail {

/// Thread-local buffered writer, owned by LocalContext.
class BufferedWriter {
  DestHandle* dh_;

 public:
  // dh not owned by BufferedWriter.
  BufferedWriter(DestHandle* dh, bool is_binary);

  BufferedWriter(const BufferedWriter&) = delete;
  ~BufferedWriter();

  void Flush();

  void Write(string&& val);

 private:
  static constexpr size_t kFlushLimit = 1 << 13;

  void operator=(const BufferedWriter&) = delete;

  bool is_binary_;

  string buffer_;
  vector<string> items_;

  size_t writes = 0, flushes = 0;
  DestHandle::StringGenCb str_cb_;
};

BufferedWriter::BufferedWriter(DestHandle* dh, bool is_binary) : dh_(dh), is_binary_(is_binary) {
  if (is_binary) {
    str_cb_ = [this]() mutable -> absl::optional<std::string> {
      if (items_.empty()) {
        return absl::nullopt;
      }
      string val = std::move(items_.back());
      items_.pop_back();
      return val;
    };
  } else {
    str_cb_ = [ptr = &buffer_]() mutable -> absl::optional<std::string> {
      string* val = ptr;
      ptr = nullptr;
      return val ? absl::optional<std::string>{std::move(*val)} : absl::nullopt;
    };
  }
}

BufferedWriter::~BufferedWriter() { CHECK(buffer_.empty()); }

void BufferedWriter::Flush() {
  if (buffer_.empty() && items_.empty())
    return;

  dh_->Write(str_cb_);
}

void BufferedWriter::Write(string&& val) {
  if (is_binary_) {
    items_.push_back(std::move(val));
  } else {
    buffer_.append(val).append("\n");
  }
  VLOG_IF(2, ++writes % 1000 == 0) << "BufferedWrite " << writes;

  if (buffer_.size() >= kFlushLimit) {
    VLOG(2) << "Flush " << ++flushes;

    dh_->Write(str_cb_);
    buffer_.clear();
  }
}

LocalContext::LocalContext(DestFileSet* mgr) : mgr_(mgr) { CHECK(mgr_); }

void LocalContext::WriteInternal(const ShardId& shard_id, std::string&& record) {
  auto it = custom_shard_files_.find(shard_id);
  if (it == custom_shard_files_.end()) {
    DestHandle* res = mgr_->GetOrCreate(shard_id);
    bool is_binary = mgr_->output().format().type() == pb::WireFormat::LST;
    it = custom_shard_files_.emplace(shard_id, new BufferedWriter{res, is_binary}).first;
  }
  it->second->Write(std::move(record));
}

void LocalContext::Flush() {
  for (auto& k_v : custom_shard_files_) {
    k_v.second->Flush();
  }
}

void LocalContext::CloseShard(const ShardId& shard_id) {
  auto it = custom_shard_files_.find(shard_id);
  if (it == custom_shard_files_.end()) {
    LOG(ERROR) << "Could not find shard " << shard_id.ToString("shard");
    return;
  }
  BufferedWriter* bw = it->second;
  bw->Flush();
  mgr_->CloseHandle(shard_id);
}

LocalContext::~LocalContext() {
  for (auto& k_v : custom_shard_files_) {
    delete k_v.second;
  }
  VLOG(1) << "~LocalContextEnd";
}

}  // namespace detail
}  // namespace mr3
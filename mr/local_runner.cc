// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "mr/local_runner.h"

#include "absl/strings/str_cat.h"
#include "base/logging.h"
#include "file/file_util.h"
#include "file/gzip_file.h"
#include "util/fibers/fiberqueue_threadpool.h"

namespace mr3 {

using namespace util;
using namespace boost;
using namespace std;

namespace {

static string FileName(StringPiece base, const pb::Output& out) {
  CHECK_EQ(out.format().type(), pb::WireFormat::TXT);
  string res(base);
  absl::StrAppend(&res, ".txt");
  if (out.has_compress()) {
    if (out.compress().type() == pb::Output::GZIP) {
      absl::StrAppend(&res, ".gz");
    } else {
      LOG(FATAL) << "Not supported " << out.compress().ShortDebugString();
    }
  }
  return res;
}

static file::WriteFile* CreateFile(const std::string& path, const pb::Output& out,
                                   fibers_ext::FiberQueueThreadPool* fq) {
  std::function<file::WriteFile*()> cb;
  if (out.has_compress()) {
    if (out.compress().type() == pb::Output::GZIP) {
      file::WriteFile* gzres{file::GzipFile::Create(path, out.compress().level())};
      cb = [gzres] {
        CHECK(gzres->Open());
        return gzres;
      };
    } else {
      LOG(FATAL) << "Not supported " << out.compress().ShortDebugString();
    }
  } else {
    cb = [&] { return file::Open(path); };
  }
  file::WriteFile* res = fq->Await(std::move(cb));
  CHECK(res);
  return res;
}

class GlobalDestFileManager {
  const string root_dir_;

  fibers_ext::FiberQueueThreadPool* fq_;
  google::dense_hash_map<StringPiece, ::file::WriteFile*> dest_files_;
  UniqueStrings str_db_;
  fibers::mutex mu_;

 public:
  using Result = std::pair<StringPiece, ::file::WriteFile*>;

  GlobalDestFileManager(const std::string& root_dir, fibers_ext::FiberQueueThreadPool* fq);
  ~GlobalDestFileManager();

  Result Get(StringPiece key, const pb::Output& out);

  fibers_ext::FiberQueueThreadPool* pool() { return fq_; }
};

class LocalContext : public RawContext {
 public:
  explicit LocalContext(const pb::Output& output, GlobalDestFileManager* mgr);
  ~LocalContext();

  void Flush();

 private:
  void WriteInternal(const ShardId& shard_id, std::string&& record) final;

  struct BufferedWriter;

  google::dense_hash_map<StringPiece, BufferedWriter*> custom_shard_files_;

  const pb::Output& output_;
  GlobalDestFileManager* mgr_;
};

GlobalDestFileManager::GlobalDestFileManager(const std::string& root_dir,
                                             fibers_ext::FiberQueueThreadPool* fq)
    : root_dir_(root_dir), fq_(fq) {
  dest_files_.set_empty_key(StringPiece());
}

auto GlobalDestFileManager::Get(StringPiece key, const pb::Output& out) -> Result {
  CHECK_EQ(out.shard_type(), pb::Output::USER_DEFINED);

  std::lock_guard<fibers::mutex> lk(mu_);
  auto it = dest_files_.find(key);
  if (it == dest_files_.end()) {
    string file_name = FileName(key, out);
    key = str_db_.Get(key);
    string full_path = file_util::JoinPath(root_dir_, file_name);
    auto res = dest_files_.emplace(key, CreateFile(full_path, out, fq_));
    CHECK(res.second && res.first->second);
    it = res.first;
  }

  return Result(it->first, it->second);
}

GlobalDestFileManager::~GlobalDestFileManager() {
  for (auto& k_v : dest_files_) {
    CHECK(k_v.second->Close());
  }
}

struct LocalContext::BufferedWriter {
  file::WriteFile* wr;
  std::string buffer;
  unsigned index;

  static constexpr size_t kFlushLimit = 1 << 16;
  void operator=(const BufferedWriter&) = delete;

  explicit BufferedWriter(file::WriteFile* wf, unsigned i) : wr(wf), index(i) {}
  BufferedWriter(const BufferedWriter&) = delete;

  void Flush(fibers_ext::FiberQueueThreadPool* fq) {
    if (buffer.empty())
      return;
    auto status = fq->Await(index, [b = std::move(buffer), wr = this->wr] { return wr->Write(b); });
    CHECK_STATUS(status);
  }

  ~BufferedWriter();

  void Write(std::initializer_list<StringPiece> src, fibers_ext::FiberQueueThreadPool* fq);
};

LocalContext::BufferedWriter::~BufferedWriter() { CHECK(buffer.empty()); }

void LocalContext::BufferedWriter::Write(std::initializer_list<StringPiece> src,
                                         fibers_ext::FiberQueueThreadPool* fq) {
  for (auto v : src) {
    buffer.append(v.data(), v.size());
  }
  if (buffer.size() >= kFlushLimit) {
    fq->Add(index, [b = std::move(buffer), wr = this->wr] {
      auto status = wr->Write(b);
      CHECK_STATUS(status);
    });
  }
}

LocalContext::LocalContext(const pb::Output& out, GlobalDestFileManager* mgr)
    : output_(out), mgr_(mgr) {
  custom_shard_files_.set_empty_key(StringPiece{});
}

void LocalContext::WriteInternal(const ShardId& shard_id, std::string&& record) {
  CHECK(absl::holds_alternative<string>(shard_id));

  const string& shard_name = absl::get<string>(shard_id);

  auto it = custom_shard_files_.find(shard_name);
  if (it == custom_shard_files_.end()) {
    auto res = mgr_->Get(shard_name, output_);
    StringPiece key = res.first;

    unsigned index =
        base::MurmurHash3_x86_32(reinterpret_cast<const uint8_t*>(key.data()), key.size(), 1);
    it = custom_shard_files_.emplace(res.first, new BufferedWriter{res.second, index}).first;
  }
  it->second->Write({record, "\n"}, mgr_->pool());
}

void LocalContext::Flush() {
  for (auto& k_v : custom_shard_files_) {
    k_v.second->Flush(mgr_->pool());
  }
}

LocalContext::~LocalContext() {
  for (auto& k_v : custom_shard_files_) {
    delete k_v.second;
  }
  VLOG(1) << "~LocalContextEnd";
}

}  // namespace

struct LocalRunner::Impl {
  string data_dir;
  std::unique_ptr<GlobalDestFileManager> dest_mgr;
  fibers_ext::FiberQueueThreadPool fq_pool;

  Impl(const string& d) : data_dir(d), fq_pool(0, 128) {}
};

LocalRunner::LocalRunner(const std::string& data_dir) : impl_(new Impl(data_dir)) {}

LocalRunner::~LocalRunner() {}

void LocalRunner::Init() {
  file_util::RecursivelyCreateDir(impl_->data_dir, 0750);
  impl_->dest_mgr.reset(new GlobalDestFileManager(impl_->data_dir, &impl_->fq_pool));
}

void LocalRunner::Shutdown() {}

RawContext* LocalRunner::CreateContext(const pb::Operator& op) {
  return new LocalContext(op.output(), impl_->dest_mgr.get());
}

void LocalRunner::ExpandGlob(const std::string& glob, std::function<void(const std::string&)> cb) {}

// Read file and fill queue. This function must be fiber-friendly.
void LocalRunner::ProcessFile(const std::string& filename, pb::WireFormat::Type type,
                              RecordQueue* queue) {}

}  // namespace mr3
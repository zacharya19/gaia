// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "mr/impl/dest_file_set.h"

#include "absl/strings/str_cat.h"
#include "base/logging.h"
#include "file/file_util.h"
#include "file/gzip_file.h"

namespace mr3 {
namespace impl {

// For some reason enabling local_runner_zsink as true performs slower than
// using GzipFile in the threadpool. I think there is something to dig here and I think
// compress operations should not be part of the FiberQueueThreadPool workload but after spending
// quite some time I am giving up.
// TODO: to implement compress directly using zlib interface an not using zlibsink/stringsink
// abstractions.
DEFINE_bool(local_runner_zsink, false, "");

using namespace boost;
using namespace std;
using namespace util;

namespace {

constexpr size_t kBufLimit = 1 << 16;

string FileName(StringPiece base, const pb::Output& out) {
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

inline auto WriteCb(std::string&& s, file::WriteFile* wf) {
  return [b = std::move(s), wf] {
    auto status = wf->Write(b);
    CHECK_STATUS(status);
  };
}

file::WriteFile* CreateFile(const std::string& path, const pb::Output& out,
                            fibers_ext::FiberQueueThreadPool* fq) {
  std::function<file::WriteFile*()> cb;
  if (out.has_compress() && !FLAGS_local_runner_zsink) {
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

}  // namespace

DestFileSet::DestFileSet(const std::string& root_dir, fibers_ext::FiberQueueThreadPool* fq)
    : root_dir_(root_dir), fq_(fq) {
  dest_files_.set_empty_key(StringPiece());
}

auto DestFileSet::Get(StringPiece key, const pb::Output& pb_out) -> Result {
  CHECK_EQ(pb_out.shard_spec().type(), pb::ShardSpec::USER_DEFINED);

  std::lock_guard<fibers::mutex> lk(mu_);
  auto it = dest_files_.find(key);
  if (it == dest_files_.end()) {
    string file_name = FileName(key, pb_out);
    key = str_db_.Get(key);
    string full_path = file_util::JoinPath(root_dir_, file_name);
    unsigned index =
        base::MurmurHash3_x86_32(reinterpret_cast<const uint8_t*>(key.data()), key.size(), 1);
    DestHandle* dh = new DestHandle{CreateFile(full_path, pb_out, fq_), index, fq_};

    if (FLAGS_local_runner_zsink && pb_out.has_compress()) {
      CHECK(pb_out.compress().type() == pb::Output::GZIP);

      static std::default_random_engine rnd;

      dh->str_sink = new StringSink;
      dh->zlib_sink.reset(new ZlibSink(dh->str_sink, pb_out.compress().level()));

      // Randomize when we flush first for each handle. That should define uniform flushing cycle
      // for all handles.
      dh->start_delta_ = rnd() % (kBufLimit - 1);
    }
    auto res = dest_files_.emplace(key, dh);
    CHECK(res.second);
    it = res.first;
  }

  return Result(it->first, it->second);
}

void DestFileSet::Flush() {
  for (auto& k_v : dest_files_) {
    DestHandle* dh = k_v.second;
    if (dh->zlib_sink) {
      CHECK_STATUS(dh->zlib_sink->Flush());

      if (!dh->str_sink->contents().empty()) {
        fq_->Add(dh->fq_index_, WriteCb(std::move(dh->str_sink->contents()), dh->wf_));
      }
    }
    VLOG(1) << "Closing file "  << k_v.first;

    bool res = fq_->Await(dh->fq_index_, [wf = dh->wf_] { return wf->Close(); });
    CHECK(res);
    dh->wf_ = nullptr;
  }
}

DestFileSet::~DestFileSet() {
  for (auto& k_v : dest_files_) {
    delete k_v.second;
  }
}

DestHandle::DestHandle(::file::WriteFile* wf, unsigned index, fibers_ext::FiberQueueThreadPool* fq)
    : wf_(wf), fq_index_(index), fq_(fq) {
  CHECK(wf && fq_);
}

void DestHandle::Write(string str) {
  if (zlib_sink) {
    std::unique_lock<fibers::mutex> lk(zmu_);
    CHECK_STATUS(zlib_sink->Append(strings::ToByteRange(str)));
    str.clear();
    if (str_sink->contents().size() >= kBufLimit - start_delta_) {
      fq_->Add(fq_index_, WriteCb(std::move(str_sink->contents()), wf_));
      start_delta_ = 0;
    }
    return;
  }
  fq_->Add(fq_index_, WriteCb(std::move(str), wf_));
}

}  // namespace impl
}  // namespace mr3

// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include <boost/fiber/buffered_channel.hpp>
#include <boost/fiber/fiber.hpp>
#include <functional>

#include "mr/pipeline.h"

#include "util/asio/io_context_pool.h"
#include "util/fibers/fiberqueue_threadpool.h"

namespace mr3 {

class Pipeline::Executor {
  using StringQueue = util::fibers_ext::SimpleChannel<std::string>;
  using FileNameQueue = ::boost::fibers::buffered_channel<std::string>;

  struct PerIoStruct {
    unsigned index;
    ::boost::fibers::fiber map_fd, process_fd;

    StringQueue record_q;
    std::unique_ptr<RawContext> do_context;
    bool stop_early = false;

    PerIoStruct(unsigned i);

    void Shutdown();
  };

 public:
  Executor(util::IoContextPool* pool, Runner* runner);
  ~Executor();

  void Init();
  void Run(const InputBase* input, TableBase* ss);

  // Stops the executor in the middle.
  void Stop();

  void Shutdown();

 private:
  // External, disk thread that reads files from disk and pumps data into record_q.
  // One per IO thread.
  void ProcessFiles(pb::WireFormat::Type tp);
  uint64_t ProcessText(file::ReadonlyFile* fd);

  void MapFiber(TableBase* sb);

  util::IoContextPool* pool_;
  FileNameQueue file_name_q_;
  std::unique_ptr<Runner> runner_;

  static thread_local std::unique_ptr<PerIoStruct> per_io_;
};

}  // namespace mr3

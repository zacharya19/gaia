// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include "mr/runner.h"

namespace mr3 {

class LocalRunner : public Runner {
 public:
  LocalRunner(const std::string& data_dir);
  ~LocalRunner();

  void Init() final;

  void Shutdown() final;

  void OperatorStart() final;

  // Must be thread-safe. Called from multiple threads in pipeline_executor.
  RawContext* CreateContext(const pb::Operator& op) final;

  void OperatorEnd(ShardFileMap* out_files) final;

  void ExpandGlob(const std::string& glob, std::function<void(const std::string&)> cb) final;

  // Read file and fill queue. This function must be fiber-friendly.
  size_t ProcessInputFile(const std::string& filename, pb::WireFormat::Type type,
                          std::function<void(std::string&&)> cb) final;

  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};


}  // namespace mr3

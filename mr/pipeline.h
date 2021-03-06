// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include "mr/mr.h"

#include "absl/container/flat_hash_map.h"

namespace util {
class IoContextPool;
}  // namespace util

namespace mr3 {
class Runner;
class OperatorExecutor;

template <typename U, typename Joiner, typename Out, typename S>
detail::HandlerBinding<Joiner, Out> JoinInput(const PTable<U>& tbl,
                                              EmitMemberFn<S, Joiner, Out> ptr) {
  return tbl.BindWith(ptr);
}

class Pipeline {
  friend class detail::TableBase;

 public:
  explicit Pipeline(util::IoContextPool* pool);
  ~Pipeline();

  StringTable ReadText(const std::string& name, const std::vector<std::string>& globs);

  StringTable ReadText(const std::string& name, const std::string& glob) {
    return ReadText(name, std::vector<std::string>{glob});
  }

  void Run(Runner* runner);

  // Stops/breaks the run.
  void Stop();

  template <typename JoinerType, typename Out>
  PTable<Out> Join(const std::string& name,
                   std::initializer_list<detail::HandlerBinding<JoinerType, Out>> args);

 private:
  const InputBase* CheckedInput(const std::string& name) const;

  typename detail::TableBase* CreateTableImpl(const std::string& name) {
    return new detail::TableBase(name, this);
  }

  util::IoContextPool* pool_;
  absl::flat_hash_map<std::string, std::unique_ptr<InputBase>> inputs_;
  std::vector<detail::TableBase*> tables_;

  ::boost::fibers::mutex mu_;
  std::unique_ptr<OperatorExecutor> executor_;
};

template <typename JoinerType, typename OutT>
PTable<OutT> Pipeline::Join(const std::string& name,
                            std::initializer_list<detail::HandlerBinding<JoinerType, OutT>> args) {
  detail::TableBase* ptr = CreateTableImpl(name);
  std::for_each(args.begin(), args.end(), [&](const auto& arg) {
    ptr->mutable_op()->add_input_name(arg.tbase()->op().output().name());
  });
  ptr->mutable_op()->set_type(pb::Operator::HASH_JOIN);

  std::vector<RawSinkMethodFactory<JoinerType, OutT>> factories;
  for (auto& a : args) {
    factories.push_back(a.factory());
  }

  auto* res = detail::TableImpl<OutT>::template AsJoin<JoinerType>(ptr, std::move(factories));
  return PTable<OutT>{res};
}

}  // namespace mr3

workspace(name = "gaia")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "353e41e99f93c0219994a7c4402a80cd04bd044703818d199c66ff82ec4ee85b",
    strip_prefix = "rules_foreign_cc-master",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/master.zip",
)

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository", "new_git_repository")

git_repository(
    name = "com_github_gflags_gflags",
    remote = "https://github.com/gflags/gflags.git",
    tag = "v2.2.2",
)

git_repository(
    name = "com_github_glog",
    branch = "Prod",
    remote = "https://github.com/romange/glog.git",
)

new_git_repository(
    name = "pmr",
    build_file = "cmake.BUILD",
    commit = "6ca661f421840a7117394c25099f615630b39a04",
    remote = "https://github.com/romange/pmr.git",
)

new_git_repository(
    name = "xxhash",
    build_file = "xxhash.BUILD",
    remote = "https://github.com/Cyan4973/xxHash.git",
    tag = "v0.7.0",
)

git_repository(
    name = "com_google_absl",
    commit = "aa468ad75539619b47979911297efbb629c52e44",
    remote = "https://github.com/abseil/abseil-cpp",
)

git_repository(
    name = "com_google_benchmark",
    remote = "https://github.com/google/benchmark.git",
    tag = "v1.5.0",
)

git_repository(
    name = "com_google_gtest",
    remote = "https://github.com/google/googletest.git",
    tag = "release-1.8.1",
)

new_git_repository(
    name = "sparsehash",
    build_file = "sparsehash.BUILD",
    commit = "0d5a2b6db26e491da44259374df5b8e0eaedc745",
    remote = "https://github.com/romange/sparsehash.git",
)

http_archive(
    name = "com_google_protobuf",
    sha256 = "1e622ce4b84b88b6d2cdf1db38d1a634fe2392d74f0b7b74ff98f3a51838ee53",
    strip_prefix = "protobuf-3.8.0",
    urls = ["https://github.com/google/protobuf/archive/v3.8.0.zip"],
)

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

new_git_repository(
    name = "zstd",
    build_file = "zstd.BUILD",
    remote = "https://github.com/facebook/zstd.git",
    strip_prefix = "lib",
    tag = "v1.4.0",
)

new_git_repository(
    name = "crc32c",
    build_file = "crc32c.BUILD",
    remote = "https://github.com/google/crc32c.git",
    tag = "1.0.7",
)

new_git_repository(
    name = "lz4",
    build_file = "lz4.BUILD",
    remote = "https://github.com/lz4/lz4.git",
    strip_prefix = "lib",
    tag = "v1.8.3",
)

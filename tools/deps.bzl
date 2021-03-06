load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def telegraph_deps():
    http_archive(name='json',
        sha256='541c34438fd54182e9cdc68dd20c898d766713ad6d901fb2c6e28ff1f1e7c10d',
        urls=['https://github.com/nlohmann/json/releases/download/v3.7.0/include.zip'],
        build_file='@telegraph//:external/json.BUILD')

    http_archive(name='hocon',
        sha256="b2cb3db5cfe02e8fb8a65cf89358912895b1a7efc852d83308af0811f221dbad",
        urls=['https://github.com/Penn-Electric-Racing/hocon/archive/8865ebbe434bfe586385514204d7c51b4825785a.zip'],
        strip_prefix='hocon-8865ebbe434bfe586385514204d7c51b4825785a')

    http_archive(name="com_github_nanopb_nanopb",
                 sha256="c3f7f23836c7d2a12fcb6c7dadb42b1ba5201d9b4e67040c02b13462de6814b4",
                 urls=["https://github.com/Penn-Electric-Racing/nanopb/archive/master.zip"],
                 strip_prefix="nanopb-master")
    http_archive(
        name = "com_google_protobuf",
        sha256="e4f8bedb19a93d0dccc359a126f51158282e0b24d92e0cad9c76a9699698268d",
        strip_prefix = "protobuf-3.11.2",
        urls=["https://github.com/protocolbuffers/protobuf/archive/v3.11.2.zip"]
    )

    http_archive(
        name = "com_github_nelhage_rules_boost",
        sha256 = "0d3a8423b1bc5c014d15d28156664c0bfbac471c60a66fa6ccfc7ec67e155c88",
        strip_prefix = "rules_boost-53276db9d8971782ab51a20ae32271b2d12fd0e1",
        urls=["https://github.com/Penn-Electric-Racing/rules_boost/archive/53276db9d8971782ab51a20ae32271b2d12fd0e1.zip"]
    )

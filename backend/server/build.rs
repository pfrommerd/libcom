fn main() {
    prost_build::compile_protos(
        &[
            "../../api.proto",
            "../../common.proto",
            "../../log.proto",
            "../../stream.proto",
        ],
        &["../.."],
    )
    .unwrap();

    prost_build::compile_protos(
        &["src/items.proto"],
        &["src/"]
    ).unwrap();
}

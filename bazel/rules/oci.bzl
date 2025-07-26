"Dockerization helpers"

load("@io_bazel_rules_go//go:def.bzl", "go_cross_binary")
load("@rules_oci//oci:defs.bzl", "oci_image", "oci_load", "oci_push")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

def linux_oci_go(bin_name):
    linux_amd_target_name = bin_name + "_linux_amd64"
    tar_name = bin_name + "_tar"
    image_name = bin_name + "_image"

    go_cross_binary(
        name = linux_amd_target_name,
        platform = "@io_bazel_rules_go//go/toolchain:linux_amd64",
        target = ":" + bin_name,
        visibility = ["//visibility:public"],
    )

    pkg_tar(
        name = tar_name,
        srcs = [":" + linux_amd_target_name],
        remap_paths = {"/" + linux_amd_target_name: "/" + bin_name},
    )

    oci_image(
        name = image_name,
        base = "@docker_lib_ubuntu",
        entrypoint = ["/" + bin_name],
        tars = [":" + tar_name],
    )

    oci_push(
        name = "push_image",
        image = ":" + image_name,
        remote_tags = ["latest"],
        repository = "ghcr.io/muchq/" + bin_name,
        tags = ["manual"],
    )

    oci_load(
        name = "oci_load_tarball",
        image = ":" + image_name,
        repo_tags = ["ghcr.io/muchq/" + bin_name + ":latest"],
    )

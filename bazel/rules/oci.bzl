"Dockerization helpers"

load("@io_bazel_rules_go//go:def.bzl", "go_cross_binary")
load("@rules_oci//oci:defs.bzl", "oci_image", "oci_load", "oci_push")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

def _create_oci_image(bin_name, binary_target, binary_path):
    """
    create oci image, push, and load targets for a binary

    Args:
      bin_name: the binary name for the image
      binary_target: the bazel target containing the binary
      binary_path: the path to remap the binary to in the container
    """
    tar_name = bin_name + "_tar"
    image_name = bin_name + "_image"

    pkg_tar(
        name = tar_name,
        srcs = [":" + binary_target],
        remap_paths = {"/" + binary_target: binary_path},
    )

    oci_image(
        name = image_name,
        base = "@docker_lib_ubuntu",
        entrypoint = [binary_path],
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

def linux_oci_go(bin_name):
    """
    generate linux oci container for go binaries

    Args:
      bin_name: the binary target name to be wrapper
    """

    linux_amd_target_name = bin_name + "_linux_amd64"

    go_cross_binary(
        name = linux_amd_target_name,
        platform = "@io_bazel_rules_go//go/toolchain:linux_amd64",
        target = ":" + bin_name,
        visibility = ["//visibility:public"],
    )

    _create_oci_image(bin_name, linux_amd_target_name, "/" + bin_name)

def linux_amd64_oci_binary(bin_name):
    """
    generate linux amd64 oci container for binary targets

    Args:
      bin_name: the binary target name to be wrapped
    """

    _create_oci_image(bin_name, bin_name, "/" + bin_name)

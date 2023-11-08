#!/bin/bash
#export XTENSA_CORE=AIR_PREMIUM_G3_HIFI5
export ARCH=hifi4

function installDeps() {
    apt update
    apt install python3 python3-pip curl wget ca-certificates apt-transport-https gnupg unzip zip -y
    pip install numpy Pillow

    curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor >bazel-archive-keyring.gpg
    mv bazel-archive-keyring.gpg /usr/share/keyrings
    echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list

    apt update && apt install bazel

    # avoid "detected dubious ownership in repository at '/host/tflite-micro'" messages
    git config --global --add safe.directory /host/tflite-micro
}

installDeps

#  make -f tensorflow/lite/micro/tools/make/Makefile \
#      TARGET=xtensa \
#      OPTIMIZED_KERNEL_DIR=xtensa \
#      TARGET_ARCH=$ARCH \
#      BUILD_TYPE=release_with_logs \
#      third_party_downloads -j$(nproc)

# make -f tensorflow/lite/micro/tools/make/Makefile \
#     TARGET=xtensa \
#     TARGET_ARCH=$ARCH \
#     OPTIMIZED_KERNEL_DIR=xtensa \
#     XTENSA_CORE=$XTENSA_CORE \
#     BUILD_TYPE=release_with_logs \
#     clean -j$(nproc)

# make -f tensorflow/lite/micro/tools/make/Makefile \
#     TARGET=xtensa \
#     TARGET_ARCH=$ARCH \
#     OPTIMIZED_KERNEL_DIR=xtensa \
#     XTENSA_CORE=$XTENSA_CORE \
#     BUILD_TYPE=release_with_logs \
#     build -j$(nproc)

make -f tensorflow/lite/micro/tools/make/Makefile \
    TARGET=xtensa \
    TARGET_ARCH=$ARCH \
    OPTIMIZED_KERNEL_DIR=xtensa \
    XTENSA_CORE=$XTENSA_CORE \
    BUILD_TYPE=release_with_logs \
    test_aicoustics_test -j$(nproc)
    
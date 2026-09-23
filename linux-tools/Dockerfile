FROM c1max-apps-builder:bookworm

# Native compiler is needed for release tarball generators (bash mkbuiltins).
# qemu-user verifies the finished MIPS executables without accessing the device.
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc libc6-dev patch pkg-config qemu-user bzip2 xz-utils file \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc-mipsel-linux-gnu g++-mipsel-linux-gnu libc6-dev-mipsel-cross cmake make git ca-certificates curl nlohmann-json3-dev python3 python3-pil nasm && rm -rf /var/lib/apt/lists/*
WORKDIR /work

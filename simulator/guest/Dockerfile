FROM docker.io/library/debian:bookworm
RUN sed -i 's|http://deb.debian.org/debian-security|http://mirrors.aliyun.com/debian-security|g; s|http://deb.debian.org/debian|http://mirrors.aliyun.com/debian|g' /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::http::Timeout=20 -o Acquire::Retries=1 update \
    && apt-get install -y --no-install-recommends gcc-mipsel-linux-gnu libc6-dev-mipsel-cross build-essential bc bison flex libssl-dev libelf-dev cpio python3 xz-utils ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN dpkg --add-architecture mipsel \
    && apt-get -o Acquire::http::Timeout=20 -o Acquire::Retries=1 update \
    && mkdir -p /opt/c1sim-openssl/debs \
    && cd /opt/c1sim-openssl/debs \
    && apt-get -o Acquire::http::Timeout=20 -o Acquire::Retries=1 download libssl-dev:mipsel \
    && for package in *.deb; do dpkg-deb --extract "$package" /opt/c1sim-openssl; done \
    && dpkg-deb --field *.deb Package Version Architecture > /opt/c1sim-openssl/version \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work

RUN apt-get update && apt-get install -y --no-install-recommends g++-mipsel-linux-gnu nlohmann-json3-dev && rm -rf /var/lib/apt/lists/*

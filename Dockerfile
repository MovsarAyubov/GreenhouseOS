FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    gdb-multiarch \
    make \
    python3 \
    python3-pip \
    python3-venv \
    python3-pytest \
    ca-certificates \
    git \
    bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY tools/docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["build"]

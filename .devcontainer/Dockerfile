FROM ubuntu:22.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install basic build tools and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    libgbm-dev \
    git \
    curl \
    wget \
    pkg-config \
    libfl-dev \
    zlib1g-dev \
    libgoogle-perftools-dev \
    numactl \
    perl \
    python3 \
    python3-pip \
    python3-dev \
    gdb \
    strace \
    procps \
    gtkwave \
    && rm -rf /var/lib/apt/lists/*

# Install Verilator
RUN apt-get update && apt-get install -y verilator

# Install Node.js v20 for Dashboard
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
    apt-get install -y nodejs

# Install Python packages for testing
RUN pip3 install --no-cache-dir \
    numpy \
    pandas \
    matplotlib \
    pytest

# Ensure remoteproc firmware directory exists
RUN mkdir -p /lib/firmware

# Set workspace directory
WORKDIR /workspace

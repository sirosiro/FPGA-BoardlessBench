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
    nano \
    gtkwave \
    bash-completion \
    && rm -rf /var/lib/apt/lists/*

# Install Verilator
RUN apt-get update && apt-get install -y verilator

# Install Charm Glow (Terminal Markdown Viewer for fbb plugin man)
RUN mkdir -p /etc/apt/keyrings && \
    curl -fsSL https://repo.charm.sh/apt/gpg.key | gpg --dearmor -o /etc/apt/keyrings/charm.gpg && \
    echo "deb [signed-by=/etc/apt/keyrings/charm.gpg] https://repo.charm.sh/apt/ * *" | tee /etc/apt/sources.list.d/charm.list && \
    apt-get update && apt-get install -y glow && \
    rm -rf /var/lib/apt/lists/*

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

# Install Rust (Non-interactive -y flag)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"

# Install Universal F-BB CLI Launcher & Dynamic Bash Completion
RUN printf '%s\n' \
    '#!/usr/bin/env bash' \
    '# FPGA-BoardlessBench (F-BB) Universal CLI Launcher' \
    'set -e' \
    'DIR="$PWD"' \
    'while [ "$DIR" != "/" ] && [ -n "$DIR" ]; do' \
    '    if [ -x "$DIR/bin/fbb" ]; then' \
    '        exec "$DIR/bin/fbb" "$@"' \
    '    fi' \
    '    DIR="$(dirname "$DIR")"' \
    'done' \
    'if [ -n "$FBB_ROOT" ] && [ -x "$FBB_ROOT/bin/fbb" ]; then' \
    '    exec "$FBB_ROOT/bin/fbb" "$@"' \
    'fi' \
    'for CANDIDATE in /workspaces/*/bin/fbb /workspace/bin/fbb /workspaces/FPGA-BoardlessBench/bin/fbb; do' \
    '    if [ -x "$CANDIDATE" ]; then' \
    '        exec "$CANDIDATE" "$@"' \
    '    fi' \
    'done' \
    'echo "Error: FPGA-BoardlessBench CLI (bin/fbb) not found." >&2' \
    'echo "Please execute from within the F-BB workspace or set FBB_ROOT." >&2' \
    'exit 1' > /usr/local/bin/fbb && \
    chmod +x /usr/local/bin/fbb && \
    printf '%s\n' \
    '# Dynamic F-BB Bash Completion Loader' \
    '_fbb_load_completion() {' \
    '    local dir="$PWD"' \
    '    while [ "$dir" != "/" ] && [ -n "$dir" ]; do' \
    '        if [ -f "$dir/scripts/fbb_completion.bash" ]; then' \
    '            source "$dir/scripts/fbb_completion.bash"' \
    '            return 0' \
    '        fi' \
    '        dir="$(dirname "$dir")"' \
    '    done' \
    '    for cand in /workspaces/*/scripts/fbb_completion.bash /workspace/scripts/fbb_completion.bash; do' \
    '        if [ -f "$cand" ]; then' \
    '            source "$cand"' \
    '            return 0' \
    '        fi' \
    '    done' \
    '}' \
    '_fbb_load_completion 2>/dev/null' \
    'unset -f _fbb_load_completion' > /etc/bash_completion.d/fbb && \
    printf '%s\n' \
    '#!/usr/bin/env bash' \
    '# VS Code CLI Forwarder & Friendly Navigation Helper' \
    'REAL_CODE=$(find /root/.vscode-server/bin /root/.vscode-server-insiders/bin /root/.antigravity-ide-server/bin -name "code" -o -name "agy-ide" -o -name "antigravity-ide" 2>/dev/null | head -n 1)' \
    'if [ -n "$REAL_CODE" ] && [ -x "$REAL_CODE" ]; then' \
    '    exec "$REAL_CODE" "$@"' \
    'fi' \
    'TARGET_FILE="$1"' \
    'if [ -n "$TARGET_FILE" ]; then' \
    '    echo "💡 Note: VS Code CLI (\"code\") is not attached to this terminal session."' \
    '    echo "To edit in VS Code on Windows / Host:"' \
    '    echo "  • Open from VS Code Left Sidebar (Explorer): $TARGET_FILE"' \
    '    echo "  • Or press Ctrl+P (Cmd+P) in VS Code and search for: $(basename "$TARGET_FILE")"' \
    '    if command -v nano >/dev/null 2>&1 && [ -t 0 ]; then' \
    '        echo "  • Launching terminal editor (nano)..."' \
    '        exec nano "$TARGET_FILE"' \
    '    fi' \
    'else' \
    '    echo "💡 Note: VS Code CLI (\"code\") is not attached to this terminal session."' \
    '    echo "Please open files directly from VS Code'\''s Left Sidebar (Explorer),"' \
    '    echo "or press Ctrl+P (Cmd+P) to quick-open files by name."' \
    'fi' > /usr/local/bin/code && \
    chmod +x /usr/local/bin/code

# Set workspace directory
WORKDIR /workspace


#!/bin/bash
# 把交叉编译产物送到板子上并跑一遍自检（在 WSL 里执行）。
# 环境变量：
#   BOARD_HOST  默认 10.1.1.2
#   BOARD_USER  默认 root
#   BOARD_PASS  必须提供（或写入 ~/.pca9555_board_pass）
# 用法：bash scripts/deploy-wsl.sh [额外传给 pca9555_tool 的参数]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/aarch64/pca9555_tool"
REMOTE_DIR=/root/pca9555_demo

BOARD_HOST="${BOARD_HOST:-10.1.1.2}"
BOARD_USER="${BOARD_USER:-root}"
if [ -z "${BOARD_PASS:-}" ]; then
    if [ -f "$HOME/.pca9555_board_pass" ]; then
        BOARD_PASS="$(cat "$HOME/.pca9555_board_pass")"
    else
        echo "缺少板子密码。请先：export BOARD_PASS=<板子密码>" >&2
        echo "或把密码写进 ~/.pca9555_board_pass（chmod 600），脚本会自动读取。" >&2
        exit 1
    fi
fi

if [ ! -x "$BIN" ]; then
    echo "找不到 $BIN，请先运行 bash scripts/build-wsl.sh" >&2
    exit 1
fi

# 用一个临时 askpass 脚本来喂密码，避免依赖 sshpass，也不往板子写公钥
ASKPASS="$(mktemp)"
trap 'rm -f "$ASKPASS"' EXIT
printf '#!/bin/sh\nprintf "%%s" "$PCA9555_BOARD_PASS"\n' > "$ASKPASS"
chmod 700 "$ASKPASS"

export PCA9555_BOARD_PASS="$BOARD_PASS"
export SSH_ASKPASS="$ASKPASS"
export SSH_ASKPASS_REQUIRE=force
export DISPLAY=:0

SSH_OPTS=(-o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null
          -o LogLevel=ERROR
          -o ConnectTimeout=10
          -o PreferredAuthentications=password
          -o PubkeyAuthentication=no
          -o NumberOfPasswordPrompts=1)

echo ">>> 上传到 $BOARD_USER@$BOARD_HOST:$REMOTE_DIR"
ssh "${SSH_OPTS[@]}" "$BOARD_USER@$BOARD_HOST" "mkdir -p $REMOTE_DIR"
scp "${SSH_OPTS[@]}" "$BIN" "$BOARD_USER@$BOARD_HOST:$REMOTE_DIR/pca9555_tool"

echo ">>> 板端冒烟测试"
ssh "${SSH_OPTS[@]}" "$BOARD_USER@$BOARD_HOST" \
    "chmod +x $REMOTE_DIR/pca9555_tool && \
     $REMOTE_DIR/pca9555_tool --sim demo | tail -6 && \
     echo '--- scan ---' && \
     $REMOTE_DIR/pca9555_tool scan || true"

if [ "$#" -gt 0 ]; then
    echo ">>> 执行：pca9555_tool $*"
    ssh "${SSH_OPTS[@]}" "$BOARD_USER@$BOARD_HOST" "$REMOTE_DIR/pca9555_tool $*"
fi

#!/bin/bash
set -e

echo "=== Docker Container Entrypoint ==="
echo "Running as: $(id)"

TARGET_USER="${RUNNER_USER:-runner}"
TARGET_UID="${RUNNER_UID:-1001}"
TARGET_GID="${RUNNER_GID:-118}"
WORKSPACE="${WORKSPACE_DIR:-/tmp/zephyr-workspace}"

echo "Target user: $TARGET_USER ($TARGET_UID:$TARGET_GID)"
echo "Workspace: $WORKSPACE"

if [ "$(id -u)" = "0" ]; then
    echo "Running as root, fixing permissions..."

    if [ -d "$WORKSPACE" ]; then
        CURRENT_OWNER=$(stat -c '%u:%g' "$WORKSPACE")
        echo "Workspace owned by: $CURRENT_OWNER"
        if [ "$CURRENT_OWNER" != "$TARGET_UID:$TARGET_GID" ]; then
            echo "Fixing workspace ownership..."
            chown -R "$TARGET_UID:$TARGET_GID" "$WORKSPACE"
        fi
    fi

    USER_HOME="/home/$TARGET_USER"
    if [ -d "$USER_HOME/.cache/zephyr" ]; then
        CURRENT_OWNER=$(stat -c '%u:%g' "$USER_HOME/.cache/zephyr")
        if [ "$CURRENT_OWNER" != "$TARGET_UID:$TARGET_GID" ]; then
            echo "Fixing zephyr cache ownership..."
            chown -R "$TARGET_UID:$TARGET_GID" "$USER_HOME/.cache/zephyr"
        fi
    fi

    if [ -d "$USER_HOME/.cache/ccache" ]; then
        CURRENT_OWNER=$(stat -c '%u:%g' "$USER_HOME/.cache/ccache")
        if [ "$CURRENT_OWNER" != "$TARGET_UID:$TARGET_GID" ]; then
            echo "Fixing ccache ownership..."
            chown -R "$TARGET_UID:$TARGET_GID" "$USER_HOME/.cache/ccache"
        fi
    fi

    echo "Dropping to user $TARGET_USER..."
    exec gosu "$TARGET_USER" "$0" "$@"
else
    echo "Running as non-root user: $(id)"
    echo "==================================="
    echo
    exec "$@"
fi

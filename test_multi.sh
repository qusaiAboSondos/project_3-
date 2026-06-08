#!/bin/bash
# Multi-client concurrent test

CONFIG="config.txt"
SERVER_BIN="./server/server_bin"
CLIENT_BIN="./client/client_bin"
NUM_CLIENTS=5
PORT=8080

cleanup() {
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    rm -f /tmp/client_ver_*.txt /tmp/client_dl_*.pkg /tmp/client_out_*.txt
    rm -f /tmp/client_cfg_*.txt /tmp/client_log_*.log
}
trap cleanup EXIT

echo "--- Starting server ---"
fuser -k ${PORT}/tcp 2>/dev/null
sleep 0.3
$SERVER_BIN $CONFIG &
SERVER_PID=$!
sleep 0.5

# -------------------------------------------------------
# TEST 1: Multiple simultaneous clients (outdated + up-to-date)
# -------------------------------------------------------
echo ""
echo "=== TEST 1: $NUM_CLIENTS simultaneous clients ==="
PIDS=()
for i in $(seq 1 $NUM_CLIENTS); do
    VER_FILE="/tmp/client_ver_$i.txt"
    DL_PATH="/tmp/client_dl_$i.pkg"
    TEMP_CFG="/tmp/client_cfg_$i.txt"
    cp $CONFIG $TEMP_CFG

    if (( i % 2 == 0 )); then
        echo "3" > "$VER_FILE"
        LABEL="up-to-date"
    else
        echo "1" > "$VER_FILE"
        LABEL="outdated"
    fi

    sed -i "s|VERSION_FILE=.*|VERSION_FILE=$VER_FILE|" $TEMP_CFG
    sed -i "s|DOWNLOAD_PATH=.*|DOWNLOAD_PATH=$DL_PATH|" $TEMP_CFG
    sed -i "s|CLIENT_LOG=.*|CLIENT_LOG=/tmp/client_log_$i.log|" $TEMP_CFG

    echo "  Launching client $i ($LABEL)"
    $CLIENT_BIN $TEMP_CFG > /tmp/client_out_$i.txt 2>&1 &
    PIDS+=($!)
done

ALL_OK=true
for i in "${!PIDS[@]}"; do
    wait "${PIDS[$i]}"
    RC=$?
    CLIENT_NUM=$((i+1))
    if [ $RC -eq 0 ]; then
        echo "  Client $CLIENT_NUM: PASSED"
    else
        echo "  Client $CLIENT_NUM: FAILED (exit code $RC)"
        ALL_OK=false
    fi
done

echo ""
echo "--- Client outputs ---"
for i in $(seq 1 $NUM_CLIENTS); do
    echo "[Client $i]:"
    cat /tmp/client_out_$i.txt
    echo ""
done

# -------------------------------------------------------
# TEST 2: Invalid client request (sends garbage instead of version)
# -------------------------------------------------------
echo "=== TEST 2: Invalid client request ==="
(echo "INVALID_REQUEST"; sleep 0.5) | nc -q 1 127.0.0.1 $PORT > /tmp/invalid_out.txt 2>&1
echo "  Server response to invalid request:"
cat /tmp/invalid_out.txt
echo "  (server should remain alive after invalid request)"

# verify server still alive
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "  Server still running: PASSED"
else
    echo "  Server crashed after invalid request: FAILED"
    ALL_OK=false
fi

# -------------------------------------------------------
# TEST 3: Interrupted connection (client disconnects immediately)
# -------------------------------------------------------
echo ""
echo "=== TEST 3: Interrupted connection ==="
(sleep 0.1) | nc -q 0 127.0.0.1 $PORT > /dev/null 2>&1 &
sleep 0.5

if kill -0 $SERVER_PID 2>/dev/null; then
    echo "  Server survived interrupted connection: PASSED"
else
    echo "  Server crashed on interrupted connection: FAILED"
    ALL_OK=false
fi

# -------------------------------------------------------
echo ""
echo "--- Stopping server (graceful shutdown via SIGINT) ---"
kill -INT $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

if $ALL_OK; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== SOME TESTS FAILED ==="
    exit 1
fi

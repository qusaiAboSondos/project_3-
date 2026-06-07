#!/bin/bash
# Multi-client concurrent test

CONFIG="config.txt"
SERVER_BIN="./server/server_bin"
CLIENT_BIN="./client/client_bin"
NUM_CLIENTS=5

echo "--- Starting server ---"
fuser -k 8080/tcp 2>/dev/null
sleep 0.3
$SERVER_BIN $CONFIG &
SERVER_PID=$!
sleep 0.5

echo "--- Launching $NUM_CLIENTS simultaneous clients ---"
PIDS=()
for i in $(seq 1 $NUM_CLIENTS); do
    # alternating versions: odd=outdated(1), even=up-to-date(3)
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

    # patch version file and download path in temp config
    sed -i "s|VERSION_FILE=.*|VERSION_FILE=$VER_FILE|" $TEMP_CFG
    sed -i "s|DOWNLOAD_PATH=.*|DOWNLOAD_PATH=$DL_PATH|" $TEMP_CFG
    sed -i "s|CLIENT_LOG=.*|CLIENT_LOG=/tmp/client_log_$i.log|" $TEMP_CFG

    echo "  Launching client $i ($LABEL)"
    $CLIENT_BIN $TEMP_CFG > /tmp/client_out_$i.txt 2>&1 &
    PIDS+=($!)
done

echo "--- Waiting for all clients to finish ---"
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

echo "--- Stopping server ---"
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

# cleanup
rm -f /tmp/client_ver_*.txt /tmp/client_dl_*.pkg /tmp/client_out_*.txt
rm -f /tmp/client_cfg_*.txt /tmp/client_log_*.log

if $ALL_OK; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== SOME TESTS FAILED ==="
    exit 1
fi

#!/bin/bash
# =============================================================
# Comprehensive test suite - covers all requirements scenarios
# =============================================================

CONFIG="config.txt"
SERVER_BIN="./server/server_bin"
CLIENT_BIN="./client/client_bin"
PORT=8080
PASS=0
FAIL=0

# ---- helpers ------------------------------------------------

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
info() { echo -e "  ${YELLOW}[INFO]${NC} $1"; }
section() { echo ""; echo "========================================="; echo "  $1"; echo "========================================="; }

start_server() {
    local cfg="${1:-$CONFIG}"
    fuser -k ${PORT}/tcp 2>/dev/null
    sleep 0.2
    $SERVER_BIN "$cfg" &
    SERVER_PID=$!
    sleep 0.5
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "ERROR: server failed to start"; exit 1
    fi
    info "Server started (PID=$SERVER_PID)"
}

stop_server() {
    kill -INT $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    info "Server stopped"
}

make_client_cfg() {
    local id="$1" ver="$2"
    local cfg="/tmp/tc_cfg_$id.txt"
    cp $CONFIG "$cfg"
    echo "$ver" > "/tmp/tc_ver_$id.txt"
    sed -i "s|VERSION_FILE=.*|VERSION_FILE=/tmp/tc_ver_$id.txt|" "$cfg"
    sed -i "s|DOWNLOAD_PATH=.*|DOWNLOAD_PATH=/tmp/tc_dl_$id.pkg|" "$cfg"
    sed -i "s|CLIENT_LOG=.*|CLIENT_LOG=/tmp/tc_log_$id.log|" "$cfg"
    echo "$cfg"
}

cleanup() {
    stop_server 2>/dev/null
    rm -f /tmp/tc_* /tmp/large_update.pkg /tmp/large_cfg.txt /tmp/large_ver.txt
}
trap cleanup EXIT

# =============================================================
# Build first
# =============================================================
section "Building project"
make 2>&1 | tail -3
if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "Build failed — aborting"; exit 1
fi
pass "Build successful"

mkdir -p logs

# =============================================================
# TEST 1: Single client — outdated version
# =============================================================
section "TEST 1: Single client with outdated version"
start_server

CFG=$(make_client_cfg 1 1)
$CLIENT_BIN "$CFG" > /tmp/tc_out_1.txt 2>&1
RC=$?

if [ $RC -eq 0 ]; then
    pass "Client exited successfully"
else
    fail "Client exited with code $RC"
fi

if grep -q "Update downloaded successfully" /tmp/tc_out_1.txt; then
    pass "Update file downloaded"
else
    fail "Update file not downloaded"
fi

if [ -f "/tmp/tc_dl_1.pkg" ] && [ -s "/tmp/tc_dl_1.pkg" ]; then
    pass "Downloaded file saved locally"
else
    fail "Downloaded file missing or empty"
fi

SAVED_VER=$(cat /tmp/tc_ver_1.txt 2>/dev/null)
if [ "$SAVED_VER" = "3" ]; then
    pass "Version file updated to latest (3)"
else
    fail "Version file not updated (got: $SAVED_VER)"
fi

if grep -q "Simulating update installation" /tmp/tc_out_1.txt; then
    pass "Update installation simulated"
else
    fail "Update installation not simulated"
fi

stop_server

# =============================================================
# TEST 2: Single client — already up to date
# =============================================================
section "TEST 2: Single client already up to date"
start_server

CFG=$(make_client_cfg 2 3)
$CLIENT_BIN "$CFG" > /tmp/tc_out_2.txt 2>&1
RC=$?

if [ $RC -eq 0 ]; then
    pass "Client exited successfully"
else
    fail "Client exited with code $RC"
fi

if grep -qi "up to date" /tmp/tc_out_2.txt; then
    pass "Correct 'up to date' message displayed"
else
    fail "Missing 'up to date' message"
fi

if [ ! -f "/tmp/tc_dl_2.pkg" ] || [ ! -s "/tmp/tc_dl_2.pkg" ]; then
    pass "No unnecessary file downloaded"
else
    fail "File was downloaded when not needed"
fi

stop_server

# =============================================================
# TEST 3: Multiple simultaneous clients (concurrent downloads)
# =============================================================
section "TEST 3: 8 simultaneous clients — concurrent downloads"
start_server

PIDS=()
for i in $(seq 1 8); do
    VER=$(( (i % 2 == 0) ? 3 : 1 ))
    CFG=$(make_client_cfg "m$i" $VER)
    $CLIENT_BIN "$CFG" > /tmp/tc_out_m$i.txt 2>&1 &
    PIDS+=($!)
done

CONCURRENT_OK=true
for i in "${!PIDS[@]}"; do
    wait "${PIDS[$i]}"
    RC=$?
    NUM=$((i+1))
    if [ $RC -ne 0 ]; then
        CONCURRENT_OK=false
        info "Client $NUM failed (exit $RC)"
    fi
done

if $CONCURRENT_OK; then
    pass "All 8 concurrent clients completed successfully"
else
    fail "One or more concurrent clients failed"
fi

# count successful downloads
DL_COUNT=$(ls /tmp/tc_dl_m*.pkg 2>/dev/null | wc -l)
info "Downloaded files: $DL_COUNT / 4 expected (outdated clients)"
if [ "$DL_COUNT" -ge 4 ]; then
    pass "All outdated clients downloaded update"
else
    fail "Some outdated clients did not download (got $DL_COUNT)"
fi

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server remained responsive throughout"
else
    fail "Server crashed during concurrent test"
fi

stop_server

# =============================================================
# TEST 4: Interrupted connection (client disconnects immediately)
# =============================================================
section "TEST 4: Interrupted connection"
start_server

# connect and immediately close without sending anything
(sleep 0.05) | nc -q 0 127.0.0.1 $PORT > /dev/null 2>&1
sleep 0.5

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server survived interrupted connection"
else
    fail "Server crashed on interrupted connection"
fi

# connect, send partial data, then close
(printf "1"; sleep 0.1) | nc -q 0 127.0.0.1 $PORT > /dev/null 2>&1 &
sleep 0.3

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server survived partial-data interrupted connection"
else
    fail "Server crashed on partial data + disconnect"
fi

stop_server

# =============================================================
# TEST 5: Invalid client requests
# =============================================================
section "TEST 5: Invalid client requests"
start_server

# send garbage string
RESP=$(echo "GARBAGE_DATA_$$" | nc -q 1 127.0.0.1 $PORT 2>/dev/null)
sleep 0.3

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server survived garbage request"
else
    fail "Server crashed on garbage request"
fi

# send empty message
RESP=$(echo "" | nc -q 1 127.0.0.1 $PORT 2>/dev/null)
sleep 0.3

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server survived empty request"
else
    fail "Server crashed on empty request"
fi

# send very large payload (10KB of junk)
python3 -c "print('X'*10240)" | nc -q 1 127.0.0.1 $PORT > /dev/null 2>&1
sleep 0.3

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server survived oversized request"
else
    fail "Server crashed on oversized request"
fi

# send negative version number
RESP=$(echo "-99" | nc -q 1 127.0.0.1 $PORT 2>/dev/null)
sleep 0.3

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server survived negative version number"
else
    fail "Server crashed on negative version"
fi

stop_server

# =============================================================
# TEST 6: Large file transfer
# =============================================================
section "TEST 6: Large file transfer (10 MB)"

# create a 10 MB fake update package
dd if=/dev/urandom of=/tmp/large_update.pkg bs=1M count=10 2>/dev/null
info "Created 10MB test update file"

# create config pointing to large file
cp $CONFIG /tmp/large_cfg.txt
sed -i "s|UPDATE_FILE=.*|UPDATE_FILE=/tmp/large_update.pkg|" /tmp/large_cfg.txt
sed -i "s|LATEST_VERSION=.*|LATEST_VERSION=99|" /tmp/large_cfg.txt

start_server /tmp/large_cfg.txt

echo "1" > /tmp/large_ver.txt
cp /tmp/large_cfg.txt /tmp/large_client_cfg.txt
sed -i "s|VERSION_FILE=.*|VERSION_FILE=/tmp/large_ver.txt|" /tmp/large_client_cfg.txt
sed -i "s|DOWNLOAD_PATH=.*|DOWNLOAD_PATH=/tmp/large_dl.pkg|" /tmp/large_client_cfg.txt
sed -i "s|CLIENT_LOG=.*|CLIENT_LOG=/tmp/large_client.log|" /tmp/large_client_cfg.txt

START_TIME=$(date +%s)
$CLIENT_BIN /tmp/large_client_cfg.txt > /tmp/large_out.txt 2>&1
RC=$?
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

if [ $RC -eq 0 ]; then
    pass "Large file transfer completed (exit 0)"
else
    fail "Large file transfer failed (exit $RC)"
fi

if [ -f "/tmp/large_dl.pkg" ]; then
    ORIG_SIZE=$(stat -c%s /tmp/large_update.pkg)
    RECV_SIZE=$(stat -c%s /tmp/large_dl.pkg)
    if [ "$ORIG_SIZE" = "$RECV_SIZE" ]; then
        pass "Large file received intact ($RECV_SIZE bytes in ${ELAPSED}s)"
    else
        fail "File size mismatch: sent=$ORIG_SIZE received=$RECV_SIZE"
    fi
else
    fail "Large downloaded file not found"
fi

rm -f /tmp/large_dl.pkg /tmp/large_client_cfg.txt /tmp/large_ver.txt /tmp/large_client.log
stop_server

# =============================================================
# TEST 7: Server connection refused (no server running)
# =============================================================
section "TEST 7: Client behavior when server is unavailable"
fuser -k ${PORT}/tcp 2>/dev/null
sleep 0.2

CFG=$(make_client_cfg "noserv" 1)
$CLIENT_BIN "$CFG" > /tmp/tc_out_noserv.txt 2>&1
RC=$?

if [ $RC -ne 0 ]; then
    pass "Client returned error when server unavailable (exit $RC)"
else
    fail "Client reported success with no server running"
fi

if grep -qi "connect.*failed\|failed\|error" /tmp/tc_out_noserv.txt 2>/dev/null || \
   grep -qi "connect.*failed\|failed\|error" /tmp/tc_log_noserv.log 2>/dev/null; then
    pass "Error logged correctly"
else
    info "No explicit error message found in output (check logs)"
fi

# =============================================================
# TEST 8: Graceful server shutdown (SIGINT)
# =============================================================
section "TEST 8: Graceful server shutdown via SIGINT"
start_server

# send SIGINT
kill -INT $SERVER_PID 2>/dev/null
sleep 0.5

if ! kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server exited cleanly on SIGINT"
else
    kill $SERVER_PID 2>/dev/null
    fail "Server did not exit on SIGINT"
fi

if grep -q "shut down\|shutdown\|stopping" logs/server.log 2>/dev/null; then
    pass "Shutdown event logged"
else
    info "Shutdown log entry not found (may be suppressed without DISPLAY)"
fi
SERVER_PID=""

# =============================================================
# TEST 9: Thread safety — race condition stress test
# =============================================================
section "TEST 9: Race condition stress test (20 concurrent clients)"
start_server

PIDS=()
for i in $(seq 1 20); do
    VER=$(( RANDOM % 3 + 1 ))
    CFG=$(make_client_cfg "r$i" $VER)
    $CLIENT_BIN "$CFG" > /tmp/tc_out_r$i.txt 2>&1 &
    PIDS+=($!)
done

RACE_FAIL=0
for pid in "${PIDS[@]}"; do
    wait "$pid"
    [ $? -ne 0 ] && RACE_FAIL=$((RACE_FAIL+1))
done

if [ $RACE_FAIL -eq 0 ]; then
    pass "All 20 concurrent clients completed without error"
else
    fail "$RACE_FAIL / 20 clients failed under stress"
fi

if kill -0 $SERVER_PID 2>/dev/null; then
    pass "Server stable after stress test"
else
    fail "Server crashed during stress test"
fi

stop_server

# =============================================================
# TEST 10: Logging completeness
# =============================================================
section "TEST 10: Logging completeness"
start_server

CFG=$(make_client_cfg "log" 1)
$CLIENT_BIN "$CFG" > /dev/null 2>&1
sleep 0.3

LOG="logs/server.log"
if [ -f "$LOG" ] && [ -s "$LOG" ]; then
    pass "Server log file exists and non-empty"
else
    fail "Server log file missing or empty"
fi

for EVENT in "connected\|connection" "version\|Version" "update\|Update" "sent\|transfer\|Transfer"; do
    if grep -qi "$EVENT" "$LOG" 2>/dev/null; then
        pass "Log contains event: $EVENT"
    else
        fail "Log missing event: $EVENT"
    fi
done

if grep -qP '\d{4}-\d{2}-\d{2}|\d{2}:\d{2}:\d{2}' "$LOG" 2>/dev/null; then
    pass "Log entries contain timestamps"
else
    fail "Timestamps missing from log"
fi

if grep -q "TID\|tid\|thread" "$LOG" 2>/dev/null; then
    pass "Log entries contain thread identifiers"
else
    fail "Thread identifiers missing from log"
fi

stop_server

# =============================================================
# SUMMARY
# =============================================================
echo ""
echo "========================================="
echo "  RESULTS"
echo "========================================="
echo -e "  ${GREEN}PASSED: $PASS${NC}"
echo -e "  ${RED}FAILED: $FAIL${NC}"
TOTAL=$((PASS+FAIL))
echo "  TOTAL : $TOTAL"
echo "========================================="

if [ $FAIL -eq 0 ]; then
    echo -e "  ${GREEN}ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "  ${RED}$FAIL TEST(S) FAILED${NC}"
    exit 1
fi

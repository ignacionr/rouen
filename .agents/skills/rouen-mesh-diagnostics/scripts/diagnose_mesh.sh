#!/usr/bin/env bash
set -euo pipefail

# Rouen Mesh Diagnostic Script
# Inspects local Rouen client, REST API, configuration files, cloud mesh reachability, and paired keys.

ROUEN_REST_PORT="${ROUEN_REST_PORT:-8081}"
REST_URL="http://127.0.0.1:${ROUEN_REST_PORT}"
REPO_ENV="/Users/inz/src/rouen/.env"
BUNDLE_ENV="${HOME}/Applications/Rouen.app/Contents/Resources/.env"
CLOUD_SERVERS=("s1.inz.dev" "s2.inz.dev")

echo "=================================================================="
echo " Rouen Mesh Diagnostic Suite"
echo " Time: $(date)"
echo "=================================================================="

# 1. Process Check
echo -e "\n[1] Checking Rouen Process..."
ROUEN_PIDS=$(pgrep -f "rouen" || true)
if [ -z "${ROUEN_PIDS}" ]; then
    echo "[-] Rouen process is NOT running."
else
    echo "[+] Running Rouen processes (PIDs):"
    ps -fp ${ROUEN_PIDS} | grep -v "grep" || true
fi

# 2. Local REST API Status
echo -e "\n[2] Querying Local REST API at ${REST_URL}/api/mesh/status..."
if curl -s -m 2 "${REST_URL}/api/mesh/status" > /tmp/rouen_mesh_status.json 2>/dev/null; then
    echo "[+] REST API responsive. Current mesh status:"
    cat /tmp/rouen_mesh_status.json | python3 -m json.tool 2>/dev/null || cat /tmp/rouen_mesh_status.json
    echo ""
else
    echo "[-] REST API at ${REST_URL} is unreachable."
fi

# 3. Configuration Comparison (Repo .env vs App Bundle .env)
echo -e "\n[3] Inspecting Mesh Configuration Files..."
echo "  Repo .env:   ${REPO_ENV}"
echo "  Bundle .env: ${BUNDLE_ENV}"

get_var() {
    local file="$1"
    local key="$2"
    if [ -f "$file" ]; then
        grep -E "^${key}=" "$file" | cut -d '=' -f2- || true
    else
        echo "(file missing)"
    fi
}

REPO_CLIENT_ID=$(get_var "${REPO_ENV}" "ROUEN_MESH_CLIENT_ID")
BUNDLE_CLIENT_ID=$(get_var "${BUNDLE_ENV}" "ROUEN_MESH_CLIENT_ID")
REPO_PUB=$(get_var "${REPO_ENV}" "ROUEN_MESH_PUBLIC_KEY")
BUNDLE_PUB=$(get_var "${BUNDLE_ENV}" "ROUEN_MESH_PUBLIC_KEY")
REPO_PRIV=$(get_var "${REPO_ENV}" "ROUEN_MESH_PRIVATE_KEY")
BUNDLE_PRIV=$(get_var "${BUNDLE_ENV}" "ROUEN_MESH_PRIVATE_KEY")
REPO_PAIRED=$(get_var "${REPO_ENV}" "ROUEN_MESH_PAIRED")
BUNDLE_PAIRED=$(get_var "${BUNDLE_ENV}" "ROUEN_MESH_PAIRED")

echo "  Client ID:   repo='${REPO_CLIENT_ID}' | bundle='${BUNDLE_CLIENT_ID}'"
echo "  Paired Flag: repo='${REPO_PAIRED}' | bundle='${BUNDLE_PAIRED}'"
echo "  Public Key:  repo='${REPO_PUB}'"
echo "               bundle='${BUNDLE_PUB}'"

if [ -n "${REPO_PRIV}" ]; then
    echo "  Private Key: repo is set (${#REPO_PRIV} chars)"
else
    echo "  Private Key: repo is EMPTY"
fi

if [ -n "${BUNDLE_PRIV}" ]; then
    echo "               bundle is set (${#BUNDLE_PRIV} chars)"
else
    echo "  [!] WARNING: bundle private key is EMPTY! Rouen will generate random ephemeral keys and fail authentication."
fi

if [ "${REPO_PUB}" != "${BUNDLE_PUB}" ]; then
    echo "  [!] MISMATCH: Repo public key differs from Bundle public key!"
fi

# 4. Network and Cloud Connectivity
echo -e "\n[4] Testing Cloud Mesh Endpoint Connectivity..."
for host in "${CLOUD_SERVERS[@]}"; do
    echo -n "  Testing HTTPS to ${host}:443... "
    if curl -s -m 3 "https://${host}/" -o /dev/null; then
        echo "OK"
    else
        echo "FAILED"
    fi
done

# 5. Remote Server Paired Keys Check (via SSH if available)
echo -e "\n[5] Querying Cloud Server Paired Keys Database (/etc/rouen-service/paired_keys.json)..."
REMOTE_PUB=""
if ssh -o ConnectTimeout=3 -o BatchMode=yes root@s1.inz.dev "cat /etc/rouen-service/paired_keys.json" > /tmp/paired_keys.json 2>/dev/null; then
    echo "[+] Successfully retrieved paired_keys.json from s1.inz.dev"
    CLIENT_TO_CHECK="${BUNDLE_CLIENT_ID:-${REPO_CLIENT_ID:-rouen-ignacios-macbook-air}}"
    REMOTE_PUB=$(python3 -c "import json, sys; d=json.load(open('/tmp/paired_keys.json')); print(d.get('${CLIENT_TO_CHECK}', 'NOT_REGISTERED'))" 2>/dev/null || echo "ERROR")
    echo "  Registered Public Key for '${CLIENT_TO_CHECK}':"
    echo "  ${REMOTE_PUB}"
    
    if [ -n "${BUNDLE_PUB}" ] && [ "${BUNDLE_PUB}" = "${REMOTE_PUB}" ]; then
        echo "  [+] MATCH: Bundle public key matches cloud paired public key."
    elif [ -n "${REPO_PUB}" ] && [ "${REPO_PUB}" = "${REMOTE_PUB}" ]; then
        echo "  [!] ACTION NEEDED: Repo public key matches cloud server, but Bundle is missing/desynchronized!"
        echo "      Fix by copying mesh settings from ${REPO_ENV} to ${BUNDLE_ENV}."
    else
        echo "  [-] Public key does not match cloud registration or client is not paired."
    fi
else
    echo "  [*] Direct SSH check to root@s1.inz.dev skipped or unreachable."
fi

echo -e "\n=================================================================="
echo " Diagnostic Check Completed."
echo "=================================================================="

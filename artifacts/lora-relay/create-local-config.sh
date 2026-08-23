#!/usr/bin/env bash
set -euo pipefail

# Generate private provisioning headers locally.
# These output files are ignored by Git. Never commit or paste their contents.

umask 077
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
relay_dir="$repo_root/artifacts/lora-relay/TrapMasterRelay"
gateway_dir="$repo_root/artifacts/lora-gateway/TrapMasterGateway"
relay_config="$relay_dir/TrapMasterRelay.local.h"
gateway_config="$gateway_dir/TrapMasterGateway.local.h"

command -v openssl >/dev/null 2>&1 || {
    echo "Error: openssl is required to generate provisioning keys." >&2
    exit 1
}

if [[ -e "$relay_config" || -e "$gateway_config" ]]; then
    echo "Local provisioning files already exist; refusing to overwrite them." >&2
    echo "Delete them only if you intentionally want to generate a new key pair." >&2
    exit 1
fi

mkdir -p "$relay_dir" "$gateway_dir"
aes_hex="$(openssl rand -hex 16)"
hmac_key="$(openssl rand -hex 32)"
aes_bytes="$(printf '%s' "$aes_hex" | sed 's/../0x&, /g; s/, $//')"

cat > "$relay_config" <<EOF
#pragma once

// Generated locally. Do not commit or share this file.
#define TM_PROTOCOL_KEY_BYTES { \\
    ${aes_bytes} \\
}

// The fixed TrapMasterRelayA-H Arduino projects define the machine ID in their
// wrapper sketch. Do not add TM_MACHINE_ID to this shared configuration file.
#define TM_RELAY_GPIO 4
#define TM_RELAY_ACTIVE_LEVEL LOW
EOF

cat > "$gateway_config" <<EOF
#pragma once

// Generated locally. Do not commit or share this file.
#define TM_PROTOCOL_KEY_BYTES { \\
    ${aes_bytes} \\
}

// Enter this value in the terminal's physical Gateway Auth Key setting.
#define TM_GATEWAY_AUTH_KEY "${hmac_key}"
EOF

chmod 600 "$relay_config" "$gateway_config"
echo "Created private local gateway and relay configuration files."
echo "They are ignored by Git and were not printed."
#!/usr/bin/env bash
set -euo pipefail

mkdir -p "$HOME/.config/nix"

NIX_CONF="$HOME/.config/nix/nix.conf"
if [[ ! -f "$NIX_CONF" ]]; then
  touch "$NIX_CONF"
fi

if ! grep -Eq '^experimental-features\s*=.*nix-command.*flakes' "$NIX_CONF"; then
  {
    echo ""
    echo "experimental-features = nix-command flakes"
  } >> "$NIX_CONF"
fi

# Prime the dev shell once so the first configure/build has fewer surprises.
nix --extra-experimental-features 'nix-command flakes' develop -c bash -lc 'cmake --version && ninja --version >/dev/null'

echo "Rouen devcontainer setup complete. Use Nix-based tasks for configure/build/test."

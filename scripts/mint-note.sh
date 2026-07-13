#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Policy: never edit Mint in place. Copy the path into Spear/overlay first.
cat <<'EOF'
Spear Mint policy (Grok)
────────────────────────
1. Product boot is KILROY:  Spear/boot/pack.sh
2. Mint is upstream packages only.
3. Any changed Mint file lives under:

     SG/Spear/overlay/<same-path-as-on-root>

   Example: change /etc/os-release → Spear/overlay/etc/os-release

4. Apply onto an extracted root:

     Spear/scripts/apply-overlay.sh /path/to/edit

5. C++ tools only for format/stack logic: Spear/src/ (make)
EOF

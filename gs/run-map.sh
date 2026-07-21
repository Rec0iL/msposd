#!/bin/bash
# Alias for the preflight mode (browse / download / set target).
# See map.sh for preview/full overlay modes.
exec "$(cd "$(dirname "$0")" && pwd)/map.sh" preflight

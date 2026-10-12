#!/bin/sh
# Optional workspace-only tools; never installed or packaged.
MD_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export PATH="$MD_ROOT/.tools/root/usr/bin:$PATH"
export PYTHONPATH="$MD_ROOT/.tools/root/usr/lib/python3/dist-packages${PYTHONPATH:+:$PYTHONPATH}"
export PKG_CONFIG_PATH="$MD_ROOT/.tools/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
exec "$@"

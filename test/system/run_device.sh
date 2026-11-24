#!/bin/sh
exec "${PYTHON:-python}" "$(dirname "$0")/run_device.py" "$@"

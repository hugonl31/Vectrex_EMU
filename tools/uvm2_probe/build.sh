#!/bin/bash
# build.sh - the UVMC2 probe for Optimize (WSL, the UVMC2 starter kit and the Arm toolchain as in the Makefile):
# builds build_uvm2/uvm2_probe.um2 and copies it to package/system/uvm2_probe.um2
set -e
cd "$(dirname "$0")"
rm -rf build_uvm2
make uvm2 "$@" < /dev/null
cp build_uvm2/uvm2_probe.um2 ../../package/system/uvm2_probe.um2
echo "package/system/uvm2_probe.um2 updated"

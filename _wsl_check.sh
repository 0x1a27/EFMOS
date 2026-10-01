#!/bin/bash
cd ~/efmos || exit 1
echo "=== build log tail ==="
tail -30 build_baseline.log 2>&1
echo "=== artifacts ==="
ls -la disk.img disk.vmdk 2>&1
echo "=== make running? ==="
pgrep -a make | head -3
echo "=== gcc running? ==="
pgrep -a gcc | head -3
echo "=== log size ==="
ls -la build_baseline.log

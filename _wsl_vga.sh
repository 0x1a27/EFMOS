#!/bin/bash
cd ~/efmos || exit 1
echo "=== vga lines in log ==="
grep -n "vga" build_baseline.log | head -20
echo "=== context around vga build ==="
awk '/vga_drv.c/{print NR": "$0}' build_baseline.log | head -20
echo "=== lines 60-100 of log ==="
sed -n '60,100p' build_baseline.log
echo "=== make exit summary: rerun vga target only ==="
make drivers/vga.drv 2>&1 | tail -15

#!/bin/bash
cd ~/efmos || exit 1
echo "=== errors in log ==="
grep -n -iE "error|undefined|cannot" build_baseline.log | head -30
echo "=== warnings count ==="
grep -c "warning:" build_baseline.log
echo "=== baseline artifact checksums ==="
for f in bootloader/BOOTX64.EFI kernel/kernel.elf efmsfile/fileman.efs efmsfile/setting.efs efmsfile/userman.efs efmsfile/efmlogin.efs efmsfile/efmshell.efs efmsfile/efmloader.efs efmsfile/efmcompositor.efs efmsfile/efmAether.efs drivers/vga.drv drivers/ahci.drv drivers/Graphics.drv Program/gcc/gcc.efs disk.img; do
  if [ -f "$f" ]; then printf "%s  %s\n" "$(md5sum "$f" | cut -d' ' -f1)" "$f"; else echo "MISSING $f"; fi
done > /mnt/c/0x1a27/EFMOS/_baseline_md5.txt
cat /mnt/c/0x1a27/EFMOS/_baseline_md5.txt
echo "=== symbol check: kernel.elf entry ==="
nm kernel/kernel.elf 2>/dev/null | grep -E "_start|kmain" | head -5

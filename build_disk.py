#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# EFMOS - a 64-bit x86_64 UEFI operating system written in C.
#
# Copyright (C) 2026 0x1a27
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""build_disk.py - 构建 EFMOS 磁盘映像 (无需 parted/mtools)
用 Python 创建 GPT + FAT16 ESP, 用 mkfs.ext4 + debugfs 创建 ext4 分区."""
import struct, os, subprocess, sys, tempfile

DISK_SIZE = 150 * 1024 * 1024
ESP_OFFSET = 1 * 1024 * 1024
ESP_SIZE = 32 * 1024 * 1024
EXT_OFFSET = 33 * 1024 * 1024
EXT_SIZE = DISK_SIZE - EXT_OFFSET - 34 * 512  # Reserve 34 sectors for backup GPT
SECTOR = 512

def create_gpt(disk_path):
    """创建 GPT 分区表: ESP (1MiB-33MiB) + ext4 (33MiB-150MiB)"""
    with open(disk_path, 'r+b') as f:
        # Protective MBR (offset 446 = first partition entry)
        f.seek(0)
        mbr = bytearray(SECTOR)
        mbr[446] = 0x00  # status (not bootable)
        mbr[447] = 0x00  # start head
        mbr[448] = 0x02  # start sector
        mbr[449] = 0x00  # start cylinder
        mbr[450] = 0xEE  # GPT protective type
        mbr[451] = 0xFF  # end head
        mbr[452] = 0xFF  # end sector/cylinder
        mbr[453] = 0xFF  # end cylinder
        struct.pack_into('<I', mbr, 454, 1)           # start LBA = 1
        struct.pack_into('<I', mbr, 458, 0xFFFFFFFF)  # num sectors (max)
        mbr[510] = 0x55
        mbr[511] = 0xAA
        f.write(mbr)

        # GPT Header (LBA 1)
        esp_start_lba = ESP_OFFSET // SECTOR
        esp_end_lba = (ESP_OFFSET + ESP_SIZE) // SECTOR - 1
        ext_start_lba = EXT_OFFSET // SECTOR
        ext_end_lba = (EXT_OFFSET + EXT_SIZE) // SECTOR - 1

        hdr = bytearray(SECTOR)
        hdr[0:8] = b'EFI PART'
        struct.pack_into('<I', hdr, 8, 0x00010000)  # version
        struct.pack_into('<I', hdr, 12, 92)  # header size
        struct.pack_into('<I', hdr, 16, 0)   # CRC (fill later)
        struct.pack_into('<I', hdr, 20, 0)   # reserved
        struct.pack_into('<Q', hdr, 24, 1)   # my LBA
        struct.pack_into('<Q', hdr, 32, DISK_SIZE // SECTOR - 1)  # backup LBA
        struct.pack_into('<Q', hdr, 40, 2)   # first usable LBA
        struct.pack_into('<Q', hdr, 48, DISK_SIZE // SECTOR - 34)  # last usable LBA
        hdr[56:72] = b'\x4E\x65\x78\x74\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'  # disk GUID (random)
        struct.pack_into('<Q', hdr, 72, 2)   # partition entries start LBA
        struct.pack_into('<I', hdr, 80, 128)  # num entries
        struct.pack_into('<I', hdr, 84, 128)  # entry size
        # CRC32 of header
        import zlib
        hdr_crc = zlib.crc32(bytes(hdr[0:92])) & 0xFFFFFFFF
        struct.pack_into('<I', hdr, 16, hdr_crc)
        f.seek(SECTOR)
        f.write(hdr)

        # Partition entries (LBA 2-33)
        f.seek(2 * SECTOR)
        entries = bytearray(128 * 128)  # 128 entries * 128 bytes

        # Entry 0: ESP (FAT16)
        # EFI System Partition type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
        # GPT mixed-endian: Data1(LE32) Data2(LE16) Data3(LE16) Data4(BE48)
        e0 = bytearray(128)
        e0[0:16] = b'\x28\x73\x2A\xC1\x1F\xF8\xD2\x11\xBA\x4B\x00\xA0\xC9\x3E\xC9\x3B'
        e0[16:32] = b'\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'  # unique GUID
        struct.pack_into('<Q', e0, 32, esp_start_lba)
        struct.pack_into('<Q', e0, 40, esp_end_lba)
        struct.pack_into('<Q', e0, 48, 0)  # attributes
        name = 'EFI System Partition'
        e0[56:56+len(name)*2] = name.encode('utf-16-le')
        entries[0:128] = e0

        # Entry 1: ext4 (Linux filesystem)
        # Linux filesystem data type GUID: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
        e1 = bytearray(128)
        e1[0:16] = b'\xAF\x3D\xC6\x0F\x83\x84\x72\x47\x8E\x79\x3D\x69\xD8\x47\x7D\xE4'
        e1[16:32] = b'\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
        struct.pack_into('<Q', e1, 32, ext_start_lba)
        struct.pack_into('<Q', e1, 40, ext_end_lba)
        struct.pack_into('<Q', e1, 48, 0)
        name2 = 'EFMOS Root'
        e1[56:56+len(name2)*2] = name2.encode('utf-16-le')
        entries[128:256] = e1

        # CRC32 of entries
        entries_crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
        struct.pack_into('<I', hdr, 88, entries_crc)
        # Recalculate header CRC
        struct.pack_into('<I', hdr, 16, 0)
        hdr_crc = zlib.crc32(bytes(hdr[0:92])) & 0xFFFFFFFF
        struct.pack_into('<I', hdr, 16, hdr_crc)
        f.seek(SECTOR)
        f.write(hdr)
        f.seek(2 * SECTOR)
        f.write(entries)

        # Backup GPT at end of disk
        backup_entry_lba = DISK_SIZE // SECTOR - 33
        backup_hdr_lba = DISK_SIZE // SECTOR - 1
        f.seek(backup_entry_lba * SECTOR)
        f.write(entries)
        bhdr = bytearray(hdr)
        struct.pack_into('<Q', bhdr, 24, backup_hdr_lba)  # my LBA
        struct.pack_into('<Q', bhdr, 32, 1)  # backup (primary) LBA
        struct.pack_into('<Q', bhdr, 72, backup_entry_lba)  # entries start
        struct.pack_into('<I', bhdr, 16, 0)
        bhdr_crc = zlib.crc32(bytes(bhdr[0:92])) & 0xFFFFFFFF
        struct.pack_into('<I', bhdr, 16, bhdr_crc)
        f.seek(backup_hdr_lba * SECTOR)
        f.write(bhdr)

def create_fat16(img_path, files):
    """创建 FAT16 文件系统映像, 写入 files dict {path: content}
    支持嵌套目录: /EFI/BOOT/BOOTX64.EFI"""
    img_size = ESP_SIZE
    bps = 512  # bytes per sector
    spc = 4    # sectors per cluster (2KB)
    reserved = 1
    nfats = 2
    root_entries = 512

    total_sectors = img_size // bps
    root_dir_sectors = (root_entries * 32 + bps - 1) // bps
    # FAT size: each cluster needs 2 bytes
    data_sectors_est = total_sectors - reserved - root_dir_sectors
    clusters_est = data_sectors_est // spc
    fat_sectors = (clusters_est * 2 + bps - 1) // bps
    data_sectors = total_sectors - reserved - nfats * fat_sectors - root_dir_sectors
    total_clusters = data_sectors // spc
    if total_clusters < 4085:
        spc = 1
        data_sectors_est = total_sectors - reserved - root_dir_sectors
        clusters_est = data_sectors_est // 1
        fat_sectors = (clusters_est * 2 + bps - 1) // bps
        data_sectors = total_sectors - reserved - nfats * fat_sectors - root_dir_sectors
        total_clusters = data_sectors // 1

    cluster_size = spc * bps
    img = bytearray(img_size)

    # Boot sector (BPB)
    bs = bytearray(bps)
    bs[0:3] = b'\xEB\x3C\x90'
    bs[3:11] = b'MSDOS5.0'
    struct.pack_into('<H', bs, 11, bps)
    bs[13] = spc
    struct.pack_into('<H', bs, 14, reserved)
    bs[16] = nfats
    struct.pack_into('<H', bs, 17, root_entries)
    struct.pack_into('<H', bs, 19, total_sectors if total_sectors < 65536 else 0)
    bs[21] = 0xF8
    struct.pack_into('<H', bs, 22, fat_sectors)
    struct.pack_into('<H', bs, 24, 63)
    struct.pack_into('<H', bs, 26, 255)
    struct.pack_into('<I', bs, 28, 0)
    struct.pack_into('<I', bs, 32, total_sectors if total_sectors >= 65536 else 0)
    bs[36] = 0x80
    bs[38] = 0x29
    bs[39:43] = b'\x01\x00\x00\x00'
    bs[43:54] = b'EFMOS ESP  '
    bs[54:62] = b'FAT16   '
    bs[510] = 0x55
    bs[511] = 0xAA
    img[0:bps] = bs

    fat_offset = reserved * bps
    fat = bytearray(fat_sectors * bps)
    fat[0:4] = b'\xF8\xFF\xFF\xFF'  # entries 0+1

    root_offset = fat_offset + nfats * fat_sectors * bps
    root_dir = bytearray(root_dir_sectors * bps)
    data_offset = root_offset + root_dir_sectors * bps
    next_cl = 2  # next free cluster

    # Track directory contents: {cluster_num: bytearray of dir entries}
    # cluster 0 = root dir (stored in root_dir separately)
    dir_contents = {}  # {parent_cluster: [(name8, ext3, attr, cluster, size)]}
    root_entries_list = []

    def alloc_cluster():
        nonlocal next_cl
        cl = next_cl
        next_cl += 1
        struct.pack_into('<H', fat, cl * 2, 0xFFFF)  # end of chain
        return cl

    def write_cluster_data(cl, data):
        """Write data to cluster cl in img"""
        off = data_offset + (cl - 2) * cluster_size
        end = min(off + len(data), len(img))
        img[off:end] = data[:end-off]

    def make_dir_entry(name8, ext3, attr, cluster, size):
        e = bytearray(32)
        e[0:8] = name8.encode('ascii') if isinstance(name8, str) else name8
        e[8:11] = ext3.encode('ascii') if isinstance(ext3, str) else ext3
        e[11] = attr
        struct.pack_into('<H', e, 20, (cluster >> 16) & 0xFFFF)
        struct.pack_into('<H', e, 26, cluster & 0xFFFF)
        struct.pack_into('<I', e, 28, size)
        return e

    def short_name_parts(filename):
        f = filename.upper()
        if '.' in f:
            n, ext = f.rsplit('.', 1)
            return n[:8].ljust(8), ext[:3].ljust(3)
        return f[:8].ljust(8), '   '

    # Build directory tree
    dir_map = {'': 0}  # path -> cluster (0 = root)

    for fpath, content in sorted(files.items()):
        parts = fpath.strip('/').split('/')
        # Create intermediate directories
        cur_path = ''
        parent_cl = 0
        for i in range(len(parts) - 1):
            cur_path = cur_path + '/' + parts[i] if cur_path else parts[i]
            if cur_path in dir_map:
                parent_cl = dir_map[cur_path]
                continue
            # Create directory
            cl = alloc_cluster()
            dir_map[cur_path] = cl
            n8, e3 = short_name_parts(parts[i])
            entry = make_dir_entry(n8, e3, 0x10, cl, 0)
            if parent_cl == 0:
                root_entries_list.append(entry)
            else:
                dir_contents.setdefault(parent_cl, []).append(entry)
            # Add . and .. entries for this directory
            dot = make_dir_entry('.       ', '   ', 0x10, cl, 0)
            ddot = make_dir_entry('..      ', '   ', 0x10, parent_cl, 0)
            dir_contents.setdefault(cl, []).append(dot)
            dir_contents.setdefault(cl, []).append(ddot)
            parent_cl = cl

        # Write file
        fname = parts[-1]
        n8, e3 = short_name_parts(fname)
        content_bytes = content if isinstance(content, bytes) else content.encode()
        content_len = len(content_bytes)
        num_clusters = max(1, (content_len + cluster_size - 1) // cluster_size) if content_len > 0 else 0

        file_cl = 0
        if content_len > 0:
            file_cl = next_cl
            for c in range(num_clusters):
                cl = next_cl + c
                if c == num_clusters - 1:
                    struct.pack_into('<H', fat, cl * 2, 0xFFFF)
                else:
                    struct.pack_into('<H', fat, cl * 2, cl + 1)
                start = c * cluster_size
                end = min(start + cluster_size, content_len)
                write_cluster_data(cl, content_bytes[start:end])
            next_cl += num_clusters

        entry = make_dir_entry(n8, e3, 0x20, file_cl, content_len)
        if parent_cl == 0:
            root_entries_list.append(entry)
        else:
            dir_contents.setdefault(parent_cl, []).append(entry)

    # Write root dir entries
    for i, e in enumerate(root_entries_list):
        root_dir[i*32:(i+1)*32] = e

    # Write subdirectory contents to clusters
    for cl, entries in dir_contents.items():
        data = bytearray()
        for e in entries:
            data.extend(e)
        # Pad to cluster size
        while len(data) < cluster_size:
            data.extend(b'\x00' * 32)
        write_cluster_data(cl, data)

    # Write FAT tables
    for i in range(nfats):
        off = fat_offset + i * fat_sectors * bps
        img[off:off + len(fat)] = fat
    # Write root dir
    img[root_offset:root_offset + len(root_dir)] = root_dir

    with open(img_path, 'wb') as f:
        f.write(img)

def main():
    src = os.path.dirname(os.path.abspath(__file__))

    # Create blank disk
    print("Creating blank disk.img...")
    with open('disk.img', 'wb') as f:
        f.truncate(DISK_SIZE)

    # GPT
    print("Writing GPT partition table...")
    create_gpt('disk.img')

    # ESP (FAT16)
    print("Creating FAT16 ESP partition...")
    esp_files = {}
    with open('bootloader/BOOTX64.EFI', 'rb') as f:
        esp_files['/EFI/BOOT/BOOTX64.EFI'] = f.read()

    create_fat16('/tmp/esp.img', esp_files)
    with open('disk.img', 'r+b') as f:
        f.seek(ESP_OFFSET)
        with open('/tmp/esp.img', 'rb') as ef:
            f.write(ef.read())
    os.unlink('/tmp/esp.img')

    # ext4 partition
    print("Creating ext4 partition...")
    with open('/tmp/ext.img', 'wb') as f:
        f.truncate(EXT_SIZE)
    subprocess.run(['mkfs.ext4', '-F', '-O', '^metadata_csum,^64bit', '/tmp/ext.img'],
                   check=True, capture_output=True)

    # Write files with debugfs
    print("Writing files to ext4...")
    files_to_write = [
        ('EFMOS', 'dir', None),
        ('EFMOS/kernel.elf', 'write', f'{src}/kernel/kernel.elf'),
        ('EFMOS/fileman.efs', 'write', f'{src}/efmsfile/fileman.efs'),
        ('EFMOS/setting.efs', 'write', f'{src}/efmsfile/setting.efs'),
        ('EFMOS/userman.efs', 'write', f'{src}/efmsfile/userman.efs'),
        ('EFMOS/efmlogin.efs', 'write', f'{src}/efmsfile/efmlogin.efs'),
        ('EFMOS/efmloader.efs', 'write', f'{src}/efmsfile/efmloader.efs'),
        ('EFMOS/efmshell.efs', 'write', f'{src}/efmsfile/efmshell.efs'),
        ('EFMOS/efmcompositor.efs', 'write', f'{src}/efmsfile/efmcompositor.efs'),
        ('EFMOS/efmAether.efs', 'write', f'{src}/efmsfile/efmAether.efs'),
        ('EFMOS/DRIVERS', 'dir', None),
        ('EFMOS/DRIVERS/01-ahci.drv', 'write', f'{src}/drivers/ahci.drv'),
        ('EFMOS/DRIVERS/02-Graphics.drv', 'write', f'{src}/drivers/Graphics.drv'),
        ('EFMOS/fonts', 'dir', None),
        ('EFMOS/fonts/sarasa-gothic-regular.ttf', 'write', f'{src}/fonts/sarasa-gothic-regular.ttf'),
        ('Program', 'dir', None),
        ('Program/gcc', 'dir', None),
        ('Program/gcc/gcc.efs', 'write', f'{src}/Program/gcc/gcc.efs'),
        ('Program/gcc/gcc_test.c',  'write', f'{src}/Program/gcc/gcc_test.c'),
        ('Program/gcc/gcc_min.c',   'write', f'{src}/Program/gcc/gcc_min.c'),
        ('Program/gcc/gcc_float.c', 'write', f'{src}/Program/gcc/gcc_float.c'),
        ('Program/gcc/gcc_sa.c',    'write', f'{src}/Program/gcc/gcc_sa.c'),
    ]
    cmds = []
    for path, op, src_file in files_to_write:
        if op == 'dir':
            cmds.append(f'mkdir {path}')
        elif op == 'write':
            cmds.append(f'write {src_file} {path}')
    # debugfs -f expects one command per line
    dbgscript = '\n'.join(cmds) + '\n'
    with tempfile.NamedTemporaryFile(mode='w', suffix='.dbg', delete=False) as tf:
        tf.write(dbgscript)
        dbgpath = tf.name
    subprocess.run(['debugfs', '-w', '/tmp/ext.img', '-f', dbgpath],
                   check=True, capture_output=True)
    os.unlink(dbgpath)

    # Write ext4 to disk
    print("Writing ext4 partition to disk...")
    with open('disk.img', 'r+b') as f:
        f.seek(EXT_OFFSET)
        with open('/tmp/ext.img', 'rb') as ef:
            f.write(ef.read())
    os.unlink('/tmp/ext.img')

    print("disk.img created successfully!")

if __name__ == '__main__':
    main()

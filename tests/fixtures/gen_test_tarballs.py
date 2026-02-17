#!/usr/bin/env python3
"""
Generate test tarballs for tar.c security tests.
Run: python3 tests/fixtures/gen_test_tarballs.py
"""

import gzip
import io
import os
import struct
import tarfile

FIXTURES_DIR = os.path.dirname(os.path.abspath(__file__))


def create_tar_header(name, size=0, mode=0o644, uid=0, gid=0, mtime=0,
                     typeflag=b'0', linkname=b'', uname=b'root', gname=b'root'):
    """Create a POSIX ustar tar header block (512 bytes)."""
    
    # Pad name to 100 bytes
    name_bytes = name.encode('utf-8')[:100].ljust(100, b'\x00')
    
    # Build header (pre-checksum)
    header = struct.pack(
        '@100s8s8s8s12s12s8s1s100s6s2s32s32s8s8s155s12s',
        name_bytes,
        b'%07o\x00' % mode,
        b'%07o\x00' % uid,
        b'%07o\x00' % gid,
        b'%011o\x00' % size,
        b'%011o\x00' % mtime,
        b'        ',  # checksum placeholder
        typeflag,
        linkname.ljust(100, b'\x00'),
        b'ustar\x00',
        b'00',
        uname.ljust(32, b'\x00'),
        gname.ljust(32, b'\x00'),
        b'\x00' * 8,   # devmajor
        b'\x00' * 8,   # devminor
        b'\x00' * 155, # prefix
        b'\x00' * 12   # padding to 512
    )
    
    # Calculate checksum (sum of all bytes in header, treated as unsigned)
    checksum = sum(header)
    checksum_bytes = b'%06o\x00 ' % checksum
    
    # Insert checksum at offset 148
    header = header[:148] + checksum_bytes + header[156:]
    
    return header


def make_tarball(name, entries):
    """Create a .tar.gz file from list of (name, content, typeflag) tuples."""
    
    buf = io.BytesIO()
    
    for entry_name, content, typeflag in entries:
        if isinstance(content, str):
            content = content.encode('utf-8')
        
        size = len(content) if typeflag == b'0' else 0
        
        # Create header
        if typeflag == b'2':  # Symlink
            header = create_tar_header(entry_name, size=0, typeflag=b'2', linkname=content)
        else:
            header = create_tar_header(entry_name, size=size, typeflag=typeflag)
        
        buf.write(header)
        
        # Write content for regular files
        if typeflag == b'0' and content:
            buf.write(content)
            # Pad to 512-byte boundary
            padding = 512 - (len(content) % 512)
            if padding != 512:
                buf.write(b'\x00' * padding)
    
    # Write two zero blocks (end of archive)
    buf.write(b'\x00' * 512)
    buf.write(b'\x00' * 512)
    
    # Compress with gzip
    tar_data = buf.getvalue()
    gz_path = os.path.join(FIXTURES_DIR, name)
    
    with gzip.open(gz_path, 'wb', compresslevel=9) as gz:
        gz.write(tar_data)
    
    print(f"Created: {gz_path} ({len(tar_data)} bytes tar, {os.path.getsize(gz_path)} bytes gz)")


def create_corrupted():
    """Create a truncated/corrupted tar.gz."""
    
    # Valid gzip header + partial tar data, then cut off
    buf = io.BytesIO()
    
    # Write a valid tar with one entry, but truncate the gzip stream
    with gzip.GzipFile(fileobj=buf, mode='wb', compresslevel=9) as gz:
        # One complete file entry
        header = create_tar_header('test.txt', size=100, typeflag=b'0')
        gz.write(header)
        gz.write(b'A' * 100)
        gz.write(b'\x00' * 412)  # padding
        # Missing end-of-archive zero blocks
    
    # Now corrupt it by truncating
    data = buf.getvalue()
    truncated = data[:-20]  # Chop off last 20 bytes
    
    path = os.path.join(FIXTURES_DIR, 'corrupted.tar.gz')
    with open(path, 'wb') as f:
        f.write(truncated)
    
    print(f"Created: {path} ({len(truncated)} bytes, truncated)")


def create_path_traversal():
    """Create tar with path traversal attack."""
    
    make_tarball('path_traversal.tar.gz', [
        ('../../etc/passwd', b'root:x:0:0:root:/root:/bin/bash\n', b'0'),
        ('normal.txt', b'This is a normal file.\n', b'0'),
    ])


def create_symlink_escape():
    """Create tar with symlink pointing outside extraction dir."""
    
    make_tarball('symlink_escape.tar.gz', [
        ('escape', b'/etc/passwd', b'2'),  # Symlink to absolute path
        ('escape2', b'../../../etc/passwd', b'2'),  # Symlink with relative traversal
        ('legit.txt', b'Safe content.\n', b'0'),
    ])


def create_hardlink_attack():
    """Create tar with hardlink (should be rejected)."""
    
    make_tarball('hardlink_attack.tar.gz', [
        ('target.txt', b'Target content.\n', b'0'),
        ('link.txt', b'target.txt', b'1'),  # Hard link to target.txt
    ])


def create_device_file():
    """Create tar with device files (should be rejected)."""
    
    make_tarball('device_file.tar.gz', [
        ('null', b'', b'3'),  # Character device
        ('zero', b'', b'3'),  # Character device
    ])


def create_valid():
    """Create a valid tar.gz for baseline testing."""
    
    make_tarball('valid.tar.gz', [
        ('dir/', b'', b'5'),
        ('dir/file.txt', b'Hello, World!\n', b'0'),
        ('dir/executable', b'#!/bin/sh\necho hi\n', b'0'),
        ('dir/link', b'file.txt', b'2'),  # Symlink within dir
    ])


if __name__ == '__main__':
    print("Generating test tarballs...")
    print(f"Output directory: {FIXTURES_DIR}")
    print()
    
    create_corrupted()
    create_path_traversal()
    create_symlink_escape()
    create_hardlink_attack()
    create_device_file()
    create_valid()
    
    print()
    print("Done. Verify with: tar tvzf tests/fixtures/<name>.tar.gz")

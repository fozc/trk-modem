#!/usr/bin/env python3
"""
Post-Build Script: Add Version, DateTime and Git Hash to Filename
Usage: python post-build.py <source_file> [version_header_file]

Examples:
    python post-build.py firmware.bin
    # Output: firmware_01102025_1430_a3b5c7d.bin
    
    python post-build.py firmware.bin version.h
    # Output: firmware_v1.0.3_01102025_1430_a3b5c7d.bin
"""

import os
import sys
import subprocess
import shutil
import re
from pathlib import Path
from datetime import datetime

# Script'in bulunduğu dizini al
SCRIPT_DIR = Path(__file__).parent.resolve()


def get_git_root():
    """Get git repository root directory"""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--show-toplevel'],
            capture_output=True,
            text=True,
            check=True,
            cwd=SCRIPT_DIR
        )
        return Path(result.stdout.strip())
    except:
        return SCRIPT_DIR  # Fallback to script directory


def get_git_hash_with_status():
    """
    Get short git commit hash (7 characters) with status indicator
    Returns: hash with suffix:
    - hash + "-d" if there are uncommitted changes
    - hash + "--" if working directory is clean
    """
    try:
        git_root = get_git_root()
        
        # Get short hash
        result = subprocess.run(
            ['git', 'rev-parse', '--short=7', 'HEAD'],
            capture_output=True,
            text=True,
            check=True,
            cwd=git_root
        )
        git_hash = result.stdout.strip()
        
        # Check if working directory is clean
        status_result = subprocess.run(
            ['git', 'status', '--porcelain'],
            capture_output=True,
            text=True,
            check=True,
            cwd=git_root
        )
        
        # If status output is empty, working directory is clean
        if status_result.stdout.strip():
            return f"{git_hash}-d"  # Dirty (uncommitted changes)
        else:
            return f"{git_hash}--"  # Clean
            
    except Exception as e:
        print(f"Warning: Could not get git status: {e}")
        return "unknown--"


def parse_version_from_header(header_file):
    """
    Parse version numbers from header file
    Returns: version string like "1.0.3" or None if not found
    """
    try:
        with open(header_file, 'r') as f:
            content = f.read()
        
        # Find VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH
        major = re.search(r'#define\s+VERSION_MAJOR\s+\((\d+)\)', content)
        minor = re.search(r'#define\s+VERSION_MINOR\s+\((\d+)\)', content)
        patch = re.search(r'#define\s+VERSION_PATCH\s+\((\d+)\)', content)
        
        if major and minor and patch:
            return f"{major.group(1)}.{minor.group(1)}.{patch.group(1)}"
        
        return None
    except Exception as e:
        print(f"Warning: Could not parse version from {header_file}: {e}")
        return None


def rename_file_with_hash(source_file, version_header=None):
    """
    Rename file by adding version, datetime and git hash to filename
    
    Args:
        source_file: Source binary file (absolute or relative path)
        version_header: Optional version.h file path for version parsing only
    """
    
    # Dosya yollarını script dizinine göre çözümle
    source_path = Path(source_file)
    if not source_path.is_absolute():
        source_path = (SCRIPT_DIR / source_file).resolve()
    
    # Check if source file exists
    if not source_path.exists():
        print(f"Error: File '{source_path}' not found")
        return False
    
    # Get file info
    file_name = source_path.stem
    file_ext = source_path.suffix
    file_dir = source_path.parent
    
    # Get git hash
    git_hash = get_git_hash_with_status()
    
    # Get current datetime
    now = datetime.now()
    datetime_str = now.strftime("%d%m%Y_%H%M")  # DDMMYYYY_HHMM
    
    # Parse version if header file provided
    version_str = ""
    if version_header:
        version_header_path = Path(version_header)
        if not version_header_path.is_absolute():
            version_header_path = (SCRIPT_DIR / version_header).resolve()
        
        if version_header_path.exists():
            version = parse_version_from_header(version_header_path)
            if version:
                version_str = f"v{version}_"
                print(f"Found version: {version}")
        else:
            print(f"Warning: Version header '{version_header_path}' not found")
    
    # Create new filename: filename_vX.Y.Z_DDMMYYYY_HHMM_hash.ext
    new_name = f"{file_name}_{version_str}{datetime_str}_{git_hash}{file_ext}"
    output_path = file_dir / new_name
    
    # Copy file to new name
    try:
        shutil.copy2(source_path, output_path)
        print(f"Created: {output_path}")
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python post-build.py <source_file> [version_header_file]")
        print("\nExamples:")
        print("  python post-build.py firmware.bin")
        print("  python post-build.py ../build/firmware.bin")
        print("  python post-build.py firmware.bin version.h")
        print("  python post-build.py ../build/firmware.bin ../Inc/version.h")
        print(f"\nScript directory: {SCRIPT_DIR}")
        print("\nNote: To update version.h file, use pre-build.py before building")
        sys.exit(1)
    
    source_file = sys.argv[1]
    version_header = sys.argv[2] if len(sys.argv) > 2 else None
    
    print(f"Working directory: {SCRIPT_DIR}")
    
    if rename_file_with_hash(source_file, version_header):
        sys.exit(0)
    else:
        sys.exit(1)
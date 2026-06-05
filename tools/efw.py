import sys
import os
import crcmod
import argparse
import pathlib

EFIL_VER : int = 1

parser = argparse.ArgumentParser(
    description="Convert binary firmware file to efile format"
)

parser.add_argument(
    "-s", "--source-file",
    help = "Source file of firmware",
    required = True,
    type=pathlib.Path
)

parser.add_argument(
    "-o", "--output-file",
    help = "output file name",
    required = False,
)

parser.add_argument(
    "-v", "--version-file",
    help = "The file that consist of all necessery information for efile file.",
    required = False,
    type=pathlib.Path
)

parser.add_argument(
    "--device-type",
    help = "The type of the device that this firmware is for.",
    type=str,
    required = False
)
parser.add_argument(
    "--device-model",
    help = "The model identifier for the device that the firmware belongs to.",
    type=str,
    required = False
)
parser.add_argument(
    "--fw-type",
    help = "e.g. bootloader, application, config, etc.",
    type=str,
    required = False
)
parser.add_argument(
    "--fw-version",
    help = "major.minor.patch.extra",
    nargs=4,
    type=str,
    required = False
)

args = parser.parse_args()

source_file_path : str  = args.source_file
version_file_path : str = args.version_file

print(f"Soure File: {source_file_path} Version File: {version_file_path}")

if not os.path.isfile(source_file_path):
    sys.exit("Source file does not exist!")

if args.version_file is not None:
    if not os.path.isfile(version_file_path):
        sys.exit("Version file does not exist!")

    with open(version_file_path, 'r') as file:
        ver_file : list = file.read().split("(")

    if len(ver_file) < 8:
        sys.exit("Version file is not correct!")

    dev_type  : str = ver_file[1].split(")")[0]
    dev_model : str = ver_file[2].split(")")[0]
    fw_type   : str = ver_file[3].split(")")[0]
    ver_major : str = ver_file[4].split(")")[0]
    ver_minor : str = ver_file[5].split(")")[0]
    ver_patch : str = ver_file[6].split(")")[0]
    ver_extra : str = ver_file[7].split(")")[0]

    print(f"Debug - dev_type: {dev_type}, dev_model: {dev_model}, fw_type: {fw_type}")
    print(f"Debug - versions: {ver_major}.{ver_minor}.{ver_patch}.{ver_extra}")
else:
    # Validate command line arguments when version file is not used
    if not all([args.device_type, args.device_model, args.fw_type, args.fw_version]):
        sys.exit("When version file is not provided, all device and firmware parameters are required!")
    
    dev_type  : str = args.device_type
    dev_model : str = args.device_model
    fw_type   : str = args.fw_type
    ver_major : str = args.fw_version[0]
    ver_minor : str = args.fw_version[1]
    ver_patch : str = args.fw_version[2]
    ver_extra : str = args.fw_version[3]

source_file_name : str = os.path.basename(source_file_path)

#Open the source file as binary read mode
with open(source_file_path, 'rb') as file:
    source_file_content : bytes = file.read()
    print(f"Source File Size: {len(source_file_content)}")

if args.output_file is not None:
    output_file = args.output_file + ".efile"
else:
    output_file = source_file_name + ".efile"

with open(output_file, "wb") as file:
    #magic code
    file.write(0x5746452A.to_bytes(4, byteorder='little')) # LIFE 0x5746452A -> *EFW
    #Firmware info
    try:
        file.write((EFIL_VER    ).to_bytes(1, byteorder='little'))
        file.write(int(dev_type ).to_bytes(1, byteorder='little'))
        file.write(int(dev_model).to_bytes(1, byteorder='little'))
        file.write(int(fw_type  ).to_bytes(1, byteorder='little'))
        #Fw version
        file.write(int(ver_major).to_bytes(1, byteorder='little'))
        file.write(int(ver_minor).to_bytes(1, byteorder='little'))
        file.write(int(ver_patch).to_bytes(1, byteorder='little'))
        file.write(int(ver_extra).to_bytes(1, byteorder='little'))
    except ValueError as e:
        sys.exit(f"Error converting parameters to integers: {e}")
    #firmware file size
    print(f"FW Image Size: {hex(len(source_file_content))}")
    file_size = len(source_file_content).to_bytes(4, byteorder='little')
    file.write(file_size)
    #firmware crc
    crc32_func  = crcmod.mkCrcFun(0x104c11db7)
    file_crc : int = crc32_func(source_file_content)
    file.write(file_crc.to_bytes(4, byteorder='little'))
    print(f"CRC: {hex(file_crc)}")
    #firmware MAC
    SECRET_CONSTANTS = [0x9E3779B9, 0x85EBCA77, 0xC2B2AE3D, 0x27D4EB2F]
    fw_mac = 0xF2F3F1F0
    fw_mac ^= file_crc
    for i, b in enumerate(source_file_content):
        # Position ve secret constant dependency
        secret_idx = i % 4
        enhanced_byte = (b + (i & 0xFF) + SECRET_CONSTANTS[secret_idx]) & 0xFF
        fw_mac = (((fw_mac >> 13) ^ (fw_mac << 7)) * enhanced_byte) & 0xFFFFFFFF
        # Extra mixing every 64 bytes
        if (i + 1) % 64 == 0:
            fw_mac ^= SECRET_CONSTANTS[(i >> 6) % 4]
    fw_mac ^= 0x1F3A7D0E
    fw_mac ^= len(source_file_content)
    fw_mac ^= file_crc  

    file.write(fw_mac.to_bytes(4, byteorder='little'))
    print(f"Fw Mac: {hex(fw_mac)}")
    #Reserve Area
    file.write((0x01020304DEADBEEF).to_bytes(8, byteorder='little'))
    #firmware image
    file.write(source_file_content)

# Get the output file size
output_file_size = os.path.getsize(output_file)
print(f"Output File Size: {output_file_size}")
   
print("DONE")






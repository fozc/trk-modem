"""Generate synthetic reference vectors with Python hashlib/hmac.

The production SHA/HMAC implementations are never imported or executed.
No product keys are read. Generated headers are tracked test fixtures.
"""
from pathlib import Path
import hashlib
import hmac

root = Path(__file__).resolve().parents[1] / "fixtures"
root.mkdir(exist_ok=True)


def header(name, guard):
    return (f"/*\n * {name}\n * Author: Fatih Ozcan\n"
            " *         fatihozcan@gmail.com\n"
            " * Independent Python reference values; synthetic inputs.\n */\n"
            f"#ifndef {guard}\n#define {guard}\n"
            "#include <stddef.h>\n#include <stdint.h>\n\n")


def digest_bytes(value):
    parts = [f"0x{byte:02X}U" for byte in value]
    return ",\n".join("        " + ", ".join(parts[i:i+8])
                       for i in range(0, 32, 8))


data = bytes((i * 29 + 17) & 255 for i in range(129))
sha = header("sha256_block_vectors.h", "SHA256_BLOCK_VECTORS_H")
sha += "typedef struct\n{\n    size_t length;\n    uint8_t digest[32];\n} sha256_test_vector_t;\n\n"
sha += "static const sha256_test_vector_t sha256_block_vectors[] =\n{\n"
for length in (0, 1, 55, 56, 63, 64, 65, 127, 128, 129):
    sha += f"    {{{length}U, {{\n{digest_bytes(hashlib.sha256(data[:length]).digest())}\n    }}}},\n"
sha += "};\n#endif\n/*** end of file ***/\n"
(root / "sha256_block_vectors.h").write_text(sha, encoding="ascii", newline="\n")

key = bytes((i * 11 + 5) & 255 for i in range(131))
mac = header("hmac_block_vectors.h", "HMAC_BLOCK_VECTORS_H")
mac += "typedef struct\n{\n    size_t key_len;\n    size_t data_len;\n    uint8_t digest[32];\n} hmac_test_vector_t;\n\n"
mac += "static const hmac_test_vector_t hmac_block_vectors[] =\n{\n"
for key_len in (0, 1, 63, 64, 65, 128, 131):
    for data_len in (0, 1, 55, 56, 63, 64, 65, 129):
        expected = hmac.new(key[:key_len], data[:data_len], hashlib.sha256).digest()
        mac += f"    {{{key_len}U, {data_len}U, {{\n{digest_bytes(expected)}\n    }}}},\n"
mac += "};\n#endif\n/*** end of file ***/\n"
(root / "hmac_block_vectors.h").write_text(mac, encoding="ascii", newline="\n")
print("Generated 10 SHA-256 and 56 HMAC boundary vectors.")

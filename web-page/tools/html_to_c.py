#!/usr/bin/env python3
"""
Convert HTML file to C array for embedding

Usage:
    python html_to_c.py <input_html> [output_name]
    python html_to_c.py <input_html> [output_name] --minify
    python html_to_c.py <input_html> [output_name] --minify --brotli
    python html_to_c.py <input_html> --output-dir outdir
    
Example:
    python html_to_c.py web-page/board_status.html board_status
    python html_to_c.py web-page/board_status.html  # uses 'board_status' from filename
    python html_to_c.py web-page/board_status.html --output-dir ../Application/web-server
    This will generate: board_status_html.c and board_status_html.h
"""

import os
import sys
import gzip
import importlib.util

# Try to import brotli (optional, better compression)
try:
    import brotli
    BROTLI_AVAILABLE = True
except ImportError:
    BROTLI_AVAILABLE = False

def escape_string_for_c(text):
    """Escape string for C with proper encoding"""
    result = []
    i = 0
    while i < len(text):
        char = text[i]
        code = ord(char)
        
        # ASCII printable characters
        if 32 <= code <= 126:
            if char == '\\':
                result.append('\\\\')
            elif char == '"':
                result.append('\\"')
            elif char == '?':
                result.append('\\?')  # Avoid trigraphs
            else:
                result.append(char)
        # Common escape sequences
        elif char == '\t':
            result.append('\\t')
        elif char == '\r':
            result.append('\\r')
        elif char == '\n':
            result.append('\\n')
        # Non-ASCII characters - encode as UTF-8 hex
        else:
            utf8_bytes = char.encode('utf-8')
            for byte in utf8_bytes:
                result.append(f'\\x{byte:02X}')
            # Check if next character is a hex digit - if so, break string
            if i + 1 < len(text):
                next_char = text[i + 1]
                if next_char in '0123456789ABCDEFabcdef':
                    result.append('" "')
        i += 1
    
    return ''.join(result)

def write_c_array(html_content, output_name, output_dir, source_html_file, compression_type=None):
    """Convert HTML file to C array with html_resource_t structure
    
    Args:
        html_content: Raw bytes (if compressed) or string (if plain)
        output_name: Base name for output files
        output_dir: Directory for output files
        source_html_file: Original source file path
        compression_type: None, 'gzip', or 'br' (brotli)
    """
    
    array_name = f"{output_name}_html"
    output_c = os.path.join(output_dir, f"{array_name}.c")
    output_h = os.path.join(output_dir, f"{array_name}.h")
    guard_name = f"{array_name.upper()}_H"
    
    # Function name for getter
    function_name = f"get_{output_name}_html"
    
    is_compressed = compression_type is not None
    
    # Generate C file
    with open(output_c, 'w', encoding='utf-8') as f:
        f.write(f'/* {array_name}.c - embedded {os.path.basename(source_html_file)} */\n')
        f.write(f'#include "{array_name}.h"\n\n')
        
        # Always use uint8_t for data array (works for both text and binary)
        f.write(f'static const uint8_t {array_name}_data[] = {{\n')
        
        # Write binary data as hex array
        if is_compressed:
            # Compressed: binary data
            for i in range(0, len(html_content), 12):
                chunk = html_content[i:i+12]
                hex_values = ', '.join(f'0x{b:02X}' for b in chunk)
                if i + 12 < len(html_content):
                    f.write(f'    {hex_values},\n')
                else:
                    f.write(f'    {hex_values}\n')
        else:
            # Plain text: convert to bytes and write as hex
            text_bytes = html_content.encode('utf-8')
            for i in range(0, len(text_bytes), 12):
                chunk = text_bytes[i:i+12]
                hex_values = ', '.join(f'0x{b:02X}' for b in chunk)
                if i + 12 < len(text_bytes):
                    f.write(f'    {hex_values},\n')
                else:
                    f.write(f'    {hex_values}\n')
        
        f.write('};\n\n')
        
        # Generate html_resource_t structure
        data_length = len(html_content) if is_compressed else len(html_content.encode('utf-8'))
        
        # Determine compression flag value: 0=none, 1=gzip, 2=brotli
        if compression_type == 'gzip':
            compression_flag = 1
        elif compression_type == 'br':
            compression_flag = 2
        else:
            compression_flag = 0
            
        f.write(f'static const html_resource_t {array_name}_resource = {{\n')
        f.write(f'    .data = {array_name}_data,\n')
        f.write(f'    .length = {data_length},\n')
        f.write(f'    .is_gzipped = {compression_flag},\n')
        f.write(f'    .content_type = "text/html"\n')
        f.write('};\n\n')
        
        # Generate getter function
        f.write(f'const html_resource_t* {function_name}(void) {{\n')
        f.write(f'    return &{array_name}_resource;\n')
        f.write('}\n')
    
    # Generate H file
    with open(output_h, 'w', encoding='utf-8') as f:
        f.write(f'/* {array_name}.h - embedded {os.path.basename(source_html_file)} header */\n')
        f.write(f'#ifndef {guard_name}\n')
        f.write(f'#define {guard_name}\n\n')
        f.write('#include "html_resources.h"\n\n')
        
        f.write('/**\n')
        f.write(f' * @brief Get {output_name.replace("_", " ")} page HTML resource\n')
        f.write(f' * @return Pointer to {output_name} HTML resource structure\n')
        f.write(' */\n')
        f.write(f'const html_resource_t* {function_name}(void);\n\n')
        f.write(f'#endif /* {guard_name} */\n')
    
    print(f"Generated {output_c} ({len(html_content)} bytes)")
    print(f"Generated {output_h}")

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description="Convert HTML file to C array for embedding. Optionally minify and compress.")
    parser.add_argument("input_html", help="Input HTML file path")
    parser.add_argument("output_name", nargs='?', default=None, help="Output array name (e.g. board_status). If not provided, uses input filename without extension.")
    parser.add_argument("--output-dir", "-o", default=None, help="Output directory (default: src/ relative to script)")
    parser.add_argument("--minify", "-m", action="store_true", help="Minify HTML before embedding")
    parser.add_argument("--gzip", "-g", action="store_true", help="Gzip compress HTML")
    parser.add_argument("--brotli", "-b", action="store_true", help="Brotli compress HTML (better than gzip)")
    args = parser.parse_args()

    # Check for conflicting options
    if args.gzip and args.brotli:
        print("Error: Cannot use both --gzip and --brotli. Choose one.")
        sys.exit(1)
    
    if args.brotli and not BROTLI_AVAILABLE:
        print("Error: Brotli not installed. Install with: pip install brotli")
        sys.exit(1)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    # Determine output directory
    if args.output_dir:
        output_dir = args.output_dir
        # Make absolute if relative
        if not os.path.isabs(output_dir):
            output_dir = os.path.join(os.getcwd(), output_dir)
    else:
        output_dir = os.path.join(project_root, 'src')
    
    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    input_html = args.input_html
    
    # Determine output name
    if args.output_name:
        output_name = args.output_name
    else:
        # Extract filename without extension from input
        input_basename = os.path.basename(input_html)
        output_name = os.path.splitext(input_basename)[0]
        print(f"Using output name from input file: '{output_name}'")

    # Make input path absolute if relative
    if not os.path.isabs(input_html):
        input_html = os.path.join(project_root, input_html)

    if not os.path.exists(input_html):
        print(f"Error: {input_html} not found")
        sys.exit(1)

    # Read HTML content
    with open(input_html, 'r', encoding='utf-8') as f:
        html_content = f.read()
    
    original_size = len(html_content)
    print(f"Original: {original_size} bytes")
    
    # Minify if requested
    if args.minify:
        minify_path = os.path.join(script_dir, "minify_html.py")
        spec = importlib.util.spec_from_file_location("minify_html", minify_path)
        if spec and spec.loader:
            minify_html = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(minify_html)
            minifier = minify_html.SafeHTMLMinifier()
            html_content = minifier.minify_html(html_content)
            minified_size = len(html_content)
            reduction = ((original_size - minified_size) / original_size) * 100
            print(f"Minified: {minified_size} bytes ({reduction:.1f}% reduction)")
        else:
            print("Error: Could not import minify_html.py")
            sys.exit(1)
    
    # Compress if requested
    compression_type = None
    if args.brotli:
        pre_compress_size = len(html_content)
        html_content = brotli.compress(html_content.encode('utf-8'), quality=11)
        compressed_size = len(html_content)
        reduction = ((pre_compress_size - compressed_size) / pre_compress_size) * 100
        print(f"Brotli: {compressed_size} bytes ({reduction:.1f}% reduction)")
        compression_type = 'br'
    elif args.gzip:
        pre_compress_size = len(html_content)
        html_content = gzip.compress(html_content.encode('utf-8'), compresslevel=9)
        compressed_size = len(html_content)
        reduction = ((pre_compress_size - compressed_size) / pre_compress_size) * 100
        print(f"Gzipped: {compressed_size} bytes ({reduction:.1f}% reduction)")
        compression_type = 'gzip'
    
    # Write C array
    write_c_array(html_content, output_name, output_dir, input_html, compression_type)
    
    # Final summary
    final_size = len(html_content)
    total_reduction = ((original_size - final_size) / original_size) * 100
    print(f"\nTotal: {original_size} -> {final_size} bytes ({total_reduction:.1f}% total reduction)")
    print(f"Output directory: {output_dir}")
    print(f"OK - HTML embedded successfully as '{output_name}_html'!")

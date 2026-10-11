#!/usr/bin/env python3
"""
Safe HTML minifier for embedded systems
Preserves functionality while reducing file size

Features:
- Removes HTML comments
- Removes unnecessary whitespace between tags
- Minifies CSS (preserves selectors and string literals)
- Minifies JavaScript (preserves string literals)
- Safe for C array conversion

Usage:
    python minify_html.py <input.html> <output.html>
"""

import re
import sys
import os
import shutil
import subprocess

class SafeHTMLMinifier:
    def __init__(self):
        self.stats = {
            'original_size': 0,
            'minified_size': 0,
            'comments_removed': 0,
            'whitespace_reduced': 0
        }
    
    def minify_css(self, css_content):
        """Minify CSS while preserving selector spaces and string literals"""
        # Remove CSS comments
        css = re.sub(r'/\*.*?\*/', '', css_content, flags=re.DOTALL)
        
        # Preserve string literals - use list of tuples
        strings = []
        string_counter = [0]
        
        def save_string(match):
            idx = string_counter[0]
            string_counter[0] += 1
            placeholder = f"___CSS_STR_{idx}___"
            strings.append((placeholder, match.group(0)))
            return placeholder
        
        # Save strings (both ' and ")
        css = re.sub(r'"(?:[^"\\]|\\.)*"', save_string, css)
        css = re.sub(r"'(?:[^'\\]|\\.)*'", save_string, css)
        
        # Remove newlines and excessive spaces
        css = re.sub(r'\s+', ' ', css)
        
        # Remove spaces around CSS syntax (but NOT in selectors)
        css = re.sub(r'\s*{\s*', '{', css)  # { } brackets
        css = re.sub(r'\s*}\s*', '}', css)
        css = re.sub(r'\s*:\s*', ':', css)  # property: value
        css = re.sub(r'\s*;\s*', ';', css)  # statement end
        css = re.sub(r';\s*}', '}', css)    # last property before }
        
        # Restore string literals in reverse order
        for placeholder, original in reversed(strings):
            css = css.replace(placeholder, original)
        
        return css.strip()
    
    def minify_javascript(self, js_content):
        """Parse JavaScript and preserve globals, JSON keys and literals."""
        node = shutil.which('node')
        if node is None:
            raise RuntimeError('Node.js >=18 is required for --minify')
        helper = os.path.join(os.path.dirname(__file__), 'minify_js.cjs')
        result = subprocess.run(
            [node, helper], input=js_content, encoding='utf-8',
            capture_output=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(
                'JavaScript minification failed. Run npm ci --prefix '
                'web-page/tools first.\n' + result.stderr.strip())
        return result.stdout

    def minify_html(self, html_content):
        """Minify markup without rewriting JavaScript/CSS string content."""
        self.stats['original_size'] = len(html_content.encode('utf-8'))
        blocks = []

        def preserve(block):
            index = len(blocks)
            blocks.append(block)
            return f'__PRESERVED_{index}__'

        def minify_script_tag(match):
            return preserve('<script>' +
                            self.minify_javascript(match.group(1)) +
                            '</script>')

        def minify_style_tag(match):
            return preserve('<style>' + self.minify_css(match.group(1)) +
                            '</style>')

        html = re.sub(r'<script[^>]*>(.*?)</script>', minify_script_tag,
                      html_content, flags=re.DOTALL | re.IGNORECASE)
        html = re.sub(r'<style[^>]*>(.*?)</style>', minify_style_tag,
                      html, flags=re.DOTALL | re.IGNORECASE)
        for tag in ('pre', 'textarea'):
            html = re.sub(r'<' + tag + r'[^>]*>.*?</' + tag + r'>',
                          lambda match: preserve(match.group(0)), html,
                          flags=re.DOTALL | re.IGNORECASE)

        original_comments = html.count('<!--')
        html = re.sub(r'<!--(?!\[if).*?-->', '', html, flags=re.DOTALL)
        self.stats['comments_removed'] = original_comments - html.count('<!--')
        original_whitespace = len(html.encode('utf-8'))
        html = re.sub(r'>\s+<', '><', html)
        html = '\n'.join(line.strip() for line in html.split('\n'))
        html = re.sub(r'\n\s*\n', '\n', html)
        html = re.sub(r' {2,}', ' ', html)
        self.stats['whitespace_reduced'] = (
            original_whitespace - len(html.encode('utf-8')))

        for index, block in enumerate(blocks):
            html = html.replace(f'__PRESERVED_{index}__', block)
        self.stats['minified_size'] = len(html.encode('utf-8'))
        return html

    def print_stats(self, filename):
        """Print minification statistics"""
        reduction = self.stats['original_size'] - self.stats['minified_size']
        reduction_pct = (reduction / self.stats['original_size'] * 100) if self.stats['original_size'] > 0 else 0
        
        print(f"\nMinification results for {filename}:")
        print(f"  Original size:    {self.stats['original_size']:6d} bytes")
        print(f"  Minified size:    {self.stats['minified_size']:6d} bytes")
        print(f"  Reduction:        {reduction:6d} bytes ({reduction_pct:.1f}%)")
        print(f"  Comments removed: {self.stats['comments_removed']:6d}")
        print(f"  Whitespace saved: {self.stats['whitespace_reduced']:6d} bytes")

def main():
    if len(sys.argv) != 3:
        print("Usage: python minify_html.py <input.html> <output.html>")
        print("\nExample:")
        print("  python minify_html.py web-page/index.html web-page/index.min.html")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    if not os.path.exists(input_file):
        print(f"Error: Input file '{input_file}' not found")
        sys.exit(1)
    
    # Read input HTML
    try:
        with open(input_file, 'r', encoding='utf-8') as f:
            html_content = f.read()
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)
    
    # Minify
    minifier = SafeHTMLMinifier()
    minified_html = minifier.minify_html(html_content)
    
    # Write output
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(minified_html)
    except Exception as e:
        print(f"Error writing file: {e}")
        sys.exit(1)
    
    # Print statistics
    minifier.print_stats(os.path.basename(input_file))
    print(f"\nOutput written to: {output_file}")

if __name__ == '__main__':
    main()

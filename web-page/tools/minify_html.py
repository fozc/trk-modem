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
        """Minify JavaScript while preserving string/regex literals.

        Literal protection uses a single-pass tokenizer instead of
        sequential regex substitution: regex pairing breaks when an
        apostrophe appears inside a regex literal (e.g. /[&<>"']/g),
        which caused the old version to swallow JS syntax between
        strings and to expose string contents to the operator-space
        stripper (mangling e.g. 'Feeder {n}' into 'Feeder{n}')."""

        def prev_nonspace(out):
            for k in range(len(out) - 1, -1, -1):
                if out[k] not in ' \t\r\n':
                    return out[k]
            return None

        def is_regex_position(out):
            """True if a '/' at this point starts a regex literal:
            previous non-space emitted char is not an operand end."""
            prev = prev_nonspace(out)
            if prev is None:
                return True
            # After these chars a '/' can only start a regex, not a division
            return prev in r'([,=:!&|?{;+\-*%<>~^'

        def scan_string(src, i, quote):
            """Return index of the closing quote (len(src) if unterminated)."""
            j = i + 1
            while j < len(src):
                c = src[j]
                if c == '\\':
                    j += 2
                    continue
                if c == quote:
                    return j
                if c == '\n' and quote != '`':
                    return len(src)  # unterminated: bail out
                j += 1
            return j

        def scan_regex(src, i):
            """Return index just past a regex literal starting at src[i]=='/'. """
            j = i + 1
            in_class = False
            while j < len(src):
                c = src[j]
                if c == '\\':
                    j += 2
                    continue
                if c == '[':
                    in_class = True
                elif c == ']':
                    in_class = False
                elif c == '/' and not in_class:
                    j += 1
                    while j < len(src) and src[j].isalpha():
                        j += 1  # flags (g, i, m, ...)
                    return j
                elif c == '\n':
                    return i + 1  # not a regex after all: treat '/' as plain
                j += 1
            return j

        def minify_template(tpl):
            """Strip per-line indentation from HTML-bearing template literals."""
            if '<' in tpl and '>' in tpl:
                body = tpl[1:-1]  # drop the surrounding backticks
                lines = [l.strip() for l in body.split('\n') if l.strip()]
                return '`' + ''.join(lines) + '`'
            return tpl

        strings = []
        string_counter = [0]
        out = []
        i = 0
        n = len(js_content)
        while i < n:
            c = js_content[i]
            # Line comment - but '//' right after ':' is a URL (http://...),
            # preserved as plain text like the old (?<!:) behaviour.
            if c == '/' and js_content.startswith('//', i) and prev_nonspace(out) != ':':
                nl = js_content.find('\n', i)
                i = n if nl == -1 else nl
                continue
            if c == '/' and js_content.startswith('/*', i):
                end = js_content.find('*/', i)
                i = n if end == -1 else end + 2
                continue
            if c in ('"', "'", '`'):
                endq = scan_string(js_content, i, c)
                lit = js_content[i:endq + 1] if endq < n else js_content[i:]
                if c == '`':
                    lit = minify_template(lit)
                idx = string_counter[0]
                string_counter[0] += 1
                placeholder = f"___JS_STR_{idx}___"
                strings.append((placeholder, lit))
                out.append(placeholder)
                i = endq + 1 if endq < n else n
                continue
            if c == '/' and is_regex_position(out):
                endr = scan_regex(js_content, i)
                out.append(js_content[i:endr])
                i = endr
                continue
            out.append(c)
            i += 1
        js = ''.join(out)

        # Remove excessive whitespace (keep single spaces) - this won't affect preserved strings
        js = re.sub(r'\s+', ' ', js)

        # Remove spaces around operators and syntax
        js = re.sub(r'\s*([{}();,=+\-*/<>!&|])\s*', r'\1', js)

        # Add back space where needed for keywords (but NOT before method names)
        # Use negative lookahead to avoid breaking .forEach, .forOwn, etc.
        js = re.sub(r'}(else|catch|finally)\b', r'} \1', js)
        js = re.sub(r'\b(var|let|const|return|function|if|else|while|new)\b', r'\1 ', js)
        # Special handling for 'for' - only add space if NOT followed by 'E' (forEach)
        js = re.sub(r'\bfor\b(?!E)', r'for ', js)

        # Restore string literals in reverse order (important for nested replacements)
        for placeholder, original in reversed(strings):
            js = js.replace(placeholder, original)

        return js.strip()
    
    def minify_html(self, html_content):
        """Main HTML minification with safe CSS/JS handling"""
        self.stats['original_size'] = len(html_content)
        
        # Remove HTML comments (but preserve IE conditional comments)
        original_comments = html_content.count('<!--')
        html = re.sub(r'<!--(?!\[if).*?-->', '', html_content, flags=re.DOTALL)
        self.stats['comments_removed'] = original_comments - html.count('<!--')
        
        # Process <style> tags
        def minify_style_tag(match):
            css = match.group(1)
            minified = self.minify_css(css)
            return f'<style>{minified}</style>'
        
        html = re.sub(r'<style[^>]*>(.*?)</style>', minify_style_tag, html, flags=re.DOTALL|re.IGNORECASE)
        
        # Process <script> tags
        def minify_script_tag(match):
            js = match.group(1)
            minified = self.minify_javascript(js)
            return f'<script>{minified}</script>'
        
        html = re.sub(r'<script[^>]*>(.*?)</script>', minify_script_tag, html, flags=re.DOTALL|re.IGNORECASE)
        
        # Preserve <pre> and <textarea> content
        preserved_blocks = []
        def preserve_block(match):
            preserved_blocks.append(match.group(0))
            return f"__PRESERVED_{len(preserved_blocks)-1}__"
        
        html = re.sub(r'<pre[^>]*>.*?</pre>', preserve_block, html, flags=re.DOTALL|re.IGNORECASE)
        html = re.sub(r'<textarea[^>]*>.*?</textarea>', preserve_block, html, flags=re.DOTALL|re.IGNORECASE)
        
        # Remove whitespace between HTML tags
        original_whitespace = len(html)
        html = re.sub(r'>\s+<', '><', html)
        
        # Remove leading/trailing whitespace on lines
        html = '\n'.join(line.strip() for line in html.split('\n'))
        
        # Remove empty lines
        html = re.sub(r'\n\s*\n', '\n', html)
        
        # Collapse multiple spaces into single space (except in preserved blocks)
        html = re.sub(r' {2,}', ' ', html)
        
        self.stats['whitespace_reduced'] = original_whitespace - len(html)
        
        # Restore preserved blocks
        for i, block in enumerate(preserved_blocks):
            html = html.replace(f"__PRESERVED_{i}__", block)
        
        self.stats['minified_size'] = len(html)
        
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

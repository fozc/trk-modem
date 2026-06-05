#!/usr/bin/env python3
"""
STM32 Map File Analyzer
Modül bazlı Flash ve RAM kullanımını analiz eder.

Kullanım:
    python map_analyzer.py [map_file] [-f filter]
    
Örnekler:
    python map_analyzer.py                          # Release map dosyasını analiz et
    python map_analyzer.py ../Debug/smart-breaker.map
    python map_analyzer.py -f shell                 # Sadece 'shell' içeren modüller
"""

import re
import sys
import os
from collections import defaultdict

def parse_map_file(map_path):
    """Map dosyasını parse et ve modül bazlı bellek kullanımını hesapla."""
    
    modules = defaultdict(lambda: {'text': 0, 'rodata': 0, 'data': 0, 'bss': 0})
    current_section = None
    
    # Section pattern'leri
    section_patterns = {
        'text': re.compile(r'^\s*\*\(\.text'),
        'rodata': re.compile(r'^\s*\*\(\.rodata'),
        'data': re.compile(r'^\s*\*\(\.data'),
        'bss': re.compile(r'^\s*\*\(\.bss'),
    }
    
    # Size ve dosya pattern'i: "0x00000000  0x00000size  path/to/file.o"
    entry_pattern = re.compile(r'^\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+(.+\.o)\b')
    
    with open(map_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            # Section başlığını kontrol et
            for section, pattern in section_patterns.items():
                if pattern.search(line):
                    current_section = section
                    break
            
            # Entry'yi parse et
            if current_section:
                match = entry_pattern.match(line)
                if match:
                    size = int(match.group(1), 16)
                    file_path = match.group(2)
                    module_name = os.path.basename(file_path)
                    modules[module_name][current_section] += size
    
    return modules

def calculate_memory(modules):
    """Flash ve RAM kullanımını hesapla."""
    results = []
    
    for module, sections in modules.items():
        flash = sections['text'] + sections['rodata'] + sections['data']
        ram = sections['data'] + sections['bss']
        
        if flash > 0 or ram > 0:
            results.append({
                'module': module,
                'flash': flash,
                'ram': ram,
                'text': sections['text'],
                'rodata': sections['rodata'],
                'data': sections['data'],
                'bss': sections['bss']
            })
    
    # Flash'a göre sırala
    results.sort(key=lambda x: x['flash'], reverse=True)
    return results

def print_results(results, filter_str=None):
    """Sonuçları tablo formatında yazdır."""
    
    # Filtreleme
    if filter_str:
        results = [r for r in results if filter_str.lower() in r['module'].lower()]
    
    if not results:
        print("Sonuç bulunamadı.")
        return
    
    # Toplam
    total_flash = sum(r['flash'] for r in results)
    total_ram = sum(r['ram'] for r in results)
    
    # Başlık
    print(f"\n{'Module':<40} {'Flash':>12} {'RAM':>12}")
    print("-" * 66)
    
    # Sonuçlar
    for r in results:
        print(f"{r['module']:<40} {r['flash']:>12,} {r['ram']:>12,}")
    
    # Toplam
    print("-" * 66)
    print(f"{'TOPLAM':<40} {total_flash:>12,} {total_ram:>12,}")
    print(f"\nModül sayısı: {len(results)}")

def main():
    # Varsayılan map dosyası
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_map = os.path.join(script_dir, '..', 'Release', 'smart-breaker.map')
    
    map_file = default_map
    filter_str = None
    
    # Basit argüman işleme
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == '-f' and i + 1 < len(args):
            filter_str = args[i + 1]
            i += 2
        elif not args[i].startswith('-'):
            map_file = args[i]
            i += 1
        else:
            i += 1
    
    # Dosya kontrolü
    if not os.path.exists(map_file):
        print(f"Hata: Map dosyası bulunamadı: {map_file}")
        sys.exit(1)
    
    print(f"=== STM32 Memory Analyzer ===")
    print(f"Map: {map_file}")
    
    # Parse ve analiz
    modules = parse_map_file(map_file)
    results = calculate_memory(modules)
    print_results(results, filter_str)

if __name__ == '__main__':
    main()

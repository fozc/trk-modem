"""Agac-ici modul yukleme: ayni adi tasiyan run_tests.py dosyalari cakismasin.

test/integration/*/run_tests.py modulleri modul adi olarak ayni "run_tests"
adini tasiyir; tek surecte ikisini (modbus_device + web_device) yuklemek
icin benzersiz isimle spec_from_file_location kullanilir. device_fault
suresi ayni dosyalari sys.path+import ile yukler (tek dosya icin yeterli);
burada iki tanesi gerekli oldugu icin adlandirilmis yukleme tercih edildi.
"""

import importlib.util
import sys
from pathlib import Path


def load_module(unique_name, file_path, extra_sys_path=(), reuse=True):
    """Dosyayi benzersiz adla yukle; ek sys.path dizileri once eklenir.

    extra_sys_path, yuklenen modulun kendi import'lari icin gereklidir
    (ornek gsm_disconnect_test -> http_port_drop_test ayni dizinde).
    reuse=True ise ayni adla daha once yuklenmis modul tekrar
    calistirilmadan dondurulur (orn. web_probe ve run_monitor ayni
    oracle'i paylasir).
    """
    for entry in extra_sys_path:
        text = str(entry)
        if text not in sys.path:
            sys.path.insert(0, text)
    if reuse and unique_name in sys.modules:
        return sys.modules[unique_name]
    path = Path(file_path)
    spec = importlib.util.spec_from_file_location(unique_name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[unique_name] = module
    spec.loader.exec_module(module)
    return module

#ifndef _VERSION_H_
#define _VERSION_H_


#define DEVICE_TYPE             (100)
#define DEVICE_MODEL            (1)
/* bin2efw.py dosya-tipi eslemesi: 1=Bootloader(0x40), 2=Application(0x60).
 * Bu image uygulama oldugu icin 2 olmali; aksi halde paketlenen .efw
 * "Bootloader" tipli uretilir (bootloader bugun kontrol etmiyor olsa da
 * yanlis, ve file_type dogrulamasi eklendiginde reddedilir). */
#define APP_TYPE                (2)
#define VERSION_MAJOR           (1)
#define VERSION_MINOR           (0)
#define VERSION_PATCH           (0)
#define VERSION_EXTRA           (0)
#define APP_VERSION             ((VERSION_EXTRA << 24) | (VERSION_PATCH << 16) | \
                                (VERSION_MINOR << 8) | (VERSION_MAJOR))

#define PRODUCT_TYPE            "Troika-Smart-Breaker"

/* Derleme damgasi: derleyici makrolari, kullanan dosya derlendiginde
 * guncellenir. Surum cikislari temiz (rebuild) derleme ile alinmalidir.
 * Kurulan imajin kimligi (hash, build takvimi, kurulum tarihi) boot
 * superblock'tan okunur (bkz. boot_get_installed_fw_info). */
#define __COMPILE_DATE__        __DATE__     /* ornek: "Sep  3 2026" */
#define __COMPILE_TIME__        __TIME__     /* ornek: "21:45:12"    */

#endif /* _VERSION_H_ */


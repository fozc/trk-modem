#ifndef _VERSION_H_
#define _VERSION_H_


#define DEVICE_TYPE             (100)
#define DEVICE_MODEL            (1)
#define APP_TYPE                (1)
#define VERSION_MAJOR           (1)
#define VERSION_MINOR           (0)
#define VERSION_PATCH           (0)
#define VERSION_EXTRA           (0)
#define APP_VERSION             ((VERSION_EXTRA << 24) | (VERSION_PATCH << 16) | \
                                (VERSION_MINOR << 8) | (VERSION_MAJOR))

#define PRODUCT_TYPE            "Troika-Smart-Breaker"

/* Derleme damgasi: derleyici makrolari, kullanan dosya derlendiginde
 * guncellenir. Surum cikislari temiz (rebuild) derleme ile alinmalidir. */
#define __COMPILE_DATE__        __DATE__     /* ornek: "Sep  3 2026" */
#define __COMPILE_TIME__        __TIME__     /* ornek: "21:45:12"    */

/* Kisaltilmis git hash'i pre-build adimi (tools/gen_version.py)
 * tarafindan version_gen.h icine uretilir. */
#include "version_gen.h"

#endif /* _VERSION_H_ */


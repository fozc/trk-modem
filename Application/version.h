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

#define __COMPILE_TIME__	("2025-09-16")
#define __COMPILE_DATE__	("22:05:23")
#define GIT_COMMIT_HASH		("fc7feae")

#endif /* _VERSION_H_ */


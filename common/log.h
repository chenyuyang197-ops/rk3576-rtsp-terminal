#ifndef RK3576_LOG_H
#define RK3576_LOG_H

#include <stdio.h>

#define LOGI(fmt, ...) fprintf(stdout, "[I] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)

#endif

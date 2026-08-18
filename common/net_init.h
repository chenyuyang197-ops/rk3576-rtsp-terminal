#ifndef RK3576_NET_INIT_H
#define RK3576_NET_INIT_H

#include "ini_config.h"

/* 启动时检查 eth0/end0 IPv4，必要时配置静态地址；无网线时进入 simulate 模式继续跑媒体链路 */
int net_init_from_ini(const IniConfig *cfg);

#endif

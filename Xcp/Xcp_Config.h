#ifndef XCP_CONFIG_H
#define XCP_CONFIG_H

/* 配置单一事实源: ECUC schema (Tools/xcpgen) -> Gen/Xcp_Cfg.h
 * 头里不再手写宏; 生成物经 -IGen 路径引入。
 * 集成钩子 (CHECK_* / DTO_*_CRITICAL) 可经 -D/-include 覆盖生成的 #ifndef 默认。
 */
#include <Xcp_Cfg.h>

#endif /* XCP_CONFIG_H */

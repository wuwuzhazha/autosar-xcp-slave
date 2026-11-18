/**
 * @file    matrix_main.c
 * @brief   编译矩阵链接入口: 只验证开关组合可编译、可链接(无未定义符号), 不做行为测试
 */
#include "Xcp.h"

int main(void)
{
    Xcp_Init(NULL_PTR);
    Xcp_MainFunction();
    return 0;
}

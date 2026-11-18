@echo off
setlocal
cd /d "%~dp0\.."

set GEN=python Tools\xcpgen\xcpgen.py
set CORE=Xcp\Xcp.c Xcp\Xcp_Cmd.c Xcp\Xcp_Std.c Xcp\Xcp_DaqCfg.c Xcp\Xcp_DaqRun.c Xcp\Xcp_DaqStim.c Xcp\Xcp_SeedKey.c Xcp\Xcp_CalPage.c Xcp\Xcp_FlashProg.c Xcp\Xcp_Resume.c Tests\test_stubs.c Tests\test_main.c
set FLAGS=-std=c99 -g -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration -ITests\stubs -ITests -IXcp -include Tests\test_config.h

echo ==================================================================
echo [R] Architecture ratchet (counts + Gen drift)
echo ==================================================================
python Tests\ratchet.py || goto :fail

echo.
echo ==================================================================
echo [0/9] Generate: 10 个变体配置 (Gen\*, 来源 Config\profiles\*.toml)
echo ==================================================================
%GEN% --profile can     --out Gen\can     || goto :fail
%GEN% --profile ilv     --out Gen\ilv     || goto :fail
%GEN% --config Config\xcp_base.toml --check || (echo CONFIG CHECK FAILED & goto :fail)
%GEN% --profile fr      --out Gen\fr      || goto :fail
%GEN% --profile eth     --out Gen\eth     || goto :fail
%GEN% --profile rbyte   --out Gen\rbyte   || goto :fail
%GEN% --profile rword   --out Gen\rword   || goto :fail
%GEN% --profile rwal    --out Gen\rwal    || goto :fail
%GEN% --profile cdd     --out Gen\cdd     || goto :fail
%GEN% --profile static  --out Gen\static  || goto :fail
%GEN% --profile predef  --out Gen\predef  || goto :fail

echo.
echo ==================================================================
echo [1/9] Build+Run: CAN 单命令模式
echo ==================================================================
gcc %FLAGS% -IGen\can -o Tests\xcp_test.exe %CORE% Gen\can\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [2/9] Build+Run: CAN Interleaved 交织模式
echo ==================================================================
gcc %FLAGS% -IGen\ilv -o Tests\xcp_test_ilv.exe %CORE% Gen\ilv\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test_ilv.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [3/9] Build+Run: FlexRay 传输层
echo ==================================================================
gcc %FLAGS% -IGen\fr -o Tests\xcp_test_fr.exe %CORE% Gen\fr\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test_fr.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [4/9] Build+Run: Ethernet 传输层
echo ==================================================================
gcc %FLAGS% -IGen\eth -o Tests\xcp_test_eth.exe %CORE% Gen\eth\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test_eth.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [5/9] Build+Run: CAN + RELATIVE_BYTE 寻址
echo ==================================================================
gcc %FLAGS% -IGen\rbyte -o Tests\xcp_test_rbyte.exe %CORE% Gen\rbyte\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test_rbyte.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [6/9] Build: RELATIVE_WORD / WORD_ALIGNED (编译验证)
echo ==================================================================
gcc %FLAGS% -IGen\rword -o Tests\xcp_test_rword.exe %CORE% Gen\rword\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
gcc %FLAGS% -IGen\rwal  -o Tests\xcp_test_rwal.exe  %CORE% Gen\rwal\Xcp_Cfg.c  Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
echo RELATIVE_WORD + WORD_ALIGNED compile OK

echo.
echo ==================================================================
echo [7/9] Build+Run: CDD 传输层
echo ==================================================================
gcc %FLAGS% -IGen\cdd -o Tests\xcp_test_cdd.exe %CORE% Gen\cdd\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test_cdd.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [8/9] Build+Run: DAQ_STATIC static DAQ
echo ==================================================================
gcc %FLAGS% -IGen\static -o Tests\xcp_test_static.exe Xcp\Xcp.c Xcp\Xcp_Cmd.c Xcp\Xcp_Std.c Xcp\Xcp_DaqCfg.c Xcp\Xcp_DaqRun.c Xcp\Xcp_DaqStim.c Xcp\Xcp_SeedKey.c Xcp\Xcp_CalPage.c Xcp\Xcp_FlashProg.c Xcp\Xcp_Resume.c Gen\static\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c Tests\test_stubs.c Tests\static_main.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test_static.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [8b/9] Build+Run: 预定义 DAQ 列表 (XCP_MIN_DAQ=2)
echo ==================================================================
gcc %FLAGS% -IGen\predef -o Tests\xcp_test_predef.exe Xcp\Xcp.c Xcp\Xcp_Cmd.c Xcp\Xcp_Std.c Xcp\Xcp_DaqCfg.c Xcp\Xcp_DaqRun.c Xcp\Xcp_DaqStim.c Xcp\Xcp_SeedKey.c Xcp\Xcp_CalPage.c Xcp\Xcp_FlashProg.c Xcp\Xcp_Resume.c Gen\predef\Xcp_Cfg.c Xcp\XcpOnCan.c Xcp\XcpOnFr.c Xcp\XcpOnEth.c Xcp\XcpOnCdd.c Tests\test_stubs.c Tests\predef_main.c || (echo BUILD FAILED & goto :fail)
Tests\xcp_test_predef.exe
if errorlevel 1 goto :fail

echo.
echo ==================================================================
echo [9/9] Tests: 生成器 (golden+负例) + 编译矩阵 (99 变体 x 4链路, -Werror)
echo ==================================================================
python Tests\test_xcpgen.py || goto :fail
python Tests\build_matrix.py || goto :fail

echo.
echo ================================================================
echo      ALL TESTS PASSED (CAN + ILV + FR + ETH + RELBYTE + CDD + STATIC + MATRIX)
echo ================================================================
goto :eof

:fail
echo.
echo ================================================================
echo      TEST RUN FAILED
echo ================================================================
exit /b 1

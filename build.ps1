# build.ps1 —— calendar_tag（日历 + 走时 + BLE 对时）
#
# 和之前那版最大的区别：**不再从 acl_peripheral_demo 的 subdir.mk 抓文件列表**。
# SDK 只贡献真正通用的部分（启动、驱动、公共工具、栈库），
# demo 目录一个文件都不参与编译 —— 入口、BLE 初始化、ATT 表全在 src/ 里自己写。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File .\build.ps1
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Sdk "D:\tc_ble_sdk\tc_ble_sdk"

param(
    [switch]$Clean,
    [string]$Sdk = ""        # SDK 根目录，默认 ..\tc_ble_sdk\tc_ble_sdk
)

$ErrorActionPreference = "Stop"

$TC32   = "C:\TelinkIoTStudio\opt\tc32\bin"
$root   = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Sdk) { $Sdk = Join-Path $root "..\tc_ble_sdk\tc_ble_sdk" }
$sdk    = (Resolve-Path $Sdk).Path
$objdir = Join-Path $root "out\obj"
$elf    = Join-Path $root "out\calendar_tag.elf"
$bin    = Join-Path $root "out\calendar_tag.bin"
$cfg    = Join-Path $root "src\app_cfg.h"

if (-not (Test-Path "$TC32\tc32-elf-gcc.exe")) { throw "找不到 TC32 工具链: $TC32" }
if (-not (Test-Path $sdk))                     { throw "找不到 SDK: $sdk" }
$env:PATH = "$TC32;" + $env:PATH

if ($Clean -and (Test-Path (Join-Path $root "out"))) {
    Remove-Item (Join-Path $root "out") -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $objdir | Out-Null

# ---------------------------------------------------------------- SDK 源文件
# 只保留通用底层。凡是 vendor/acl_peripheral_demo 的东西都在这份清单里没有，
# 所以改了也影响不到本工程（拷到别处一样编）。
$sdk_sources = @(
    # 启动与数学
    "boot/B85/cstartup_825x.S",
    "div_mod.S",

    # 公共工具
    "common/sdk_version.c",
    "common/string.c",
    "common/tl_queue.c",
    "common/utility.c",

    # printf（SDK 内部有引用，保留；宏全关时不打印）
    "application/print/putchar.c",
    "application/print/u_printf.c",

    # B85 驱动（全部通用外设，未用到的由 --gc-sections 丢掉）
    "drivers/B85/adc.c",
    "drivers/B85/aes.c",
    "drivers/B85/analog.c",
    "drivers/B85/audio.c",
    "drivers/B85/bsp.c",
    "drivers/B85/clock.c",
    "drivers/B85/flash.c",
    "drivers/B85/gpio.c",
    "drivers/B85/i2c.c",
    "drivers/B85/lpc.c",
    "drivers/B85/qdec.c",
    "drivers/B85/s7816.c",
    "drivers/B85/spi.c",
    "drivers/B85/timer.c",
    "drivers/B85/uart.c",
    "drivers/B85/usbhw.c",
    "drivers/B85/watchdog.c",

    # flash 芯片适配（按 MID 自动选，缺一个都可能读不出容量）
    "drivers/B85/flash/flash_common.c",
    "drivers/B85/flash/flash_mid0113325e.c",
    "drivers/B85/flash/flash_mid0114325e.c",
    "drivers/B85/flash/flash_mid011460c8.c",
    "drivers/B85/flash/flash_mid1060c8.c",
    "drivers/B85/flash/flash_mid13325e.c",
    "drivers/B85/flash/flash_mid134051.c",
    "drivers/B85/flash/flash_mid136085.c",
    "drivers/B85/flash/flash_mid1360c8.c",
    "drivers/B85/flash/flash_mid1360eb.c",
    "drivers/B85/flash/flash_mid14325e.c",
    "drivers/B85/flash/flash_mid1460c8.c",

    # 驱动扩展：射频功率、校准、MISC
    "drivers/B85/driver_ext/ext_calibration.c",
    "drivers/B85/driver_ext/ext_hci_uart.c",
    "drivers/B85/driver_ext/ext_misc.c",
    "drivers/B85/driver_ext/rf_pa.c",

    # SDK 公共应用层：MAC/校准参数/flash 扇区地址/固件签名检查
    "vendor/common/app_common.c",
    "vendor/common/ble_flash.c",
    "vendor/common/blt_fw_sign.c",
    "vendor/common/flash_fw_check.c",
    "vendor/common/flash_prot.c",
    "vendor/common/tlkapi_debug.c",
    # 只为了 boot.link 里那句 __PM_DEEPSLEEP_RETENTION_ENABLE 的定义（一个 asm .equ）
    "vendor/common/user_config.c"
)

Write-Host "SDK 源文件: $($sdk_sources.Count) 个（不含 demo）"

# 构建时间 -> Unix 时间戳（UTC），作为没有存档时的兜底起点
$epoch = [int64]([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
Write-Host ("兜底走时起点 : {0}  ({1} UTC)" -f $epoch, ([DateTimeOffset]::FromUnixTimeMilliseconds($epoch * 1000).ToString('yyyy-MM-dd HH:mm:ss'))) -ForegroundColor Cyan

# ---------------------------------------------------------------- 编译参数
$CFLAGS = @(
    "-ffunction-sections", "-fdata-sections",
    "-I$($root)\src",
    "-I$sdk", "-I$($sdk)\build\B85", "-I$($sdk)\vendor\common",
    "-I$($sdk)\drivers\B85", "-I$($sdk)\common",
    "-DCHIP_TYPE=CHIP_TYPE_825x",
    "-DBUILD_EPOCH=$epoch",
    # 关键：强制预包含本工程自己的配置头，替代 demo 的 app_config.h
    "-include", $cfg,
    "-Wall", "-O2", "-fpack-struct", "-fshort-enums",
    "-finline-small-functions", "-std=gnu99", "-fshort-wchar", "-fms-extensions"
)

$objs = @()

# ---------------------------------------------------------------- 编 SDK
$i = 0
foreach ($s in $sdk_sources) {
    $i++
    $abs = Join-Path $sdk $s
    if (-not (Test-Path $abs)) { throw "SDK 源文件不存在: $s" }
    $obj = Join-Path $objdir ("sdk_" + ($s -replace '[/\\]', '_' -replace '\.(c|S)$', '.o'))
    Write-Progress -Activity "编译 SDK" -Status $s -PercentComplete (100 * $i / $sdk_sources.Count)

    if ($s.ToLower().EndsWith('.s')) {
        & "$TC32\tc32-elf-gcc.exe" "-DMCU_STARTUP_8258" -c $abs -o $obj
    } else {
        & "$TC32\tc32-elf-gcc.exe" @CFLAGS -c $abs -o $obj
    }
    if ($LASTEXITCODE -ne 0) { throw "编译失败: $s" }
    $objs += $obj
}
Write-Progress -Activity "编译 SDK" -Completed

# ---------------------------------------------------------------- 编本工程
foreach ($f in (Get-ChildItem (Join-Path $root "src") -Filter *.c | Sort-Object Name)) {
    Write-Host "CC  src/$($f.Name)"
    $obj = Join-Path $objdir ("app_" + $f.BaseName + ".o")
    & "$TC32\tc32-elf-gcc.exe" @CFLAGS -c $f.FullName -o $obj
    if ($LASTEXITCODE -ne 0) { throw "编译失败: src/$($f.Name)" }
    $objs += $obj
}

# ---------------------------------------------------------------- 链接
Write-Host "LD  calendar_tag.elf"
& "$TC32\tc32-elf-ld.exe" --gc-sections `
    "-L$($sdk)\proj_lib" `
    "-T$($sdk)\build\B85\boot.link" `
    -o $elf @objs -llt_8258_c0p4
if ($LASTEXITCODE -ne 0) { throw "链接失败" }

Write-Host "BIN calendar_tag.bin"
& "$TC32\tc32-elf-objcopy.exe" -O binary $elf $bin
if ($LASTEXITCODE -ne 0) { throw "objcopy 失败" }

& (Join-Path $sdk "tl_check_fw2.exe") $bin
if ($LASTEXITCODE -ne 0) { throw "tl_check_fw2 失败" }

# ---------------------------------------------------------------- 自检
$b = [IO.File]::ReadAllBytes($bin)
$size = $b.Length
$h8  = [BitConverter]::ToUInt32($b, 8)
$h18 = [BitConverter]::ToUInt32($b, 0x18)

Write-Host ""
Write-Host ("产物: {0}" -f $bin) -ForegroundColor Green
Write-Host ("大小: {0} 字节 ({1:N1} KB)" -f $size, ($size / 1KB))
Write-Host ("0x08 固件标识 : 0x{0:X8}   (应为 0x544C4E4B = 'TLNK')" -f $h8)
Write-Host ("0x18 固件长度 : {0}   (应与文件大小 {1} 相等)" -f $h18, $size)

$ok = ($h8 -eq 0x544C4E4B) -and ($h18 -eq $size) -and ([BitConverter]::ToUInt32($b,0) -eq 0x8056)
if (-not $ok) { Write-Host "!! 头部自检未通过，烧进去也不会跑" -ForegroundColor Red }
else          { Write-Host "头部自检通过，可以烧录" -ForegroundColor Green }

Write-Host ""
& "$TC32\tc32-elf-size.exe" -A $elf

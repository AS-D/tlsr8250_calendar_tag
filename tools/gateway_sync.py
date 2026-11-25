#!/usr/bin/env python3
"""
calendar_tag 网关对时脚本（跑在树莓派 / 旧笔记本 / 任何常开的 Linux、Windows 机器上）

为什么需要它：
    固件如果只在"每天固定窗口"里开广播（其余时间深睡），手机 / 网关必须在
    那个窗口内主动扫描并连接，才能写进时间。这个脚本就是那个"网关"。

依赖：
    pip install bleak

用法：
    # 立刻扫 3 分钟并连上对时（设备此刻必须在广播）
    python gateway_sync.py

    # 每天 03:00 开始扫 5 分钟（配合 cron / 任务计划）
    python gateway_sync.py --at 03:00 --window 300

    # 一直循环，每 60 秒试一次（调试用）
    python gateway_sync.py --loop 60

协议（见 src/ble_app.c）：
    广播名  calendar_tag
    写特征  00010203-0405-0607-0809-0a0b0c0d2b11 （write without response，免配对）
    数据    0x01 + Unix 秒（4 字节小端）

退出码：0 = 对时成功；1 = 超时没找到 / 连接失败。
"""

import argparse
import asyncio
import datetime
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "calendar_tag"
WRITE_UUID = "00010203-0405-0607-0809-0a0b0c0d2b11"


def match(d, adv_data):
    """只认广播名，避免连到别的设备。"""
    name = adv_data.local_name or ""
    return name == DEVICE_NAME


async def find_device(timeout):
    print(f"扫描 {timeout}s，等待 {DEVICE_NAME} 广播 ...")
    return await BleakScanner.find_device_by_filter(match, timeout=timeout)


async def sync_once(timeout):
    dev = await find_device(timeout)
    if dev is None:
        print("没扫到设备（不在窗口内，或者离得太远）")
        return False

    print(f"发现 {dev.name or DEVICE_NAME} @ {dev.address}，连接中 ...")
    async with BleakClient(dev, timeout=10) as client:
        if not client.is_connected:
            print("连接失败")
            return False

        epoch = int(time.time())
        payload = b"\x01" + struct.pack("<I", epoch)
        await client.write_gatt_char(WRITE_UUID, payload, response=False)
        print(f"已写入 {epoch} = {datetime.datetime.fromtimestamp(epoch)}")
    return True


async def run(args):
    if args.at:
        # 等到当天的 HH:MM 再开始（跨天则顺延到明天）
        target = datetime.datetime.combine(
            datetime.date.today(),
            datetime.time(args.at // 60, args.at % 60),
        )
        now = datetime.datetime.now()
        if target < now:
            target += datetime.timedelta(days=1)
        wait = (target - now).total_seconds()
        print(f"等到 {target}（还有 {wait/60:.1f} 分钟）")
        await asyncio.sleep(wait)

    if args.loop:
        while True:
            ok = await sync_once(args.scan_timeout)
            if ok:
                return 0
            await asyncio.sleep(args.loop)
    else:
        ok = await sync_once(args.scan_timeout)
        return 0 if ok else 1


def main():
    p = argparse.ArgumentParser(description="calendar_tag 网关对时")
    p.add_argument("--at", type=str, default="",
                   help="每天几点开始扫，格式 HH:MM，例如 03:00")
    p.add_argument("--window", type=int, default=300,
                   help="窗口内扫描总时长（秒），默认 300")
    p.add_argument("--loop", type=int, default=0,
                   help="找到之前每隔 N 秒重试一次（0=只试一次）")
    p.add_argument("--scan-timeout", type=int, default=60,
                   help="单次扫描超时（秒）")
    args = p.parse_args()

    if args.at:
        hh, mm = args.at.split(":")
        args.at = int(hh) * 60 + int(mm)

    try:
        rc = asyncio.run(run(args))
    except KeyboardInterrupt:
        rc = 130
    sys.exit(rc)


if __name__ == "__main__":
    main()

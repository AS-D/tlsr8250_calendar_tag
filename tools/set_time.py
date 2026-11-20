#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
set_time.py —— PC 上通过蓝牙给价签对时（不用手机）

协议（写 Telink SPP 的 Client2Server 特征值，5 字节）：
    [0]    = 0x01                 命令：设置时间
    [1..4] = Unix 时间戳（小端）   UTC 秒

依赖：
    pip install bleak

用法：
    python tools\\set_time.py --scan                 # 先看附近能不能扫到 calendar_tag
    python tools\\set_time.py                        # 用当前时间和默认设备名对时
    python tools\\set_time.py --name calendar_tag    # 指定广播名
    python tools\\set_time.py --address XX:XX:...    # 直接按 MAC 连（扫不到名字时用）
    python tools\\set_time.py --date "2026-11-20 09:10"   # 按指定本地时间对时
    python tools\\set_time.py --dry-run              # 只算字节，不碰蓝牙

说明：
    · 价签收到命令后会立刻重画整月并刷屏 15~20 秒，这期间连接可能被刷屏阻塞搞断，
      属于正常现象——已经写进去了，不用重连重写。
    · 广播名是 `calendar_tag`（本工程在 main_app.c 的 ble_adv_start() 里自己设的）。
"""
import argparse
import asyncio
import datetime
import sys
import time

CTRL_UUID = "00010203-0405-0607-0809-0a0b0c0d2b11"   # SPP Client2Server（write）
DEF_NAME = "calendar_tag"
SCAN_SECS = 5.0


def make_payload(epoch):
    return bytes([0x01]) + int(epoch).to_bytes(4, "little")


def local_to_epoch(s, tz):
    dt = datetime.datetime.strptime(s, "%Y-%m-%d %H:%M")
    return int((dt - datetime.datetime(1970, 1, 1)).total_seconds()) - tz * 3600


def describe(epoch, tz):
    utc = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=epoch)
    loc = utc + datetime.timedelta(seconds=tz * 3600)
    print("Unix 时间戳 : %d  (0x%08X)" % (epoch, epoch))
    print("UTC         : %s" % utc.strftime("%Y-%m-%d %H:%M:%S"))
    print("当地时间    : %s (UTC%+d)" % (loc.strftime("%Y-%m-%d %H:%M:%S"), tz))
    print("要写入      : %s" % " ".join("%02X" % b for b in make_payload(epoch)))


async def do_scan(secs):
    try:
        from bleak import BleakScanner
    except ImportError:
        print("缺 bleak：pip install bleak")
        return

    print("扫描 %.1f 秒 ..." % secs)
    devs = await BleakScanner.discover(timeout=secs)
    if not devs:
        print("没扫到设备：确认价签在广播、电脑蓝牙已开、离得近一点。")
        return
    for d in devs:
        print("  %-28s %s  rssi=%s" % (d.name or "(无名)", d.address, d.rssi))


async def do_write(name, address, payload, timeout):
    from bleak import BleakClient, BleakScanner

    target = address
    if not target:
        print("找广播名 %r ..." % name)
        dev = await BleakScanner.find_device_by_name(name, timeout=timeout)
        if dev is None:
            print("没找到 %r。先跑 --scan 看看实际广播名，或用 --address 指定。" % name)
            return 1
        target = dev.address
        print("找到 %s (%s)" % (dev.name or name, target))

    print("连接 %s ..." % target)
    async with BleakClient(target, timeout=timeout) as cli:
        if not await cli.is_connected():
            print("连接失败。")
            return 1
        print("已连接，写入 %s ..." % " ".join("%02X" % b for b in payload))
        # 这个特征是 write_without_rsp，response=False
        try:
            await cli.write_gatt_char(CTRL_UUID, payload, response=False)
        except Exception as e:                       # 部分栈对 write_without_rsp 处理不同
            print("  write_without_rsp 失败(%s)，改试 with response ..." % e)
            await cli.write_gatt_char(CTRL_UUID, payload, response=True)
        print("写入完成。价签接下来会重画并刷屏 15~20 秒，连接断开是正常的。")
        await asyncio.sleep(1.0)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true", help="只扫描附近的 BLE 设备")
    ap.add_argument("--name", default=DEF_NAME, help="广播名，默认 " + DEF_NAME)
    ap.add_argument("--address", default=None, help="直接按 MAC/UUID 连，跳过按名字查找")
    ap.add_argument("--date", default=None, help='本地时间，如 "2026-11-20 09:10"')
    ap.add_argument("--tz", type=int, default=8, help="本地时区小时，默认 +8")
    ap.add_argument("--timeout", type=float, default=10.0, help="扫描/连接超时秒数")
    ap.add_argument("--dry-run", action="store_true", help="只算字节，不连蓝牙")
    a = ap.parse_args()

    epoch = local_to_epoch(a.date, a.tz) if a.date else int(time.time())

    if a.scan:
        asyncio.run(do_scan(SCAN_SECS))
        return

    describe(epoch, a.tz)
    if a.dry_run:
        print("")
        print("(--dry-run：把上面这 5 字节按 Byte Array 填进 nRF Connect 也能对时)")
        return

    try:
        import bleak  # noqa: F401
    except ImportError:
        print("")
        print("缺 bleak：pip install bleak")
        sys.exit(2)

    rc = asyncio.run(do_write(a.name, a.address, make_payload(epoch), a.timeout))
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()

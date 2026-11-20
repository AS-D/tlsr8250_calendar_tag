#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
time_bytes.py —— 生成"对时"要写的 5 个字节

协议（往 Telink SPP 的 Client2Server 特征值写）：
    [0]    = 0x01                 命令：设置时间
    [1..4] = Unix 时间戳（小端）   UTC 秒

用法：
  python tools/time_bytes.py                     # 当前时间
  python tools/time_bytes.py --now
  python tools/time_bytes.py --date "2026-11-19 12:00"    # 按本地时间(+8)算

输出示例：
    01 5A 3F 2B 6B
把它按顺序填进 nRF Connect 的字节数组里点 Write 即可。
"""
import argparse
import datetime
import time

TZ = 28800


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--date", help='本地时间，如 "2026-11-19 12:00"')
    ap.add_argument("--tz", type=int, default=8, help="本地时区小时，默认 +8")
    a = ap.parse_args()

    if a.date:
        dt = datetime.datetime.strptime(a.date, "%Y-%m-%d %H:%M")
        epoch = int((dt - datetime.datetime(1970, 1, 1)).total_seconds()) - a.tz * 3600
    else:
        epoch = int(time.time())

    payload = bytes([0x01]) + epoch.to_bytes(4, "little")
    hexs = " ".join("%02X" % b for b in payload)
    utc = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=epoch)
    local = utc + datetime.timedelta(seconds=a.tz * 3600)

    print("Unix 时间戳 : %d  (0x%08X)" % (epoch, epoch))
    print("UTC         : %s" % utc.strftime("%Y-%m-%d %H:%M:%S"))
    print("当地时间    : %s (UTC%+d)" % (local.strftime("%Y-%m-%d %H:%M:%S"), a.tz))
    print("")
    print("要写入的 5 字节：")
    print("    %s" % hexs)
    print("")
    print("nRF Connect: 连上后找到特征值 00010203-0405-0607-0809-0a0b0c0d2b11")
    print("             点向上箭头(Write)，类型选 Byte Array，填入上面这 5 个字节。")


if __name__ == "__main__":
    main()

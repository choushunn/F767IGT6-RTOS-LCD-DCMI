#!/usr/bin/env python3
"""Capture STM32 display/camera registers and framebuffers through pyOCD."""

from __future__ import annotations

import argparse
from pathlib import Path

from pyocd.core.helpers import ConnectHelper


DEFAULT_REGISTERS = (
    ("DCMI_CR", 0x50050000, 0x10),
    ("DMA2_LISR", 0x40026400, 0x04),
    ("DMA2_S1", 0x40026428, 0x18),
    ("FMC_SDCR1_SDTR1", 0xA0000144, 0x08),
    ("LTDC_GLOBAL", 0x40016800, 0x40),
    ("LTDC_LAYER1", 0x40016884, 0x34),
    ("LTDC_LAYER2", 0x40016904, 0x34),
)

DEFAULT_DUMPS = (
    ("ui", 0xC0000000, 424 * 600 * 2),
    ("cam0", 0xC0140000, 600 * 600 * 2),
    ("cam1", 0xC0200000, 600 * 600 * 2),
    ("cam2", 0xC02C0000, 600 * 600 * 2),
)


def parse_range(value: str) -> tuple[str, int, int]:
    try:
        name, address, size = value.split(":")
        return name, int(address, 0), int(size, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "expected NAME:ADDRESS:SIZE, for example ui:0xC0000000:508800"
        ) from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--uid", required=True, help="pyOCD probe unique ID")
    parser.add_argument("--target", default="stm32f767zi")
    parser.add_argument("--frequency", type=int, default=1_000_000)
    parser.add_argument("--out", type=Path, default=Path(".tmp_diag/swd-capture"))
    parser.add_argument("--register", action="append", type=parse_range, dest="registers")
    parser.add_argument("--dump", action="append", type=parse_range, dest="dumps")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    options = {"frequency": args.frequency}

    session = ConnectHelper.session_with_chosen_probe(
        unique_id=args.uid,
        target_override=args.target,
        connect_mode="attach",
        options=options,
        blocking=False,
    )
    if session is None:
        print("probe not found")
        return 2

    with session:
        target = session.target
        registers = args.registers or list(DEFAULT_REGISTERS)
        dumps = args.dumps or list(DEFAULT_DUMPS)

        register_path = args.out / "registers.txt"
        with register_path.open("w", encoding="utf-8") as stream:
            for name, address, size in registers:
                data = target.read_memory_block8(address, size)
                words = [
                    int.from_bytes(data[index:index + 4], "little")
                    for index in range(0, min(size, len(data)) - 3, 4)
                ]
                line = f"{name} @ 0x{address:08X}: " + " ".join(
                    f"{word:08X}" for word in words
                )
                print(line)
                stream.write(line + "\n")

        for name, address, size in dumps:
            data = bytes(target.read_memory_block8(address, size))
            path = args.out / f"{name}.bin"
            path.write_bytes(data)
            print(f"saved {name}: {path} ({len(data)} bytes)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

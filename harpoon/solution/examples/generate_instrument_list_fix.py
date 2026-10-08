#!/usr/bin/env python3
"""Generate synthetic Kraken Instrument List FIX messages."""

from __future__ import annotations

import argparse
from decimal import Decimal
from pathlib import Path
from uuid import uuid4


SOH = "\x01"


SYMBOLS = (
    "XBT/USD",
    "ETH/USD",
    "SOL/USD",
    "XRP/USD",
    "ADA/USD",
    "DOT/USD",
    "LINK/USD",
    "LTC/USD",
)


def checksum(message: str) -> str:
    return f"{sum(message.encode('ascii')) % 256:03d}"


def fix_field(tag: int | str, value: object) -> str:
    return f"{tag}={value}{SOH}"


def decimal_value(base: str, step: str, index: int) -> str:
    return format(Decimal(base) + (Decimal(step) * index), "f")


def build_message(index: int, total_messages: int, fragmentation: bool) -> str:
    symbol = SYMBOLS[index % len(SYMBOLS)]
    qty_precision = 8 if symbol.startswith(("XBT", "ETH")) else 6
    price_precision = 1 if symbol.startswith("XBT") else 2

    body_fields = [
        fix_field(35, "y"),
        fix_field(320, f"REQ-{uuid4()}"),
        fix_field(322, f"RESP-{uuid4()}"),
        fix_field(560, 0),
        fix_field(146, 1),
        fix_field(55, symbol),
        fix_field(562, decimal_value("0.00000001", "0.00000001", index % 5)),
        fix_field(5010, qty_precision),
        fix_field(5011, decimal_value("0.0001", "0.0001", index % 7)),
        fix_field(5012, decimal_value("1000", "100", index % 9)),
        fix_field(5013, decimal_value("0.5", "0.25", index % 6)),
        fix_field(2349, price_precision),
        fix_field(5022, "0.1" if price_precision == 1 else "0.01"),
        fix_field(5032, 1),
    ]

    if fragmentation:
        body_fields.extend(
            [
                fix_field(393, total_messages),
                fix_field(893, "Y" if index == total_messages - 1 else "N"),
            ]
        )

    body = "".join(body_fields)
    head = fix_field(8, "FIX.4.4") + fix_field(9, len(body.encode("ascii")))
    message_without_checksum = head + body
    return message_without_checksum + fix_field(10, checksum(message_without_checksum))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate synthetic Kraken Instrument List FIX messages."
    )
    parser.add_argument("n", type=int, help="number of FIX messages to generate")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("instrument_list.fix"),
        help="output file path; default: instrument_list.fix",
    )
    parser.add_argument(
        "--printable",
        action="store_true",
        help="write '|' separators instead of SOH bytes; this is now the default",
    )
    parser.add_argument(
        "--soh",
        action="store_true",
        help="write FIX SOH bytes instead of printable '|' separators",
    )
    parser.add_argument(
        "--fragmentation-fields",
        action="store_true",
        help="include TotNoRelatedSym(393) and LastFragment(893)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.n < 0:
        raise SystemExit("n must be greater than or equal to 0")

    stream = "".join(
        build_message(i, args.n, args.fragmentation_fields) for i in range(args.n)
    )
    if not args.soh:
        stream = stream.replace(SOH, "|")

    args.output.write_text(stream, encoding="ascii")
    print(f"Wrote {args.n} FIX messages to {args.output}")


if __name__ == "__main__":
    main()

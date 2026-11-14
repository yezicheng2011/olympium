#!/usr/bin/env python3
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0
"""Check the support page address, copy source, and QR payload."""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / "site" / "support"
ADDRESS = "0x53f421bb0192cfcafd9a46a798a377e15c555027"
HEX = re.compile(r"0x[0-9a-fA-F]{40}")


def main() -> int:
    html = (SUPPORT / "index.html").read_text()
    js = (SUPPORT / "support.js").read_text()
    css = (SUPPORT / "support.css").read_text()
    found = set(HEX.findall(html + js))
    if found != {ADDRESS}:
        print("address set is", found)
        return 1
    if ADDRESS.lower() != ADDRESS or len(ADDRESS) != 42:
        print("address was altered")
        return 1
    if ">" + ADDRESS + "<" not in html:
        print("visible address mismatch")
        return 1
    if 'var ADDRESS = "' + ADDRESS + '";' not in js:
        print("script constant mismatch")
        return 1
    if 'data-payload="' + ADDRESS + '"' not in html:
        print("qr payload attribute mismatch")
        return 1
    blob = (html + js).lower()
    for banned in ("ethereum:", "bitcoin:", "solana:", "tron:", "walletconnect", "amount="):
        if banned in blob:
            print("banned marker", banned)
            return 1
    if "navigator.clipboard.writeText(ADDRESS)" not in js:
        print("copy does not use the address constant")
        return 1
    if "execCommand(\"copy\")" not in js:
        print("missing copy fallback")
        return 1
    if "aria-live=\"polite\"" not in html:
        print("missing live region")
        return 1
    if "prefers-color-scheme: light" not in css or "color-scheme: light dark" not in css:
        print("missing light and dark")
        return 1
    if "Arbitrum One" not in html or "Arbitrum One" not in js:
        print("missing Arbitrum One")
        return 1
    banned_networks = (
        "Ethereum",
        "BNB Chain",
        "Avalanche",
        "Polygon",
        "Base",
        "Optimism",
    )
    for name in banned_networks:
        if name in html or name in js:
            print("unverified network listed", name)
            return 1
    if 'type="radio"' in html or "input[name='network']" in js:
        print("network picker still present")
        return 1
    if html.count("<script") != 1 or 'src="support.js"' not in html:
        print("script is not a single local file")
        return 1
    if "onclick=" in html:
        print("inline handler")
        return 1
    if 'src="address-qr.svg"' not in html:
        print("qr is not the local svg")
        return 1
    svg = SUPPORT / "address-qr.svg"
    png = SUPPORT / "address-qr.png"
    if not svg.is_file() or not png.is_file():
        print("missing qr asset")
        return 1
    decoded = subprocess.check_output(["zbarimg", "--raw", "-q", str(png)], text=True).strip()
    if decoded != ADDRESS:
        print("qr decoded", decoded)
        return 1
    svg_text = svg.read_text().lower().replace("http://www.w3.org/2000/svg", "")
    if "http:" in svg_text or "https:" in svg_text:
        print("qr svg references a remote url")
        return 1
    print("support page ok", ADDRESS, "qr", decoded)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate kernel version overrides without interpolating inputs into a shell."""

import os
import re
import sys


VERSION_NAME_PATTERN = re.compile(
    r"v?[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?"
)


def get_overrides(environ=None):
    environ = os.environ if environ is None else environ
    name = environ.get("ZYSU_KERNEL_VERSION_NAME", "")
    code = environ.get("ZYSU_KERNEL_VERSION_CODE", "")
    normalized_code = code.lstrip("0") or "0"
    if name and (len(name) > 80 or VERSION_NAME_PATTERN.fullmatch(name) is None):
        raise ValueError(
            "ZYSU_KERNEL_VERSION_NAME must be a version such as 1.8.0 or "
            "v1.8.0-rc.1, at most 80 characters"
        )
    if code and (
        re.fullmatch(r"[0-9]+", code) is None
        or len(normalized_code) > 10
        or not 1 <= int(normalized_code) <= 2100000000
    ):
        raise ValueError("ZYSU_KERNEL_VERSION_CODE must be an integer from 1 to 2100000000")
    return name.removeprefix("v") or "-", str(int(normalized_code)) if code else "-"


if __name__ == "__main__":
    try:
        print(" ".join(get_overrides()))
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)

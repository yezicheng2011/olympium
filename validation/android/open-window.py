#!/usr/bin/env python3
"""Run the native C++ Android window backend installed with the desktop."""
import os
from pathlib import Path
import sys

binary = Path(__file__).resolve().parents[2] / 'image/olympium-android'
os.execv(str(binary), [str(binary), 'window', *sys.argv[1:]])

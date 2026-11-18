#!/usr/bin/env python3
"""Run the Android window implementation installed with the desktop."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'image'))
from android_window import main

if __name__ == '__main__':
    main()

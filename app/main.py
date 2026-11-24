#!/usr/bin/env python3
"""Stable Craft Anything entrypoint for the compiled Harness Studio launcher."""
from pathlib import Path
import os
import sys

launcher = Path(__file__).with_name("harnessstudio")
os.execv(str(launcher), [str(launcher), *sys.argv[1:]])

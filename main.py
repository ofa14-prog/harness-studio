#!/usr/bin/env python3
"""Craft Anything entrypoint for the built Harness Studio launcher."""
from pathlib import Path
import os
import sys

launcher = Path(__file__).with_name("harnessstudio")
os.execv(str(launcher), [str(launcher), *sys.argv[1:]])

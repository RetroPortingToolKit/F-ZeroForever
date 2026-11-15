"""Locate course-converter resources in a checkout or standalone helper."""
from pathlib import Path
import sys

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))


def native_tool(name):
    """Resolve a native helper; callers name it with the Windows .exe suffix."""
    folder = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else ROOT/'build'
    if sys.platform != 'win32' and name.endswith('.exe'):
        name = name[:-4]
    return folder/name

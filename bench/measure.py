#!/usr/bin/env python3
"""measure.py — ukur satu proses secara nyata: waktu dinding + puncak RSS.
Pemakaian: measure.py <perintah> [argumen...]
Keluaran (stderr): "<waktu_detik> <rss_KB>"; stdout diteruskan dari anak."""
import os
import sys
import time


def main() -> int:
    argv = sys.argv[1:]
    if not argv:
        print("pemakaian: measure.py <cmd> [args...]", file=sys.stderr)
        return 2
    t0 = time.monotonic()
    pid = os.fork()
    if pid == 0:
        try:
            os.execvp(argv[0], argv)
        except Exception:
            os._exit(127)
    _, status, ru = os.wait4(pid, 0)
    t1 = time.monotonic()
    code = os.waitstatus_to_exitcode(status)
    wall = t1 - t0
    print(f"{wall:.4f} {ru.ru_maxrss}", file=sys.stderr)
    return 0 if code == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

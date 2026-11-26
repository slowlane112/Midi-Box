#!/usr/bin/env python3
"""
Send files to the MIDI Box over Bluetooth (Linux and Windows).

    python3 midi_box_transfer.py song.mid [more files ...]
    python3 midi_box_transfer.py --install      # add "Send to MIDI Box" to the file manager

On the box: open Transfer, then press 8 to accept each file (7 declines).
Files are saved in /sdcard/downloads.

Needs Python 3.8+ (Windows 10 1709 or later). bleak is found automatically: if the Python
that starts the script doesn't have it, another Python on the machine that does is used,
and if none has it, it is installed once into the script's own folder (needs internet).

The last transfer is logged in midi_box_transfer.log next to this script.
"""
import asyncio
import os
import shutil
import subprocess
import sys
import tempfile

BleakClient = BleakScanner = None


class BleakError(Exception):
    pass


DEVICE_NAME = "MIDI BOX"
SERVICE_UUID = "1792a3bb-b1dd-4487-9bd1-94b805efbd23"
RX_CHAR_UUID = "f9e70f7c-3242-4eb4-93d0-2e7040901e43"   # we write here
TX_CHAR_UUID = "314ae969-157a-4dc2-9345-447e1f2014b0"   # the box notifies here

MAX_CHUNK = 240              # the box's v1 chunk size
MAX_SIZE = 4 * 1024 * 1024   # the box holds the whole file in PSRAM before saving it
ANSWER_TIMEOUT = 60          # seconds to press accept / decline on the box
SAVE_TIMEOUT = 15            # seconds for the box to write the file to the card

WINDOWS = sys.platform == "win32"
FAT_BAD = '\\/:*?"<>|'


# bleak is found or installed by the script itself, so it works whatever Python starts it
# and however (or whether) bleak was installed: apt, pip, pyenv, conda, Windows Store ...
PRIVATE_LIB = os.path.join(
    os.environ.get("LOCALAPPDATA", os.path.expanduser("~")) if sys.platform == "win32"
    else os.path.expanduser("~/.local/share"),
    "midi_box_transfer", f"lib-py{sys.version_info[0]}{sys.version_info[1]}")


def try_import():
    global BleakClient, BleakScanner, BleakError
    if os.path.isdir(PRIVATE_LIB) and PRIVATE_LIB not in sys.path:
        sys.path.insert(0, PRIVATE_LIB)
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as e:
        return str(e)
    try:
        from bleak.exc import BleakError
    except ImportError:
        pass   # very old bleak: keep the local BleakError
    return None


def other_pythons():
    """Other Python 3 interpreters on this machine."""
    found = []
    if sys.platform == "win32":
        if shutil.which("py"):
            try:
                out = subprocess.run(["py", "-0p"], capture_output=True, text=True, timeout=10).stdout
                found += [line.split()[-1] for line in out.splitlines() if line.strip().endswith(".exe")]
            except (OSError, subprocess.SubprocessError):
                pass
        found += [shutil.which(n) or "" for n in ("python", "python3")]
        import glob
        local = os.environ.get("LOCALAPPDATA", "")
        if local:   # Python install manager, and the classic python.org installer
            found += sorted(glob.glob(os.path.join(local, "Python", "pythoncore-*", "python.exe")), reverse=True)
            found += sorted(glob.glob(os.path.join(local, "Programs", "Python", "Python3*", "python.exe")), reverse=True)
    else:
        found += ["/usr/bin/python3", "/usr/local/bin/python3", "/opt/homebrew/bin/python3"]
        for d in os.environ.get("PATH", "").split(os.pathsep):
            if os.path.isdir(d):
                try:
                    found += [os.path.join(d, n) for n in sorted(os.listdir(d))
                              if n.startswith("python3") and n[7:].replace(".", "").isdigit()]
                except OSError:
                    pass
    me = os.path.realpath(sys.executable)
    out, seen = [], {me}
    for py in found:
        if py and os.path.isfile(py) and os.path.realpath(py) not in seen:
            seen.add(os.path.realpath(py))
            out.append(py)
    return out


def has_bleak(py):
    try:
        return subprocess.run([py, "-c", "import bleak"], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=30).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def rerun_with(py):
    print(f"Using {py}, which has bleak")
    env = dict(os.environ, MIDI_BOX_RERUN="1")
    args = [py, os.path.abspath(__file__)] + sys.argv[1:]
    sys.stdout.flush()
    if sys.platform == "win32":
        sys.exit(subprocess.run(args, env=env).returncode)
    os.execve(py, args, env)


def install_private():
    """pip install bleak into the script's own folder (needs internet, once)."""
    notify("MIDI Box", "Setting up Bluetooth support (first run, needs internet)...")
    os.makedirs(PRIVATE_LIB, exist_ok=True)
    cmd = [sys.executable, "-m", "pip", "install", "--disable-pip-version-check",
           "--no-warn-script-location", "--upgrade", "--target", PRIVATE_LIB, "bleak"]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        print(r.stdout[-2000:], r.stderr[-2000:], flush=True)
        if r.returncode == 0:
            return True
        if "No module named pip" in r.stderr:
            print("This Python has no pip.")
    except (OSError, subprocess.SubprocessError) as e:
        print(f"pip failed: {e}")
    return False


def load_bleak():
    err = try_import()
    if err is None:
        return
    print(f"{sys.executable}: {err}")
    if not os.environ.get("MIDI_BOX_RERUN"):
        for py in other_pythons():
            if has_bleak(py):
                rerun_with(py)
    if install_private() and try_import() is None:
        print(f"Installed bleak in {PRIVATE_LIB}")
        return
    for py in other_pythons():   # e.g. this Python has no pip but another one does
        if not os.environ.get("MIDI_BOX_RERUN"):
            env_ok = subprocess.run([py, "-m", "pip", "--version"], stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL).returncode == 0
            if env_ok:
                rerun_with(py)
    notify("MIDI Box", "Could not set up bleak; no internet, or no pip. See the log.", "critical")
    print("Fix: connect to the internet and try again, or install bleak once with")
    print("  Linux:   sudo apt install python3-bleak   (or: python3 -m pip install --user bleak)")
    print("  Windows: py -m pip install bleak")
    sys.exit(2)


# ---------------------------------------------------------------- messages

def notify(title, message, urgency="normal"):
    """Desktop notification on Linux; on Windows the console window shows everything."""
    print(f"{title}: {message}", flush=True)
    if not WINDOWS and shutil.which("notify-send"):
        subprocess.run(["notify-send", "-u", urgency, title, message], check=False)


# ---------------------------------------------------------------- one transfer at a time

class SingleInstance:
    """Lock so two transfers started from the file manager don't fight over the box.
    The OS drops the lock when the process ends, even after a crash."""

    def __init__(self):
        self.path = os.path.join(tempfile.gettempdir(), "midi_box_transfer.lock")
        self.f = None

    def __enter__(self):
        self.f = open(self.path, "a+")
        try:
            if WINDOWS:
                import msvcrt
                self.f.seek(0)
                msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.f.close()
            return False
        return True

    def __exit__(self, *exc):
        if self.f and not self.f.closed:
            self.f.close()


# ---------------------------------------------------------------- the transfer

def box_name(path):
    """File name as it will be stored on the box's SD card (ASCII, no FAT-illegal characters)."""
    name = os.path.basename(path)
    name = name.encode("ascii", "replace").decode("ascii")
    name = "".join("_" if (c in FAT_BAD or c == "?" or ord(c) < 32) else c for c in name)
    return name.strip(" .") or "file.bin"


async def find_box():
    print(f"Looking for {DEVICE_NAME} (open Transfer on the box)...")
    return await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15.0)


async def send_one(client, path, replies, chunk):
    size = os.path.getsize(path)
    name = box_name(path)
    print(f"\n{name}: {size} bytes")
    if size > MAX_SIZE:
        print(f"  Skipped: larger than {MAX_SIZE // (1024 * 1024)} MB, too big for the box's memory.")
        return False

    while not replies.empty():
        replies.get_nowait()
    await client.write_gatt_char(RX_CHAR_UUID, f"META:{size}:{name}".encode("ascii"), response=True)
    print("  Waiting for you to accept on the box (8 accepts, 7 declines)...")
    try:
        while True:
            msg = await asyncio.wait_for(replies.get(), ANSWER_TIMEOUT)
            if msg.startswith("ACCEPTED"):
                break
            if msg.startswith("DECLINED"):
                why = msg.partition(":")[2]
                print(f"  Declined on the box{': ' + why if why else ''}.")
                return False
    except asyncio.TimeoutError:
        print("  No answer from the box.")
        return False

    sent = 0
    with open(path, "rb") as f:
        while sent < size:
            data = f.read(min(chunk, size - sent))
            if not data:
                break   # the file got shorter while we were sending
            # write with response: the box acknowledges each chunk, so it is never flooded
            await client.write_gatt_char(RX_CHAR_UUID, data, response=True)
            sent += len(data)
            print(f"  Sending {sent * 100 // size if size else 100:3d}%  ({sent}/{size})", end="\r")
    print()
    if sent != size:
        print("  The file changed size while it was being sent. Not finished.")
        return False
    await client.write_gatt_char(RX_CHAR_UUID, b"EOF", response=True)

    # boxes with the v2 firmware reply SAVED / FAILED; the v1 firmware says nothing
    try:
        while True:
            msg = await asyncio.wait_for(replies.get(), SAVE_TIMEOUT)
            if msg.startswith("SAVED"):
                print(f"  Saved on the box as {msg.partition(':')[2] or name}.")
                return True
            if msg.startswith("FAILED"):
                print(f"  The box could not save it: {msg.partition(':')[2]}")
                return False
    except asyncio.TimeoutError:
        print("  Sent (the box did not confirm the save).")
        return True


async def send_files(paths):
    device = await find_box()
    if not device:
        notify("MIDI Box", "Not found. Is Transfer open on the box and Bluetooth on?", "critical")
        return 1

    print(f"Found {DEVICE_NAME} [{device.address}], connecting...")
    replies = asyncio.Queue()
    loop = asyncio.get_running_loop()

    def on_notify(_sender, data):
        msg = bytes(data).decode("utf-8", errors="ignore").strip()
        loop.call_soon_threadsafe(replies.put_nowait, msg)

    kwargs = {"timeout": 20.0}
    if WINDOWS:
        kwargs["winrt"] = {"use_cached_services": False}   # don't trust services cached by Windows
    ok = 0
    try:
        async with BleakClient(device, **kwargs) as client:
            # Windows reports the negotiated packet size, so fit the chunks to it. BlueZ can
            # report 23 before it has negotiated (usually 517), so Linux keeps v1's 240.
            chunk = MAX_CHUNK
            mtu = getattr(client, "mtu_size", 0) or 0
            if WINDOWS and 23 <= mtu < MAX_CHUNK + 3:
                chunk = mtu - 3
            await client.start_notify(TX_CHAR_UUID, on_notify)
            for p in paths:
                if await send_one(client, p, replies, chunk):
                    ok += 1
            try:
                await client.stop_notify(TX_CHAR_UUID)
            except (BleakError, OSError):
                pass
    except (BleakError, OSError, asyncio.TimeoutError) as e:
        print(f"\nBluetooth error: {e or type(e).__name__}")
        notify("MIDI Box", "Connection lost during the transfer", "critical")
        return 1

    if ok == len(paths):
        notify("MIDI Box", f"{ok} file{'s' if ok != 1 else ''} sent")
        return 0
    notify("MIDI Box", f"{ok} of {len(paths)} files sent", "critical")
    return 1


# ---------------------------------------------------------------- file manager integration

NEMO_ACTION = """[Nemo Action]
Active=true
Name=Send to MIDI Box
Comment=Send the selected files to the MIDI Box over Bluetooth
Exec={python} "{script}" %F
Icon-Name=bluetooth-active
Selection=notnone
Extensions=any;
Dependencies=python3;
"""

WINDOWS_CMD = """@echo off
title Send to MIDI Box
"{python}" "{script}" %*
echo.
pause
"""


def python_with_bleak():
    """Pick the Python the file manager will start, and make sure it can load bleak now,
    while there is a window to show any problem: this Python if it has bleak (or the
    script's own copy), else another Python on the machine that has it, else install
    bleak into the script's own folder for this Python."""
    if try_import() is None:
        return sys.executable
    for py in other_pythons():
        if has_bleak(py):
            print(f"{py} has bleak")
            return py
    print("No Python here has bleak yet; setting it up for this one (needs internet)...")
    if install_private() and try_import() is None:
        print(f"bleak installed in {PRIVATE_LIB}")
        return sys.executable
    v = f"{sys.version_info[0]}.{sys.version_info[1]}"
    print(f"\nCould not install bleak for Python {v} (details above).")
    if WINDOWS and sys.version_info >= (3, 14):
        print(f"Python {v} may be too new: bleak's Windows Bluetooth parts may not have")
        print("ready-built packages for it yet. Install an older Python next to it, e.g.")
        print("    py install 3.13")
        print("then run --install again; the script will find and use it.")
    else:
        print("Check the internet connection, then run --install again.")
    return sys.executable


def install():
    script = os.path.abspath(__file__)
    python = python_with_bleak()
    print(f"Using {python}")
    if WINDOWS:
        python = python.replace("pythonw.exe", "python.exe")
        sendto = os.path.join(os.environ["APPDATA"], "Microsoft", "Windows", "SendTo")
        target = os.path.join(sendto, "MIDI Box.cmd")
        with open(target, "w") as f:
            f.write(WINDOWS_CMD.format(python=python, script=script))
        print(f"Added {target}")
        print('Right-click files in Explorer > Send to > "MIDI Box"')
        print("(Windows 11: Show more options > Send to)")
        return
    actions = os.path.expanduser("~/.local/share/nemo/actions")
    os.makedirs(actions, exist_ok=True)
    target = os.path.join(actions, "send_to_midi_box.nemo_action")
    with open(target, "w") as f:
        f.write(NEMO_ACTION.format(python=python, script=script))
    old = os.path.join(actions, "transfer_to_midi_box.nemo_action")
    if os.path.exists(old):
        os.remove(old)
        print(f"Removed the old {old}")
    print(f"Added {target}; restart Nemo with: nemo -q")


def log_path():
    """Last transfer's log: next to the script (like the v1 Nemo action), or in the temp
    folder if that folder can't be written."""
    here = os.path.dirname(os.path.abspath(__file__))
    if os.access(here, os.W_OK):
        return os.path.join(here, "midi_box_transfer.log")
    return os.path.join(tempfile.gettempdir(), "midi_box_transfer.log")


LOG_PATH = log_path()


class Tee:
    """Writes to the log and, when there is one, the terminal."""

    def __init__(self, log, screen):
        self.log, self.screen = log, screen

    def write(self, text):
        self.log.write(text)
        if self.screen is not None:
            try:
                self.screen.write(text)
            except (OSError, ValueError):
                self.screen = None
        return len(text)

    def flush(self):
        self.log.flush()
        if self.screen is not None:
            try:
                self.screen.flush()
            except (OSError, ValueError):
                self.screen = None

    def isatty(self):
        return False


def start_log():
    """Every run keeps a log of the last transfer, and any crash becomes a notification,
    so nothing fails silently when started from the file manager."""
    screen = sys.stdout if (sys.stdout is not None and sys.stdout.isatty()) else None
    try:
        # a restart under another Python (see load_bleak) adds to the same log
        mode = "a" if os.environ.get("MIDI_BOX_RERUN") else "w"
        log = open(LOG_PATH, mode, buffering=1, encoding="utf-8", errors="replace")
        sys.stdout = sys.stderr = Tee(log, screen)
    except OSError:
        pass

    def crashed(kind, value, tb):
        import traceback
        traceback.print_exception(kind, value, tb)
        notify("MIDI Box", f"Error: {value or kind.__name__} (see {LOG_PATH})", "critical")
    sys.excepthook = crashed


def main():
    start_log()
    import time
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')}  {sys.executable} {' '.join(sys.argv)}")
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0 if args else 1
    if args[0] == "--install":
        install()
        return 0
    paths = [a for a in args if os.path.isfile(a)]
    for a in args:
        if not os.path.isfile(a):
            print(f"Skipping '{a}': not a file")
    if not paths:
        notify("MIDI Box", "Nothing to send", "critical")
        return 1
    load_bleak()
    names = ", ".join(os.path.basename(p) for p in paths)
    notify("MIDI Box", f"Sending {names}... (accept on the box)")
    with SingleInstance() as got:
        if not got:
            notify("MIDI Box", "A transfer is already in progress")
            return 1
        return asyncio.run(send_files(paths))


if __name__ == "__main__":
    sys.exit(main())

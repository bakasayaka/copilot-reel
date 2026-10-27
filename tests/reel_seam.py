"""Driver for Copilot Reel's interaction test seam.

A test instance is the Release CopilotReel.exe started unpackaged with
COPILOT_REEL_TEST_INPUT=<run id> and an isolated COPILOT_REEL_DATA_DIR (src/test_input.h).
It owns the '.Test.<run id>' namespace and the title 'Copilot Reel Test <run id>', so the
installed reel stays resident and is never addressed. The driver reaches the test instance
only through messages sent to that window:

- CommandPackets (WM_COPYDATA 'REEL'), exactly as a second CopilotReel.exe would send them;
- the Copilot fast path (PostMessage 0x8001 with wParam 0=Tap, 1=HoldStart, 2=HoldStop);
- TestPackets (WM_COPYDATA 'RTST') for the synthetic pointer, clicks, presses, releases and keys.

It never moves the real pointer, never sends input to the system and never activates a
window. user32 is bound through an explicit allowlist, shell32 only for reading where a tray
icon is, and assert_no_input_injection() refuses any source that names an input-injection API.
"""
import ctypes
from ctypes import wintypes as w
import hashlib
import math
import os
from pathlib import Path
import re
import subprocess
import time
import uuid

WINDOW_CLASS = 'CopilotReel.Controller'
INSTALLED_TITLE = 'Copilot Reel'
EDITOR_CLASS = 'CopilotReel.Osd'
DIALOG_CLASS = '#32770'
CANVAS_DIP = 400
CENTER_DIP = 192
SECTOR_RADIUS_DIP = 112
DEAD_ZONE_DIP = 44
# Wherever the edge clamp leaves a hold, on the plate or off it, it selects by direction once the
# pointer moves; there is no center entry (src/core.h PlaceWheel).
PLATE_RADIUS_DIP = 168
# A page selects nothing until the pointer leaves this square around where it opened (MovePointer).
OPENING_SLOP_DIP = 4
# Adjust chevrons of slot 0 (the hub language): 40x40 DIP boxes that exclude the square within
# 25 DIP of slot 0's centre, so a click on the seat confirms the tile; click the chevron centres to adjust.
SLOT0_DIP = (CENTER_DIP, CENTER_DIP - SECTOR_RADIUS_DIP)
EARLIER_CHEVRON_DIP = (CENTER_DIP - 40, 80)
LATER_CHEVRON_DIP = (CENTER_DIP + 40, 80)
SEAT_GUARD_DIP = 25

IPC_MAGIC = 0x5245454c
TEST_MAGIC = 0x52545354
COPILOT_MESSAGE = 0x8001  # WM_APP + 1
WM_CLOSE = 0x0010
WM_COPYDATA = 0x004A
WM_SYSCOMMAND = 0x0112
SC_MINIMIZE = 0xF020
SW_SHOWMINNOACTIVE = 7
SMTO_ABORTIFHUNG = 0x0002
CREATE_NO_WINDOW = 0x08000000
MONITOR_DEFAULTTONEAREST = 2
MONITOR_DEFAULTTOPRIMARY = 1

# src/core.h Command
BACKGROUND, SETTINGS, PREVIEW, HOLD_START, HOLD_STOP, CANCEL, QUIT, TAP, DIAGNOSTICS = range(9)
# src/test_input.h TestAction
POINTER, CLICK, RIGHT_CLICK, KEY, PRESS, RELEASE, ACTION_COUNT = range(7)
# Copilot fast-path wParam
FAST_TAP, FAST_HOLD_START, FAST_HOLD_STOP = range(3)
# src/radial_menu.h MenuPage (4 was the Preferred agent page)
PAGE_MAIN, PAGE_MANAGE, PAGE_PRESETS, PAGE_SETTINGS, PAGE_ENTRY, PAGE_ABOUT = 0, 1, 2, 3, 5, 6

VK_TAB, VK_RETURN, VK_ESCAPE, VK_SPACE = 0x09, 0x0D, 0x1B, 0x20
VK_LEFT, VK_RIGHT, VK_DELETE = 0x25, 0x27, 0x2E
VK_A, VK_D = ord('A'), ord('D')
VK_NONE = 0  # routes nowhere: a Key packet with it is a pure ordering barrier

RUN_ID = re.compile(r'[A-Za-z0-9-]{8,40}')
DONE = re.compile(r'Test input (\d+) done; page=(-?\d+); selected=(-?\d+); active=([01]); drag=(-?\d+); target=([\w:]+)')
OPENING = re.compile(r'(Hold started|Preview opened); pointer=(-?\d+),(-?\d+); '
                     r'window=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
# The whole 'Hold started' line; a hold that cut short a running exit ends with '; interrupted=exit'.
HOLD_LINE = re.compile(r'Hold started;[^\r\n]*')
INTERRUPTED_EXIT = '; interrupted=exit'
# Every completed agent discovery, at startup and whenever the hub or 'Your apps' opens.
DISCOVERY = re.compile(r'Agent discovery(?: updated)?: (\d+) catalog entries')
BARE_START = re.compile(r'^[\d-]+ [\d:.]+ (Started without arguments; activation=none;[^\r\n]*command=preview)\r?$',
                        re.MULTILINE)
RAW_INPUT_SKIPPED = 'Raw Input skipped: test input seam'


class CommandPacket(ctypes.Structure):
    _fields_ = [('version', ctypes.c_uint), ('command', ctypes.c_uint),
                ('stamp', ctypes.c_uint64), ('pointer', w.POINT)]


class TestPacket(ctypes.Structure):
    _fields_ = [('version', ctypes.c_uint), ('action', ctypes.c_uint), ('sequence', ctypes.c_uint64),
                ('screen', w.POINT), ('vk', ctypes.c_uint), ('reserved', ctypes.c_uint)]


class CopyData(ctypes.Structure):
    _fields_ = [('dwData', ctypes.c_size_t), ('cbData', w.DWORD), ('lpData', ctypes.c_void_p)]


class MonitorInfo(ctypes.Structure):
    _fields_ = [('cbSize', w.DWORD), ('rcMonitor', w.RECT), ('rcWork', w.RECT), ('dwFlags', w.DWORD)]


assert ctypes.sizeof(CommandPacket) == 24, 'CommandPacket must match src/main.cpp'
assert ctypes.sizeof(TestPacket) == 32 and TestPacket.sequence.offset == 8 and TestPacket.screen.offset == 16 \
    and TestPacket.vk.offset == 24, 'TestPacket must match src/test_input.h'
assert ctypes.sizeof(CopyData) == 24, 'COPYDATASTRUCT is 24 bytes in a 64-bit process'

ENUM_WINDOWS = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
# Every user32 function the seam suite may call. Nothing here moves the pointer, synthesizes
# input or changes the foreground window; anything else is unreachable through `user32`.
# ShowWindowAsync is only ever called with SW_SHOWMINNOACTIVE (Reel.minimize_quietly).
_USER32 = {
    'SetProcessDpiAwarenessContext': ([ctypes.c_void_p], w.BOOL),
    'FindWindowW': ([w.LPCWSTR, w.LPCWSTR], w.HWND),
    'EnumWindows': ([ENUM_WINDOWS, w.LPARAM], w.BOOL),
    'GetWindowThreadProcessId': ([w.HWND, ctypes.POINTER(w.DWORD)], w.DWORD),
    'GetClassNameW': ([w.HWND, w.LPWSTR, ctypes.c_int], ctypes.c_int),
    'GetWindowTextW': ([w.HWND, w.LPWSTR, ctypes.c_int], ctypes.c_int),
    'GetWindowRect': ([w.HWND, ctypes.POINTER(w.RECT)], w.BOOL),
    'IsWindow': ([w.HWND], w.BOOL),
    'IsWindowVisible': ([w.HWND], w.BOOL),
    'IsIconic': ([w.HWND], w.BOOL),
    'ShowWindowAsync': ([w.HWND, ctypes.c_int], w.BOOL),
    'GetDpiForWindow': ([w.HWND], w.UINT),
    'GetForegroundWindow': ([], w.HWND),
    'MonitorFromPoint': ([w.POINT, w.DWORD], w.HANDLE),
    'GetMonitorInfoW': ([w.HANDLE, ctypes.POINTER(MonitorInfo)], w.BOOL),
    'PostMessageW': ([w.HWND, w.UINT, w.WPARAM, w.LPARAM], w.BOOL),
    'SendMessageTimeoutW': ([w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT,
                             ctypes.POINTER(ctypes.c_size_t)], w.LPARAM),
}


class _Bound:
    def __init__(self, library, table):
        dll = ctypes.WinDLL(library, use_last_error=True)
        for name, (argtypes, restype) in table.items():
            function = getattr(dll, name)
            function.argtypes, function.restype = argtypes, restype
            setattr(self, name, function)


class Guid(ctypes.Structure):
    _fields_ = [('Data1', w.DWORD), ('Data2', w.WORD), ('Data3', w.WORD), ('Data4', ctypes.c_ubyte * 8)]

    @classmethod
    def of(cls, value):
        data = value.bytes
        return cls(int.from_bytes(data[0:4], 'big'), int.from_bytes(data[4:6], 'big'), int.from_bytes(data[6:8], 'big'),
                   (ctypes.c_ubyte * 8)(*data[8:]))


class NotifyIconIdentifier(ctypes.Structure):
    _fields_ = [('cbSize', w.DWORD), ('hWnd', w.HWND), ('uID', w.UINT), ('guidItem', Guid)]


user32 = _Bound('user32', _USER32)
kernel32 = _Bound('kernel32', {'GetSystemTimePreciseAsFileTime': ([ctypes.POINTER(ctypes.c_uint64)], None)})
# Only reads where the Shell keeps a notification-area icon; nothing is added, changed or shown.
shell32 = _Bound('shell32', {'Shell_NotifyIconGetRect': ([ctypes.POINTER(NotifyIconIdentifier), ctypes.POINTER(w.RECT)], ctypes.c_long)})
# The installed reel's notification-area icon (kTrayGuid in src/main.cpp).
TRAY_GUID = uuid.UUID('55f5d570-3345-477e-89fc-bc7a2e773133')
# Physical pixels, like the per-monitor-aware reel.
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


def forbidden_tokens():
    # Spelled in parts so this file can check itself.
    return tuple(a + b for a, b in (('Send', 'Input'), ('SetCursor', 'Pos'), ('mouse', '_event'), ('keybd', '_event')))


def assert_no_input_injection(*paths):
    """Refuse to run if a driver or runner source names an input-injection API."""
    found = []
    for path in paths:
        text = Path(path).read_text(encoding='utf-8')
        found += [f'{Path(path).name}: {token}' for token in forbidden_tokens() if token in text]
    if found:
        raise SystemExit('Input injection is not allowed in the seam suite: ' + ', '.join(found))
    return [str(Path(path).resolve()) for path in paths]


def assert_seam_binary(exe):
    """Refuse a binary without the seam: it would start in the user's own namespace."""
    image = Path(exe).read_bytes()
    missing = [text for text in ('COPILOT_REEL_TEST_INPUT', 'Copilot Reel Test ', 'Test input seam active')
               if text.encode('utf-16-le') not in image]
    if missing:
        raise SystemExit(f'{exe} has no interaction test seam (missing {missing}); nothing was started.')


def wait(predicate, description, timeout=6.0, interval=.01):
    deadline = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() >= deadline:
            raise AssertionError('Timed out waiting for ' + description)
        time.sleep(interval)


_last_stamp = 0


def filetime_now():
    """A strictly increasing FILETIME from the same clock as the reel's EventStamp."""
    global _last_stamp
    value = ctypes.c_uint64()
    kernel32.GetSystemTimePreciseAsFileTime(ctypes.byref(value))
    _last_stamp = max(value.value, _last_stamp + 1)
    return _last_stamp


def window_process(hwnd):
    pid = w.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


def window_class(hwnd):
    buffer = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buffer, len(buffer))
    return buffer.value


def window_title(hwnd):
    buffer = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, buffer, len(buffer))
    return buffer.value


def process_windows(pid):
    """Top-level windows owned by one process, as (hwnd, class, title, visible)."""
    found = []

    @ENUM_WINDOWS
    def collect(hwnd, unused):
        if window_process(hwnd) == pid:
            found.append((hwnd, window_class(hwnd), window_title(hwnd), bool(user32.IsWindowVisible(hwnd))))
        return True

    user32.EnumWindows(collect, 0)
    return found


def installed_window():
    """The user's own resident reel, if any. Only ever observed, never messaged."""
    return user32.FindWindowW(WINDOW_CLASS, INSTALLED_TITLE) or None


def foreground():
    hwnd = user32.GetForegroundWindow()
    return (hwnd or None), (window_process(hwnd) if hwnd else 0)


def work_area(point):
    info = MonitorInfo(cbSize=ctypes.sizeof(MonitorInfo))
    assert user32.GetMonitorInfoW(user32.MonitorFromPoint(w.POINT(*point), MONITOR_DEFAULTTONEAREST), ctypes.byref(info))
    work = info.rcWork
    return work.left, work.top, work.right, work.bottom


def tray_icon_rect(guid=None, hwnd=None, uid=1):
    """The screen rectangle of a notification-area icon, by its GUID or its window and uID; None when
    the Shell has no such icon. The installed reel's icon is found by TRAY_GUID from any process."""
    identifier = NotifyIconIdentifier(cbSize=ctypes.sizeof(NotifyIconIdentifier))
    if guid:
        identifier.guidItem = Guid.of(guid)
    else:
        identifier.hWnd, identifier.uID = hwnd, uid
    bounds = w.RECT()
    if shell32.Shell_NotifyIconGetRect(ctypes.byref(identifier), ctypes.byref(bounds)) != 0:
        return None
    return bounds.left, bounds.top, bounds.right, bounds.bottom


def primary_work_area():
    info = MonitorInfo(cbSize=ctypes.sizeof(MonitorInfo))
    assert user32.GetMonitorInfoW(user32.MonitorFromPoint(w.POINT(0, 0), MONITOR_DEFAULTTOPRIMARY), ctypes.byref(info))
    work = info.rcWork
    return work.left, work.top, work.right, work.bottom


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


def encode_ini(value):
    return value.replace('\\', '\\\\').replace('\t', '\\t')


def decode_ini(value):
    return re.sub(r'\\(.)', lambda match: '\t' if match.group(1) == 't' else match.group(1), value)


def write_apps(path, apps):
    """apps.ini as src/settings.cpp expects it: UTF-16 LE with a BOM. apps: (name, target, arguments)."""
    text = '; isolated seam test\r\n[CopilotReel]\r\nVersion=1\r\nCount=' + str(len(apps)) + '\r\n'
    for index, (name, target, arguments) in enumerate(apps):
        text += (f'[App{index}]\r\nName={encode_ini(name)}\r\nKind=shell\r\nTarget={encode_ini(str(target))}\r\n'
                 f'Arguments={encode_ini(arguments)}\r\nWorkingDirectory=\r\n')
    Path(path).write_text(text, encoding='utf-16', newline='')


def read_app_names(path):
    """Display order of the saved apps."""
    names, section = {}, None
    for line in Path(path).read_text(encoding='utf-16').splitlines():
        header = re.fullmatch(r'\[App(\d+)\]', line.strip())
        if header:
            section = int(header.group(1))
        elif section is not None and line.startswith('Name='):
            names[section] = decode_ini(line[5:])
    return [names[index] for index in sorted(names)]


class Diagnostics:
    def __init__(self, text):
        self.text = text
        self.values = dict(re.findall(r'^([^=\r\n]+)=([^\r\n]*)', text, re.MULTILINE))
        names = self.values.get('menuNames', '')
        self.names = names.split('|') if names else []
        # Artwork identities in menuNames order, joined by TAB because an identity contains '|'.
        art = self.values.get('menuArt', '')
        self.art = art.split('\t') if art else []

    def __getitem__(self, key):
        return self.values[key]

    def get(self, key, default=None):
        return self.values.get(key, default)

    @property
    def page(self):
        return int(self['menuPage'])

    @property
    def selected(self):
        return int(self['gestureSelected'])

    @property
    def title(self):
        match = re.search(r'; menu=([^;]*);', self.text)
        return match.group(1) if match else None

    @property
    def surface_draws(self):
        return int(re.search(r'surface draws=(\d+)', self.text).group(1))

    @property
    def rebuilds(self):
        """(pages built, seats that appeared): an update in place adds to neither."""
        return (int(re.search(r'pages built=(\d+)', self.text).group(1)),
                int(re.search(r'seat appears=(\d+)', self.text).group(1)))

    def flag(self, key):
        return self.values.get(key) == '1'

    def index(self, name):
        matches = [i for i, entry in enumerate(self.names) if entry == name]
        assert len(matches) == 1, f'Expected exactly one menu entry named {name!r}: {self.names}'
        return matches[0]


class Reel:
    """One isolated test instance: its process, window, data directory and synthetic cursor."""

    def __init__(self, exe, data, run_id=None, pointer=None, visual=None, extra=None):
        self.exe = Path(exe).resolve()
        self.data = Path(data).resolve()
        self.run_id = run_id or uuid.uuid4().hex
        assert RUN_ID.fullmatch(self.run_id), 'A run id has 8-40 characters of A-Z, a-z, 0-9 or -'
        self.title = 'Copilot Reel Test ' + self.run_id
        left, top, right, bottom = primary_work_area()
        self.cursor = tuple(pointer) if pointer else ((left + right) // 2, (top + bottom) // 2)
        self.visual = visual
        # More COPILOT_REEL_TEST_* settings for this instance, such as an update feed.
        self.extra = dict(extra or {})
        self.process = None
        self.hwnd = None
        self.sequence = 0
        self.log = self.data / 'run.log'
        self.marker = self.data / 'launch-probe.txt'
        self.arguments = self.data / 'launch-probe-args.txt'
        self.data.mkdir(parents=True, exist_ok=True)

    # Process lifetime -------------------------------------------------------------------
    def environment(self, pointer=None, run_id=None, data=None):
        env = {key: value for key, value in os.environ.items() if not key.upper().startswith('COPILOT_REEL_')}
        env['COPILOT_REEL_DATA_DIR'] = str(data or self.data)
        env['COPILOT_REEL_TEST_INPUT'] = run_id or self.run_id
        point = pointer or self.cursor
        env['COPILOT_REEL_TEST_POINTER'] = f'{round(point[0])},{round(point[1])}'
        if self.visual:
            env['COPILOT_REEL_VISUAL_OVERRIDE'] = self.visual
        env.update(self.extra)
        return env

    def start(self, argument='--background', pointer=None):
        """Start the test instance; returns the run.log offset before it started.

        argument=None starts it bare, with no command-line argument at all."""
        assert self.process is None, 'This fixture already owns a running test instance.'
        assert not user32.FindWindowW(WINDOW_CLASS, self.title), 'A test instance with this run id is already resident.'
        if pointer:
            self.cursor = (round(pointer[0]), round(pointer[1]))
        offset = len(self.read_log())
        command = [str(self.exe)] + ([] if argument is None else [argument])
        self.process = subprocess.Popen(command, env=self.environment(), creationflags=CREATE_NO_WINDOW)

        def ready():
            if self.process.poll() is not None:
                raise AssertionError(f'The test instance exited with code {self.process.returncode} during startup.')
            hwnd = user32.FindWindowW(WINDOW_CLASS, self.title)
            return hwnd if hwnd and f'Test input seam active: run={self.run_id}' in self.read_log()[offset:] else None

        self.hwnd = wait(ready, 'the seam-activated test controller', timeout=15)
        assert window_process(self.hwnd) == self.process.pid, 'The test window must belong to the process this fixture started.'
        assert self.hwnd != installed_window(), 'The test instance must never be the installed reel.'
        return offset

    def stop(self):
        assert self.process is not None
        self.close_owned_dialogs()
        assert self.send_command(QUIT), 'The test instance refused Quit.'
        assert self.process.wait(timeout=8) == 0, 'The test instance must exit cleanly.'
        wait(lambda: not user32.FindWindowW(WINDOW_CLASS, self.title), 'test controller shutdown')
        self.process = self.hwnd = None

    def shutdown(self):
        """Best-effort cleanup that only ever touches the process this fixture started."""
        if self.process is None:
            return
        try:
            if self.process.poll() is None:
                self.close_owned_dialogs()
                if self.hwnd and user32.IsWindow(self.hwnd):
                    self.send_command(QUIT)
                self.process.wait(timeout=8)
        except Exception:
            if self.process.poll() is None:
                self.process.terminate()
                self.process.wait(timeout=5)
        self.process = self.hwnd = None

    def close_owned_dialogs(self):
        if self.process is None or self.process.poll() is not None:
            return
        for hwnd, class_name, _, _ in process_windows(self.process.pid):
            if class_name in (EDITOR_CLASS, DIALOG_CLASS):
                user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
                time.sleep(.1)

    def client(self, *arguments, timeout=10):
        """A second CopilotReel.exe in the same test namespace: the real handoff client path."""
        return subprocess.run([str(self.exe), *arguments], env=self.environment(), creationflags=CREATE_NO_WINDOW,
                              timeout=timeout).returncode

    def owned_windows(self, class_name=None):
        if self.process is None:
            return []
        return [entry for entry in process_windows(self.process.pid) if class_name is None or entry[1] == class_name]

    # Channels ----------------------------------------------------------------------------
    def _copy_data(self, tag, packet):
        copy = CopyData(tag, ctypes.sizeof(packet), ctypes.cast(ctypes.pointer(packet), ctypes.c_void_p))
        result = ctypes.c_size_t()
        sent = user32.SendMessageTimeoutW(self.hwnd, WM_COPYDATA, 0, ctypes.addressof(copy), SMTO_ABORTIFHUNG, 2000,
                                          ctypes.byref(result))
        return bool(sent) and result.value != 0

    def send_command(self, command, pointer=None, stamp=None, version=1):
        """A CommandPacket with an exact pointer and a FILETIME stamp. True when the reel queued it."""
        point = pointer or self.cursor
        packet = CommandPacket(version, command, filetime_now() if stamp is None else stamp,
                               w.POINT(round(point[0]), round(point[1])))
        return self._copy_data(IPC_MAGIC, packet)

    def send_test_packet(self, action, point=None, vk=0, version=1, reserved=0, sequence=None):
        """A raw TestPacket; returns (accepted, sequence) without waiting for it."""
        if sequence is None:
            self.sequence += 1
            sequence = self.sequence
        point = point or self.cursor
        packet = TestPacket(version, action, sequence, w.POINT(round(point[0]), round(point[1])), vk, reserved)
        return self._copy_data(TEST_MAGIC, packet), sequence

    def copilot(self, wparam):
        """The Shell's fast path; the reel stamps it on receipt and uses the synthetic cursor."""
        assert user32.PostMessageW(self.hwnd, COPILOT_MESSAGE, wparam, 0)

    def post(self, message, wparam=0, lparam=0):
        assert user32.PostMessageW(self.hwnd, message, wparam, lparam)

    def test_input(self, action, point=None, vk=0):
        """Send one test packet and wait for its 'Test input <seq> done' barrier in run.log."""
        offset = len(self.read_log())
        accepted, sequence = self.send_test_packet(action, point, vk)
        assert accepted, f'The test instance rejected test packet {sequence}.'
        if point is not None and action in (POINTER, CLICK, RIGHT_CLICK, PRESS, RELEASE):
            self.cursor = (round(point[0]), round(point[1]))

        def done():
            text = self.read_log()[offset:]
            for match in DONE.finditer(text):
                if int(match.group(1)) == sequence:
                    return match, text
            return None

        match, text = wait(done, f'test input {sequence}')
        return {'sequence': sequence, 'page': int(match.group(2)), 'selected': int(match.group(3)),
                'active': match.group(4) == '1', 'drag': int(match.group(5)), 'target': match.group(6),
                'passedThrough': f'Test input {sequence} passed through the reel' in text}

    def pointer(self, point):
        return self.test_input(POINTER, point)

    def path(self, points):
        return [self.pointer(point) for point in points]

    def click(self, point):
        return self.test_input(CLICK, point)

    def press(self, point):
        return self.test_input(PRESS, point)

    def release_button(self, point):
        return self.test_input(RELEASE, point)

    def drag(self, start, points, end):
        """Press at start, move through points, release at end; returns every step's barrier."""
        steps = [self.press(start)]
        steps += [self.pointer(point) for point in points]
        steps.append(self.release_button(end))
        return steps

    def right_click(self, point=None):
        return self.test_input(RIGHT_CLICK, point or self.center())

    def key(self, vk):
        return self.test_input(KEY, vk=vk)

    def sync(self):
        """Every earlier message to the reel has been handled once this returns."""
        return self.key(VK_NONE)

    def diagnostics(self):
        path = self.data / 'diagnostics.txt'
        before = self.read_log().count('Diagnostics written')
        if path.exists():
            path.unlink()
        assert self.send_command(DIAGNOSTICS), 'The test instance refused Diagnostics.'
        wait(lambda: self.read_log().count('Diagnostics written') > before and path.exists(), 'fresh diagnostics')
        result = Diagnostics(path.read_text(encoding='utf-8'))
        assert 'last error=' not in result.text, 'The renderer recorded an error: ' + result.text
        return result

    # Observation -------------------------------------------------------------------------
    def read_log(self):
        return self.log.read_text(encoding='utf-8', errors='replace') if self.log.exists() else ''

    def launches(self):
        return self.marker.read_text(encoding='utf-8').count('launched') if self.marker.exists() else 0

    def launch_arguments(self):
        return self.arguments.read_text(encoding='utf-8').splitlines() if self.arguments.exists() else []

    def launched_targets(self):
        return re.findall(r'Launching target: ([^\r\n]+)', self.read_log())

    def apps(self):
        return read_app_names(self.data / 'apps.ini')

    def parked(self):
        """Apps taken out of the reel (parked.ini), most recent first; empty when there is none."""
        path = self.data / 'parked.ini'
        return read_app_names(path) if path.exists() else []

    def visible(self):
        return bool(self.hwnd and user32.IsWindowVisible(self.hwnd))

    def iconic(self):
        return bool(user32.IsIconic(self.hwnd))

    def minimize_quietly(self):
        """Make the test window iconic without activating it; the pointer and the foreground stay put."""
        assert user32.ShowWindowAsync(self.hwnd, SW_SHOWMINNOACTIVE), 'ShowWindowAsync was refused.'
        wait(self.iconic, 'the iconic test window')

    def hidden(self, timeout=6):
        wait(lambda: not self.visible(), 'completion of the closing animation', timeout)

    def rect(self):
        bounds = w.RECT()
        assert user32.GetWindowRect(self.hwnd, ctypes.byref(bounds))
        return bounds.left, bounds.top, bounds.right, bounds.bottom

    def center(self):
        left, top, right, bottom = self.rect()
        return (left + right) / 2, (top + bottom) / 2

    def scale(self):
        left, _, right, _ = self.rect()
        return (right - left) / CANVAS_DIP

    def canvas(self, x, y):
        """A point given in canvas DIP (0..400) as physical screen pixels."""
        left, top, _, _ = self.rect()
        return round(left + x * self.scale()), round(top + y * self.scale())

    def sector(self, index, count, radius=SECTOR_RADIUS_DIP):
        x, y = self.center()
        angle = index * 2 * math.pi / count
        return round(x + math.sin(angle) * radius * self.scale()), round(y - math.cos(angle) * radius * self.scale())

    def center_point(self):
        """The plate center in whole pixels: inside the cancel zone, and Back when clicked."""
        x, y = self.center()
        return round(x), round(y)

    # Menu by name ------------------------------------------------------------------------
    def sector_of(self, name, state=None):
        state = state or self.diagnostics()
        return self.sector(state.index(name), len(state.names))

    def choose(self, name):
        """Click the entry with this display name."""
        return self.click(self.sector_of(name))

    def select(self, name):
        """Select the entry with this display name with the pointer, through the center."""
        target = self.sector_of(name)
        self.pointer(self.center_point())
        return self.pointer(target)

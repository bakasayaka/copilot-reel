"""Exercise the radial UI, Copilot events, preference persistence and placement.

0.3.4 evidence: it assumes the five-item hub with Preferred agent, the center-entry gate and the
384 DIP window, drives the real pointer, and fails against 0.3.5 and later. Current checks run
through tools/test.ps1 and tests/run-reel-seam.py.

Run only after a completed build and with no other Copilot Reel instance running.
Every launch target is the harmless probe, all data lives in a fresh QA directory,
and the original cursor position is restored. Do not interact with the desktop
while this test runs. Hardware Copilot key delivery still needs a separate check.
"""
import argparse
import ctypes
from ctypes import wintypes as w
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import traceback
import uuid


class MonitorInfo(ctypes.Structure):
    _fields_ = [("cbSize", w.DWORD), ("rcMonitor", w.RECT),
                ("rcWork", w.RECT), ("dwFlags", w.DWORD)]


class MouseInput(ctypes.Structure):
    _fields_ = [("dx", w.LONG), ("dy", w.LONG), ("mouseData", w.DWORD),
                ("dwFlags", w.DWORD), ("time", w.DWORD), ("dwExtraInfo", w.WPARAM)]


class KeyboardInput(ctypes.Structure):
    _fields_ = [("wVk", w.WORD), ("wScan", w.WORD), ("dwFlags", w.DWORD),
                ("time", w.DWORD), ("dwExtraInfo", w.WPARAM)]


class InputPayload(ctypes.Union):
    _fields_ = [("mouse", MouseInput), ("keyboard", KeyboardInput)]


class Input(ctypes.Structure):
    _fields_ = [("type", w.DWORD), ("payload", InputPayload)]


ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', type=Path, default=ROOT / 'build/Release/CopilotReel.exe')
parser.add_argument('--probe', type=Path, default=ROOT / 'build/Release/launch_probe.exe')
parser.add_argument('--output', type=Path, help='New, empty QA directory under artifacts/qa.')
parser.add_argument('--native-picker-only', action='store_true', help='Run only unresolved website and native picker regression checks.')
parser.add_argument('--follow-up-only', action='store_true',
                    help='Run only animation interruption and missing-preset editor regression checks.')
args = parser.parse_args()
exe, probe = args.exe.resolve(), args.probe.resolve()
assert exe.is_file() and probe.is_file(), 'Build the app and launch probe before running this test.'
qa_root = (ROOT / 'artifacts/qa').resolve()
data = (args.output or qa_root / ('radial-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])).resolve()
assert data.is_relative_to(qa_root) and data != qa_root, 'Test output must be a child of artifacts/qa.'
assert not data.exists(), 'Use a fresh QA directory; previous evidence is preserved.'
data.mkdir(parents=True)
marker, log = data / 'launch-probe.txt', data / 'run.log'
env = os.environ.copy()
env['COPILOT_REEL_DATA_DIR'] = str(data)
flags = 0x08000000

u = ctypes.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.GetCursorPos.argtypes = [ctypes.POINTER(w.POINT)]
u.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u.MonitorFromPoint.argtypes = [w.POINT, w.DWORD]
u.MonitorFromPoint.restype = w.HANDLE
u.GetMonitorInfoW.argtypes = [w.HANDLE, ctypes.POINTER(MonitorInfo)]
u.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
u.IsWindowVisible.argtypes = [w.HWND]
u.IsWindowEnabled.argtypes = [w.HWND]
u.IsWindowEnabled.restype = w.BOOL
u.GetDpiForWindow.argtypes = [w.HWND]
u.GetDpiForWindow.restype = w.UINT
u.GetForegroundWindow.restype = w.HWND
u.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
u.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
u.GetDlgItem.restype = w.HWND
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
u.SendInput.argtypes = [w.UINT, ctypes.POINTER(Input), ctypes.c_int]
u.SendInput.restype = w.UINT
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.PostMessageW.restype = w.BOOL
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
u.GetDlgCtrlID.argtypes = [w.HWND]
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = w.LPARAM
ChildCallback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumChildWindows.argtypes = [w.HWND, ChildCallback, w.LPARAM]

assert not u.FindWindowW('CopilotReel.Controller', 'Copilot Reel'), 'Another controller is resident; exit it before testing.'
original_pointer = w.POINT()
assert u.GetCursorPos(ctypes.byref(original_pointer))
monitor = MonitorInfo(cbSize=ctypes.sizeof(MonitorInfo))
assert u.GetMonitorInfoW(u.MonitorFromPoint(original_pointer, 2), ctypes.byref(monitor))
work = monitor.rcWork
anchor = ((work.left + work.right) // 2, (work.top + work.bottom) // 2)


def encoded(value):
    return value.replace('\\', '\\\\').replace('\t', '\\t')


config = '; isolated radial controller test\r\n[CopilotReel]\r\nVersion=1\r\nCount=3\r\n'
for index in range(3):
    config += (f'[App{index}]\r\nName=Probe {index}\r\nKind=shell\r\nTarget={encoded(str(probe))}\r\n'
               f'Arguments=--radial-probe={index}\r\nWorkingDirectory=\r\n')
(data / 'apps.ini').write_text(config, encoding='utf-16', newline='')

results, placements, draw_samples, interruption_samples, hover_samples = [], [], [], [], []
process = None
hwnd = None
opening_pattern = re.compile(
    r'(Hold started|Preview opened); pointer=(-?\d+),(-?\d+); window=(-?\d+),(-?\d+),(-?\d+),(-?\d+); awaitingCenter=(true|false)')


def wait(predicate, description, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.025)
    raise AssertionError('Timed out waiting for ' + description)


def read_log():
    return log.read_text(encoding='utf-8') if log.exists() else ''


def count():
    return marker.read_text(encoding='utf-8').count('launched') if marker.exists() else 0


def command(argument, *additional):
    subprocess.run([str(exe), argument, *additional], env=env, creationflags=flags, timeout=10, check=True)


def diagnostics():
    path = data / 'diagnostics.txt'
    if path.exists():
        path.unlink()
    command('--diagnostics')
    wait(lambda: path.exists() and path.stat().st_size > 0, 'fresh diagnostics')
    text = path.read_text(encoding='utf-8')
    assert 'last error=' not in text, 'Renderer recorded an error: ' + text
    values = dict(re.findall(r'^([^=\r\n]+)=([^\r\n]*)', text, re.MULTILINE))
    return text, values


def page(number, title=None):
    text, values = diagnostics()
    assert int(values['menuPage']) == number, text
    assert values['gestureActive'] == '1', text
    if title is not None:
        assert '; menu=' + title + ';' in text, text
    return values


def rect():
    bounds = w.RECT()
    assert u.GetWindowRect(hwnd, ctypes.byref(bounds))
    return bounds.left, bounds.top, bounds.right, bounds.bottom


def center():
    left, top, right, bottom = rect()
    return (left + right) / 2, (top + bottom) / 2


def scale():
    left, _, right, _ = rect()
    return (right - left) / 384


def cursor():
    point = w.POINT()
    assert u.GetCursorPos(ctypes.byref(point))
    return point.x, point.y


def send_events(events):
    array = (Input * len(events))(*events)
    assert u.SendInput(len(events), array, ctypes.sizeof(Input)) == len(events), ctypes.get_last_error()


def move(point):
    assert u.SetCursorPos(round(point[0]), round(point[1])), ctypes.get_last_error()
    # The zero-distance native movement delivers Raw Input without changing the
    # cursor anchor; SetCursorPos alone does not guarantee a raw mouse event.
    send_events([Input(type=0, payload=InputPayload(mouse=MouseInput(dwFlags=0x0001)))])
    time.sleep(.07)


def click(point):
    move(point)
    send_events([Input(type=0, payload=InputPayload(mouse=MouseInput(dwFlags=flag)))
                 for flag in (0x0002, 0x0004)])
    time.sleep(.12)


def key(virtual_key, expected_window=None):
    target = expected_window or hwnd
    wait(lambda: u.GetForegroundWindow() == target, 'the intended app window to own keyboard focus')
    send_events([Input(type=1, payload=InputPayload(keyboard=KeyboardInput(wVk=virtual_key, dwFlags=flag)))
                 for flag in (0, 2)])
    time.sleep(.12)


def sector(index, entries, radius=112):
    x, y = center()
    angle = index * 2 * math.pi / entries
    return x + math.sin(angle) * radius * scale(), y - math.cos(angle) * radius * scale()


def choose(index, entries):
    click(sector(index, entries))


def hidden():
    wait(lambda: not u.IsWindowVisible(hwnd), 'completion of the closing animation')


def start(argument='--background'):
    global process, hwnd
    assert not u.FindWindowW('CopilotReel.Controller', 'Copilot Reel')
    offset = len(read_log())
    process = subprocess.Popen([str(exe), argument], env=env, creationflags=flags)
    hwnd = wait(lambda: u.FindWindowW('CopilotReel.Controller', 'Copilot Reel'), 'controller creation')
    wait(lambda: 'Ready;' in read_log()[offset:], 'controller initialization')
    process_id = w.DWORD()
    u.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
    assert process_id.value == process.pid, 'The controller must belong to this isolated test process.'
    return offset


def stop():
    global process, hwnd
    command('--quit')
    assert process.wait(timeout=8) == 0
    wait(lambda: not u.FindWindowW('CopilotReel.Controller', 'Copilot Reel'), 'controller shutdown')
    process = None
    hwnd = None


def verify_placement(offset, point, held):
    match = wait(lambda: opening_pattern.search(read_log()[offset:]), 'pointer placement evidence')
    wait(lambda: u.IsWindowVisible(hwnd), 'visible radial UI')
    values = tuple(map(int, match.groups()[1:7]))
    pointer_x, pointer_y, left, top, right, bottom = values
    assert (pointer_x, pointer_y) == point, 'The invocation must preserve its captured mouse position.'
    assert cursor() == point, 'Opening must not warp the pointer.'
    assert (left, top, right, bottom) == rect()
    assert work.left <= left < right <= work.right and work.top <= top < bottom <= work.bottom
    assert right - left == bottom - top, 'The complete circular canvas must keep its aspect ratio.'
    x, y = center()
    distance = math.hypot(x - point[0], y - point[1])
    size = right - left
    expected_left = max(work.left, min(point[0] - size // 2, work.right - size))
    expected_top = max(work.top, min(point[1] - size // 2, work.bottom - size))
    assert (left, top) == (expected_left, expected_top), 'Only boundary interference may displace the wheel.'
    if (expected_left, expected_top) == (point[0] - size // 2, point[1] - size // 2):
        assert abs(x - point[0]) <= .5 and abs(y - point[1]) <= .5, 'An unconstrained invocation must leave the pointer at the wheel center.'
    expected_guard = held and distance > 44 * scale()
    assert match.group(8) == ('true' if expected_guard else 'false')
    placements.append({'pointer': point, 'bounds': [left, top, right, bottom],
                       'centerDisplacementPixels': round(distance, 2), 'awaitingCenter': expected_guard,
                       'held': held, 'dpi': u.GetDpiForWindow(hwnd)})
    time.sleep(.20)


def open_at(point=anchor, argument='--hold-start'):
    move(point)
    offset = len(read_log())
    command(argument)
    verify_placement(offset, point, argument == '--hold-start')


def assert_launch(expected_count, target):
    wait(lambda: count() >= expected_count, 'harmless launch probe')
    time.sleep(.08)
    assert count() == expected_count, 'One action must dispatch exactly one launch.'
    launches = re.findall(r'Launching target: ([^\r\n]+)', read_log())
    assert launches[-1] == target, launches
    argument_file = data / 'launch-probe-args.txt'
    if argument_file.exists():
        arguments = argument_file.read_text(encoding='utf-8').splitlines()
        assert arguments[-1] == '--radial-probe=' + target.split()[-1], arguments


def preference_language():
    path = data / 'preferences.ini'
    return path.read_text(encoding='utf-16') if path.exists() else ''


def center_label(expected, source):
    text, values = diagnostics()
    assert 'centerLabel' in values, 'Fresh diagnostics must expose the actual center label.'
    assert values['centerLabel'] == expected, (source, expected, values['centerLabel'], text)
    hover_samples.append({'source': source, 'label': values['centerLabel']})


def hub_hover_labels(language='en-US', keyboard=False):
    before_count = count()
    labels = (['Language', 'Appearance', 'Preferred agent', 'Manage apps', 'Add app']
              if language == 'en-US' else ['语言', '外观', '首选应用', '管理应用', '添加应用'])
    for index, label in enumerate(labels):
        move(sector(index, 5))
        center_label(label, f'{language} hub pointer sector {index}')
    x, y = center()
    for direction in (-1, 1):
        move((x + direction * 48 * scale(), y - 112 * scale()))
        center_label(labels[0], f'{language} language arrow {direction}')
    move(center())
    center_label('', f'{language} idle hub')
    if keyboard:
        for index, label in enumerate(labels):
            key(0x09)
            center_label(label, f'{language} hub keyboard sector {index}')
        key(0x09)
        center_label(labels[0], f'{language} keyboard wrap')
        move(center())
        center_label('', f'{language} idle after keyboard navigation')
    assert count() == before_count, 'Hovering or selecting with Tab must never launch an app.'
    results.append(f'{language} hub hover names, language arrows, and idle center are correct'
                   + (' with keyboard selection and wrap' if keyboard else ''))


def stable_draws(state):
    before, _ = diagnostics()
    time.sleep(.35)
    after, _ = diagnostics()
    first = int(re.search(r'surface draws=(\d+)', before).group(1))
    last = int(re.search(r'surface draws=(\d+)', after).group(1))
    assert first == last, f'The {state} reel must not redraw unchanged surfaces.'
    draw_samples.append({'state': state, 'surfaceDrawsBefore': first,
                         'surfaceDrawsAfter': last, 'intervalSeconds': .35})


def animation_interruptions():
    before_count = count()
    move(anchor)
    offset = len(read_log())
    began = time.perf_counter()
    assert u.PostMessageW(hwnd, 0x8001, 1, 0)
    wait(lambda: 'Hold started;' in read_log()[offset:] and u.IsWindowVisible(hwnd), 'quick opening')
    stop_after = time.perf_counter() - began
    assert stop_after < .167, 'The test must release during the 167 ms opening animation.'
    assert u.PostMessageW(hwnd, 0x8001, 2, 0)
    hidden()
    assert count() == before_count, 'Releasing during opening must cancel without launching.'
    interruption_samples.append({'scenario': 'release during opening', 'releaseAfterMilliseconds': round(stop_after * 1000, 2)})

    move(anchor)
    offset = len(read_log())
    assert u.PostMessageW(hwnd, 0x8001, 1, 0)
    wait(lambda: 'Hold started;' in read_log()[offset:] and u.IsWindowVisible(hwnd), 'opening before interruption')
    cancel_offset = len(read_log())
    cancelled = time.perf_counter()
    # WM_CLOSE cancels in-process; a --cancel process launch alone outlasts the 83 ms exit.
    assert u.PostMessageW(hwnd, 0x0010, 0, 0)
    wait(lambda: 'Canceled' in read_log()[cancel_offset:], 'beginning of exit animation')
    reopen_offset = len(read_log())
    reopen_after = time.perf_counter() - cancelled
    assert reopen_after < .083, 'The test must reopen before the old 83 ms exit finishes.'
    assert u.PostMessageW(hwnd, 0x8001, 1, 0)
    wait(lambda: 'Hold started;' in read_log()[reopen_offset:], 'replacement gesture')
    time.sleep(.27)
    assert u.IsWindowVisible(hwnd), 'An old closing timer must not hide the replacement gesture.'
    assert page(0, 'Copilot Reel')['preview'] == '0'
    assert count() == before_count, 'Cancel/reopen must not launch an agent.'
    assert u.PostMessageW(hwnd, 0x8001, 2, 0)
    hidden()
    assert count() == before_count
    interruption_samples.append({'scenario': 'cancel and reopen before exit completes',
                                 'reopenAfterMilliseconds': round(reopen_after * 1000, 2),
                                 'visibleAfterMilliseconds': 270})
    results.append('release during opening and cancel/reopen before exit preserve the new gesture without launching')


def missing_preset_editor():
    # Discovery currently resolves Claude Science only by an exact Start Menu
    # shortcut name. If installed later, this environment-specific case is skipped.
    shortcut_roots = [Path(os.environ['APPDATA']) / 'Microsoft/Windows/Start Menu/Programs',
                      Path(os.environ['ProgramData']) / 'Microsoft/Windows/Start Menu/Programs']
    for root in shortcut_roots:
        if root.exists() and any(path.stem.casefold() in ('claude science', 'claude for science')
                                 for path in root.rglob('*.lnk')):
            return False
    before_count = count()
    original_apps = (data / 'apps.ini').read_bytes()
    open_at(argument='--preview')
    choose(3, 4)
    page(3, 'Settings')
    choose(4, 5)
    page(2, 'Add an agent')
    choose(2, 7)
    editor = wait(lambda: u.FindWindowW('CopilotReel.Osd', None), 'missing-preset focused editor')
    name, target = ctypes.create_unicode_buffer(256), ctypes.create_unicode_buffer(256)
    u.GetWindowTextW(u.GetDlgItem(editor, 112), name, len(name))
    u.GetWindowTextW(u.GetDlgItem(editor, 113), target, len(target))
    assert name.value == 'Claude Science', 'The missing preset must prefill its official display name.'
    assert target.value == '', 'An unresolved preset must ask for a real target, not invent a launch URI.'
    assert u.PostMessageW(editor, 0x0010, 0, 0)
    wait(lambda: not u.FindWindowW('CopilotReel.Osd', None), 'missing-preset editor cancellation')
    page(1, 'Your apps')
    assert (data / 'apps.ini').read_bytes() == original_apps, 'Cancelling a preset must leave the app list unchanged.'
    assert count() == before_count, 'Selecting an unavailable preset must not launch another app.'
    key(0x1b)
    page(3, 'Settings')
    key(0x1b)
    page(0, 'Copilot Reel')
    key(0x1b)
    hidden()
    results.append('missing Claude Science preset opens a prefilled focused editor and cancels without changing or launching apps')
    return True


def native_picker_short_tap():
    # An explicit website is permitted in the wheel but must never become a
    # browser fallback for the preferred short-tap action.
    before_count = count()
    website_config = ('[CopilotReel]\r\nVersion=1\r\nCount=1\r\n[App0]\r\n'
                      'Name=Unresolved web agent\r\nKind=shell\r\n'
                      'Target=https://example.invalid/agent\r\nArguments=\r\nWorkingDirectory=\r\n')
    (data / 'apps.ini').write_text(website_config, encoding='utf-16', newline='')
    before_apps = (data / 'apps.ini').read_bytes()
    offset = start()
    command('--tap')
    picker = wait(lambda: u.FindWindowW('#32770', 'Choose a local application'), 'native local-application picker')
    picker_process = w.DWORD()
    u.GetWindowThreadProcessId(picker, ctypes.byref(picker_process))
    assert picker_process.value == process.pid, 'Only the fixture-owned app picker may be cancelled.'
    assert not u.IsWindowVisible(hwnd), 'A rejected website short tap must not open the wheel.'
    assert u.PostMessageW(picker, 0x0010, 0, 0)
    wait(lambda: not u.FindWindowW('#32770', 'Choose a local application'), 'native picker cancellation')
    time.sleep(.1)
    assert count() == before_count, 'A website short tap or picker cancellation must not launch any target.'
    assert 'Launching target:' not in read_log()[offset:], 'The website must never reach Shell activation.'
    assert (data / 'apps.ini').read_bytes() == before_apps, 'Cancelling native selection must preserve the website profile.'
    assert diagnostics()[1]['editorOpen'] == '0', 'Cancelling must release the modal-editor guard.'

    command('--tap')
    picker = wait(lambda: u.FindWindowW('#32770', 'Choose a local application'), 'second local-application picker')
    picker_process = w.DWORD()
    u.GetWindowThreadProcessId(picker, ctypes.byref(picker_process))
    assert picker_process.value == process.pid
    filename_text = ctypes.create_unicode_buffer(str(probe))
    filename_attempts = []
    observed_controls = []

    def fill_native_filename():
        nonlocal picker
        current_picker = u.FindWindowW('#32770', 'Choose a local application')
        if not current_picker:
            return False
        current_process = w.DWORD()
        u.GetWindowThreadProcessId(current_picker, ctypes.byref(current_process))
        assert current_process.value == process.pid, 'The live filename dialog must remain owned by this fixture.'
        picker = current_picker
        filename_controls = []

        @ChildCallback
        def filename_control(child, unused):
            class_name = ctypes.create_unicode_buffer(128)
            u.GetClassNameW(child, class_name, len(class_name))
            observed_controls.append({'class': class_name.value, 'id': u.GetDlgCtrlID(child)})
            if class_name.value == 'Edit' and u.GetDlgCtrlID(child) == 1148:
                filename_controls.append(child)
            return True

        u.EnumChildWindows(picker, filename_control, 0)
        if len(filename_controls) != 1:
            filename_attempts.append({'matches': len(filename_controls)})
            return False
        filename = filename_controls[0]
        if not u.IsWindowVisible(picker) or not u.IsWindowVisible(filename) or not u.IsWindowEnabled(filename):
            return False
        set_result = u.SendMessageW(filename, 0x000c, 0, ctypes.addressof(filename_text))
        # GetWindowText cannot read another process's edit contents. WM_GETTEXT
        # marshals the requested native text instead and proves the field is ready.
        actual = ctypes.create_unicode_buffer(32768)
        get_result = u.SendMessageW(filename, 0x000d, len(actual), ctypes.addressof(actual))
        filename_attempts.append({'set': set_result, 'get': get_result, 'text': actual.value})
        return actual.value == str(probe)

    # The dialog HWND is published before the common dialog finishes filling
    # its initial directory. Wait for visible controls before entering a path.
    wait(lambda: u.IsWindowVisible(picker), 'visible native application picker')
    time.sleep(.25)
    try:
        wait(fill_native_filename, 'ready native filename control containing the probe path')
    finally:
        (data / 'picker-filename.json').write_text(json.dumps({'attempts': filename_attempts[-3:], 'controls': observed_controls[-80:]}), encoding='utf-8')
    assert u.GetDlgItem(picker, 1), 'The native Open action must be present.'
    if u.GetForegroundWindow() == picker:
        key(0x0d, expected_window=picker)
    else:
        # An unrelated desktop window can retain foreground ownership. Invoke
        # this verified dialog's own Open command rather than typing elsewhere.
        assert u.PostMessageW(picker, 0x0111, 1, 0)
    wait(lambda: not u.FindWindowW('#32770', 'Choose a local application'), 'native picker acceptance')
    wait(lambda: count() == before_count + 1, 'launching the selected harmless native probe')
    time.sleep(.1)
    assert count() == before_count + 1, 'Accepting one local executable must dispatch exactly once.'
    saved_profile = (data / 'apps.ini').read_text(encoding='utf-16')
    assert 'https://example.invalid/agent' not in saved_profile and encoded(str(probe)) in saved_profile
    assert 'Name=Unresolved web agent' in saved_profile, 'Choosing a local target retains the user display name.'
    backups = list(data.glob('apps.ini.before-local-apps.*.bak'))
    assert backups and any(path.read_bytes() == before_apps for path in backups), 'Target replacement must preserve the original profile backup.'
    assert re.findall(r'Launching target: ([^\r\n]+)', read_log())[-1] == 'Unresolved web agent'
    assert (data / 'launch-probe-args.txt').read_text(encoding='utf-8').splitlines()[-1] == ''
    command('--tap')
    wait(lambda: count() == before_count + 2, 'subsequent direct native short tap')
    time.sleep(.1)
    assert count() == before_count + 2 and not u.FindWindowW('#32770', 'Choose a local application')
    results.append('native picker acceptance saves the real executable, preserves its backup, launches once, and enables subsequent direct short tap')
    stop()
    results.append('an unresolved website short tap opens only the native application picker; cancellation preserves data and never launches a browser')


class FollowUpComplete(Exception):
    pass


failure = None
try:
    if args.native_picker_only:
        native_picker_short_tap()
        raise FollowUpComplete()
    start()
    assert 'Configuration validation failed' not in read_log()
    assert not u.IsWindowVisible(hwnd)
    animation_interruptions()
    missing_preset_editor()
    if args.follow_up_only:
        stop()
        raise FollowUpComplete()
    command('--tap')
    assert_launch(1, 'Probe 0')
    assert not u.IsWindowVisible(hwnd), 'A short tap must launch directly without displaying the reel.'
    results.append('warm short tap launches the default first agent directly')

    open_at()
    command('--hold-stop')
    hidden()
    assert count() == 1, 'Releasing without leaving the pointer-centered cancel zone must not launch.'
    results.append('interior hold opens directly on the pointer and an unmoved release cancels')

    open_at()
    move(sector(2, 4))
    assert count() == 1, 'Long hold selection must not launch before release.'
    starts_before = read_log().count('Hold started;')
    command('--hold-start')
    assert read_log().count('Hold started;') == starts_before, 'Duplicate hold-start must not reset the active gesture.'
    command('--hold-stop')
    hidden()
    assert_launch(2, 'Probe 2')
    command('--hold-stop')
    time.sleep(.1)
    assert count() == 2, 'Duplicate release must not launch again.'
    results.append('interior hold selects directly from the pointer center and launches once on release')

    move(anchor)
    offset = len(read_log())
    assert u.PostMessageW(hwnd, 0x8001, 1, 0)
    verify_placement(offset, anchor, True)
    move(center())
    move(sector(0, 4))
    assert u.PostMessageW(hwnd, 0x8001, 2, 0)
    hidden()
    assert_launch(3, 'Probe 0')
    assert u.PostMessageW(hwnd, 0x8001, 2, 0)
    time.sleep(.1)
    assert count() == 3
    assert u.PostMessageW(hwnd, 0x8001, 0, 0)
    assert_launch(4, 'Probe 0')
    results.append('registered fast-path Tap/Down/Up message contract dispatches exactly once')

    edge_points = [(work.left + 2, anchor[1]), (work.right - 3, anchor[1]),
                   (anchor[0], work.top + 2), (anchor[0], work.bottom - 3),
                   (work.left + 2, work.top + 2), (work.right - 3, work.top + 2),
                   (work.left + 2, work.bottom - 3), (work.right - 3, work.bottom - 3)]
    for point in edge_points:
        open_at(point)
        move(sector(0, 4))
        command('--hold-stop')
        hidden()
        assert count() == 4
    results.append('eight screen edges and corners retain the full circle; crossing a sector before center entry still cancels')

    size = rect()[2] - rect()[0]
    interior_point = (round(work.left + size / 2 + (work.right - work.left - size) * .25),
                      round(work.top + size / 2 + (work.bottom - work.top - size) * .75))
    open_at(interior_point)
    assert center() == tuple(float(value) for value in interior_point), 'An off-center desktop position must remain the actual wheel center.'
    command('--hold-stop')
    hidden()
    assert count() == 4
    results.append('a distinct interior desktop point remains the exact wheel center without a guard or cursor warp')

    open_at(argument='--preview')
    page(0, 'Copilot Reel')
    stable_draws('visible and stationary')
    move(sector(0, 4))
    center_label('Probe 0', 'main app hover')
    move(sector(3, 4))
    center_label('Apps and settings', 'main combined entry hover')
    move(center())
    center_label('', 'idle main')
    choose(3, 4)
    assert page(3, 'Settings')['menuEntries'] == '5'
    assert not u.FindWindowW('CopilotReel.Osd', None), 'The combined hub must remain in the reel.'
    hub_hover_labels(keyboard=True)
    move(sector(0, 5))
    key(ord('D'))
    wait(lambda: 'Language=zh-CN' in preference_language(), 'Chinese language preference')
    page(3, '\u8bbe\u7f6e')
    hub_hover_labels('zh-CN')
    key(ord('A'))
    wait(lambda: 'Language=en-US' in preference_language(), 'English language preference')
    page(3, 'Settings')
    x, y = center()
    click((x + 48 * scale(), y - 112 * scale()))
    wait(lambda: 'Language=zh-CN' in preference_language(), 'right language arrow')
    click((x - 48 * scale(), y - 112 * scale()))
    wait(lambda: 'Language=en-US' in preference_language(), 'left language arrow')
    page(3, 'Settings')
    choose(0, 5)
    wait(lambda: 'Language=zh-CN' in preference_language(), 'language icon click')
    choose(0, 5)
    wait(lambda: 'Language=en-US' in preference_language(), 'second language icon click')
    results.append('one plus entry opens the combined hub; A/D, language icon, and adjacent arrows persist the language')

    choose(4, 5)
    page(2, 'Add an agent')
    click(center())
    page(3, 'Settings')
    results.append('Add from the combined hub returns to that hub through the center back control')

    choose(2, 5)
    page(4, 'Preferred agent')
    choose(1, 3)
    state = page(3, 'Settings')
    assert state['preferredIndex'] == '1'
    click(center())
    page(0, 'Copilot Reel')
    key(0x1b)
    hidden()
    command('--tap')
    assert_launch(5, 'Probe 1')
    results.append('radial preferred-agent selection changes short-tap behavior; center and Escape navigate back')

    open_at(argument='--preview')
    choose(3, 4)
    page(3, 'Settings')
    choose(3, 5)
    page(1, 'Your apps')
    choose(0, 4)
    page(5, 'Probe 0')
    choose(0, 3)
    editor = wait(lambda: u.FindWindowW('CopilotReel.Osd', None), 'focused app editor')
    assert not u.IsWindowVisible(hwnd), 'Only the focused editor should remain visible while editing text.'
    name = u.GetDlgItem(editor, 112)
    text = ctypes.create_unicode_buffer(256)
    u.GetWindowTextW(name, text, len(text))
    assert text.value == 'Probe 0', 'Existing target must be loaded directly into the editor.'
    assert u.PostMessageW(editor, 0x0010, 0, 0)
    wait(lambda: not u.FindWindowW('CopilotReel.Osd', None), 'focused editor cancellation')
    wait(lambda: u.IsWindowVisible(hwnd), 'return to the management reel')
    page(1, 'Your apps')
    assert count() == 5
    results.append('Manage to entry actions to focused editor; cancelling returns to the management reel')

    choose(3, 4)
    page(2, 'Add an agent')
    click(center())
    page(1, 'Your apps')
    choose(3, 4)
    first = page(2, 'Add an agent')
    assert first['presetOffset'] == '0'
    assert int(first['menuEntries']) == 7
    choose(6, 7)
    second = page(2, 'Add an agent')
    assert second['presetOffset'] == '5'
    # After the preset entries, Custom and Previous precede any Next action.
    second_count = int(second['menuEntries'])
    previous_index = 6 if second_count == 8 else second_count - 1
    choose(previous_index, second_count)
    first = page(2, 'Add an agent')
    assert first['presetOffset'] == '0'
    choose(5, 7)
    editor = wait(lambda: u.FindWindowW('CopilotReel.Osd', None), 'custom app editor')
    text = ctypes.create_unicode_buffer(256)
    u.GetWindowTextW(u.GetDlgItem(editor, 112), text, len(text))
    assert text.value == '', 'Custom app addition must start with an empty name.'
    assert u.PostMessageW(editor, 0x0010, 0, 0)
    wait(lambda: not u.FindWindowW('CopilotReel.Osd', None), 'custom editor cancellation')
    page(1, 'Your apps')
    key(0x1b)
    page(3, 'Settings')
    key(0x1b)
    page(0, 'Copilot Reel')
    key(0x1b)
    hidden()
    results.append('preset Next/Previous pagination and Custom return retain the radial navigation hierarchy')

    stable_draws('hidden')
    stop()
    results.append('hidden renderer performs no repeated surface drawing and shuts down cleanly')

    move(anchor)
    start('--tap')
    assert_launch(6, 'Probe 1')
    assert not u.IsWindowVisible(hwnd)
    assert diagnostics()[1]['preferredIndex'] == '1'
    stop()
    results.append('cold short tap restores the preferred launch profile without displaying the reel')

    move(anchor)
    offset = start('--hold-start')
    verify_placement(offset, anchor, True)
    move(center())
    move(sector(0, 4))
    command('--hold-stop')
    hidden()
    assert_launch(7, 'Probe 0')
    command('--copilot-uri', 'copilot-reel://?state=Tap')
    assert_launch(8, 'Probe 1')
    stop()
    results.append('cold long hold opens at its captured pointer; protocol Tap uses the persisted preferred agent')

    move(anchor)
    offset = start('--hold-stop')
    assert not u.IsWindowVisible(hwnd)
    command('--hold-stop')
    time.sleep(.1)
    assert count() == 8
    stop()
    results.append('orphan cold and duplicate release events do not open UI or launch an agent')

    native_picker_short_tap()
    assert 'Window error:' not in read_log() and 'Unexpected window error' not in read_log()
except FollowUpComplete:
    pass
except Exception:
    failure = traceback.format_exc()
finally:
    try:
        if process is not None and process.poll() is None:
            picker = u.FindWindowW('#32770', 'Choose a local application')
            if picker:
                picker_process = w.DWORD()
                u.GetWindowThreadProcessId(picker, ctypes.byref(picker_process))
                if picker_process.value == process.pid:
                    u.PostMessageW(picker, 0x0010, 0, 0)
                    time.sleep(.1)
            editor = u.FindWindowW('CopilotReel.Osd', None)
            if editor:
                u.PostMessageW(editor, 0x0010, 0, 0)
                time.sleep(.1)
            command('--quit')
            process.wait(timeout=8)
    except Exception:
        if process is not None and process.poll() is None:
            # Only terminate the process created and owned by this fixture.
            process.terminate()
            process.wait(timeout=5)
    u.SetCursorPos(original_pointer.x, original_pointer.y)
    report = {'passed': results, 'placements': placements, 'drawSamples': draw_samples,
              'interruptionSamples': interruption_samples, 'hoverSamples': hover_samples, 'error': failure,
              'binary': str(exe), 'evidenceDirectory': str(data),
              'note': 'Hardware Copilot delivery, mixed-refresh monitors and physical high-refresh pacing require separate validation.'}
    (data / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))
if failure:
    raise SystemExit(1)

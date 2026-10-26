"""Capture only the application's own HWND using Windows Graphics Capture.

0.3.4 evidence: it assumes the five-item hub with Preferred agent and the 384 DIP window and
drives the real pointer. tools/capture-seam.py and seam group `hub` cover the languages now.

Run exclusively, with no resident Copilot Reel instance. The fixture uses
one legacy-named application profile in both languages and never launches an app. It moves the
pointer, changes only the isolated UI language, and restores pointer/focus.
"""
import ctypes
from ctypes import wintypes as w
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import hashlib
import uuid
from PIL import Image


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
EXE = ROOT / 'build/Release/CopilotReel.exe'
CAPTURE = ROOT / 'build/Release/capture-window.exe'
DATA = ROOT / ('artifacts/qa/language-invariance-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
OUTPUT = ROOT / 'output/runtime/v0.3.4'
DATA.mkdir(parents=True, exist_ok=True)
OUTPUT.mkdir(parents=True, exist_ok=True)
u = ctypes.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.IsWindowVisible.argtypes = [w.HWND]
u.GetCursorPos.argtypes = [ctypes.POINTER(w.POINT)]
u.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
u.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
u.GetForegroundWindow.restype = w.HWND
u.SetForegroundWindow.argtypes = [w.HWND]
u.SendInput.argtypes = [w.UINT, ctypes.POINTER(Input), ctypes.c_int]
u.SendInput.restype = w.UINT
u.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
u.GetDlgItem.restype = w.HWND
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = w.LPARAM
assert not u.FindWindowW('CopilotReel.Controller', 'Copilot Reel'), 'Close the resident application before this isolated fixture.'
assert CAPTURE.exists(), 'Run tools/build-capture-tool.ps1 first.'
original_pointer = w.POINT()
u.GetCursorPos(ctypes.byref(original_pointer))
original_foreground = u.GetForegroundWindow()
profiles = [('ChatGPT', 'aumid', 'OpenAI.Codex_2p2nqsd0c76g0!App'),
            ('Claude', 'aumid', 'Claude_pzs8sxrjxfjjc!Claude'),
            ('Gemini', 'shell', 'https://gemini.google.com'),
            ('Perplexity', 'shell', 'https://www.perplexity.ai'),
            ('记事本', 'shell', 'notepad.exe'), ('Windows 设置', 'shell', 'ms-settings:')]
content = '[CopilotReel]\r\nVersion=1\r\nCount=6\r\n'
for index, (name, kind, target) in enumerate(profiles):
    content += f'[App{index}]\r\nName={name}\r\nKind={kind}\r\nTarget={target}\r\nArguments=\r\nWorkingDirectory=\r\n'
(DATA / 'apps.ini').write_text(content, encoding='utf-16', newline='')
(DATA / 'preferences.ini').write_text('[General]\r\nLanguage=en-US\r\n', encoding='utf-16', newline='')
env = os.environ.copy()
env['COPILOT_REEL_DATA_DIR'] = str(DATA)
process = None
hwnd = None


def command(value):
    subprocess.run([str(EXE), value], env=env, creationflags=0x08000000, check=True, timeout=8)


def wait(predicate):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.025)
    raise AssertionError('The native application did not reach the expected state.')


def send(events):
    array = (Input * len(events))(*events)
    assert u.SendInput(len(events), array, ctypes.sizeof(Input)) == len(events), ctypes.get_last_error()


def move(x, y):
    assert u.SetCursorPos(round(x), round(y))
    send([Input(type=0, payload=InputPayload(mouse=MouseInput(dwFlags=0x0001)))])
    time.sleep(.08)


def geometry():
    bounds = w.RECT()
    assert u.GetWindowRect(hwnd, ctypes.byref(bounds))
    return (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2, (bounds.right - bounds.left) / 384


def hover(index, count):
    x, y, scale = geometry()
    angle = index * 2 * math.pi / count
    move(x + math.sin(angle) * 112 * scale, y - math.cos(angle) * 112 * scale)


def clear_hover():
    x, y, _ = geometry()
    move(x, y)


def key(code):
    wait(lambda: u.GetForegroundWindow() == hwnd)
    send([Input(type=1, payload=InputPayload(keyboard=KeyboardInput(wVk=code, dwFlags=flag)))
          for flag in (0, 2)])
    time.sleep(.12)


def capture(name, expected_label):
    diagnostics_path = DATA / 'diagnostics.txt'
    if diagnostics_path.exists():
        diagnostics_path.unlink()
    command('--diagnostics')
    wait(lambda: diagnostics_path.exists() and diagnostics_path.stat().st_size > 0)
    diagnostics = diagnostics_path.read_text(encoding='utf-8')
    assert 'last error=' not in diagnostics, diagnostics
    values = dict(re.findall(r'^([^=\r\n]+)=([^\r\n]*)', diagnostics, re.MULTILINE))
    assert values.get('centerLabel') == expected_label, (name, expected_label, values.get('centerLabel'))
    subprocess.run([str(CAPTURE), str(hwnd), str(OUTPUT / f'{name}.png')],
                   creationflags=0x08000000, check=True, timeout=10)
    assert Path(values['dataPath']) == DATA / 'apps.ini'
    evidence['fixtures'].append({'name': name, 'centerLabel': expected_label, 'diagnostics': diagnostics})
    return values



def choose(index, count):
    hover(index, count)
    send([Input(type=0, payload=InputPayload(mouse=MouseInput(dwFlags=flag))) for flag in (2, 4)])
    time.sleep(.12)


def assert_unchanged():
    assert (DATA / 'apps.ini').read_bytes() == original_apps, 'Switching language cannot rename, reorder, or replace applications.'
    assert (DATA / 'behavior.ini').read_bytes() == original_preference, 'Switching language cannot change the preferred profile.'


def same_pixels(first, second, excluded=None):
    a, b = Image.open(OUTPUT / first).convert('RGBA'), Image.open(OUTPUT / second).convert('RGBA')
    assert a.size == b.size
    differences = 0
    for y in range(a.height):
        for x in range(a.width):
            if excluded and excluded[0] <= x < excluded[2] and excluded[1] <= y < excluded[3]:
                continue
            if a.getpixel((x,y)) != b.getpixel((x,y)):
                differences += 1
    return differences


try:
    anchor = (u.GetSystemMetrics(0) // 2, u.GetSystemMetrics(1) // 2)
    u.SetCursorPos(*anchor)
    process = subprocess.Popen([str(EXE), '--background'], env=env, creationflags=0x08000000)
    hwnd = wait(lambda: u.FindWindowW('CopilotReel.Controller', 'Copilot Reel'))
    owner = w.DWORD()
    u.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
    assert owner.value == process.pid
    evidence = {'captureMethod': 'Windows.Graphics.Capture.CreateForWindow', 'fixtures': []}
    command('--settings')
    wait(lambda: u.IsWindowVisible(hwnd))
    time.sleep(.25)
    choose(2, 5)
    choose(4, 6)
    original_apps = (DATA / 'apps.ini').read_bytes()
    original_preference = (DATA / 'behavior.ini').read_bytes()
    scale = geometry()[2]
    for phase, chinese in [('en', False), ('zh', True), ('en-return', False)]:
        if phase != 'en':
            key(ord('D'))
        clear_hover()
        state = capture('hub-' + phase, '')
        assert state['language'] == ('zh-CN' if chinese else 'en-US')
        assert state['preferredIndex'] == '4'
        assert_unchanged()
        command('--cancel')
        wait(lambda: not u.IsWindowVisible(hwnd))
        move(*anchor)
        command('--preview')
        wait(lambda: u.IsWindowVisible(hwnd))
        time.sleep(.25)
        clear_hover()
        capture('main-' + phase, '')
        hover(4, 7)
        capture('notepad-' + phase, '记事本' if chinese else 'Notepad')
        hover(5, 7)
        capture('windows-settings-' + phase, 'Windows 设置' if chinese else 'Windows Settings')
        choose(6, 7)
        choose(3, 5)
        choose(4, 7)
        choose(0, 3)
        editor = wait(lambda: u.FindWindowW('CopilotReel.Osd', None))
        edit = u.GetDlgItem(editor, 112)
        value = ctypes.create_unicode_buffer(256)
        u.SendMessageW(edit, 0x000d, len(value), ctypes.addressof(value))
        assert value.value == ('记事本' if chinese else 'Notepad'), 'The focused editor must localize built-in labels without modifying their saved name.'
        u.PostMessageW(editor, 0x0010, 0, 0)
        wait(lambda: not u.FindWindowW('CopilotReel.Osd', None))
        wait(lambda: u.IsWindowVisible(hwnd))
        key(0x1b)
        assert_unchanged()
    command('--quit')
    assert process.wait(timeout=8) == 0
    language_tile = tuple(round(v * scale) for v in (170, 58, 214, 102))
    comparisons = {
        'main_en_zh_different_pixels': same_pixels('main-en.png', 'main-zh.png'),
        'main_en_return_different_pixels': same_pixels('main-en.png', 'main-en-return.png'),
        'hub_outside_language_tile_different_pixels': same_pixels('hub-en.png', 'hub-zh.png', language_tile),
        'hub_en_return_different_pixels': same_pixels('hub-en.png', 'hub-en-return.png')}
    assert all(value == 0 for value in comparisons.values()), comparisons
    assert same_pixels('hub-en.png', 'hub-zh.png') > 0, 'Only the EN/ZH monogram should change on the idle hub.'
    evidence.update({'passed': True, 'comparisons': comparisons,
        'applicationConfigSha256': hashlib.sha256(original_apps).hexdigest(),
        'preferredConfigSha256': hashlib.sha256(original_preference).hexdigest(),
        'dataPath': str(DATA / 'apps.ini'), 'note': 'One profile and one native process; EN to ZH to EN with no application launches.'})
    (DATA / 'results.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'comparisons': comparisons, 'evidenceDirectory': str(DATA)}))
finally:
    if process is not None and process.poll() is None:
        editor = u.FindWindowW('CopilotReel.Osd', None)
        if editor:
            u.PostMessageW(editor, 0x0010, 0, 0)
            time.sleep(.1)
        command('--quit')
        process.wait(timeout=8)
    u.SetCursorPos(original_pointer.x, original_pointer.y)
    if original_foreground:
        u.SetForegroundWindow(original_foreground)

"""Capture only the application's own HWND using Windows Graphics Capture.

Evidence from before 0.3.4: it assumes the five-item hub with Preferred agent and the 384 DIP
window and drives the real pointer. tools/capture-seam.py takes the current stills.

Run exclusively, with no resident Copilot Reel instance. The fixture mirrors the
six default launch profiles but never clicks or launches them. It moves the
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
DATA = ROOT / 'artifacts/qa/visual-v0.3.2'
OUTPUT = ROOT / 'output/runtime/v0.3.2'
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
assert not u.FindWindowW('CopilotReel.Controller', 'Copilot Reel'), 'Close the resident application before this isolated fixture.'
assert CAPTURE.exists(), 'Run tools/build-capture-tool.ps1 first.'
original_pointer = w.POINT()
u.GetCursorPos(ctypes.byref(original_pointer))
original_foreground = u.GetForegroundWindow()
profiles = [('ChatGPT', 'https://chatgpt.com'), ('Claude', 'https://claude.ai'),
            ('Gemini', 'https://gemini.google.com'), ('Perplexity', 'https://www.perplexity.ai'),
            ('Notepad', 'notepad.exe'), ('Windows Settings', 'ms-settings:')]
content = '[CopilotReel]\r\nVersion=1\r\nCount=6\r\n'
for index, (name, target) in enumerate(profiles):
    content += f'[App{index}]\r\nName={name}\r\nKind=shell\r\nTarget={target}\r\nArguments=\r\nWorkingDirectory=\r\n'
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
    evidence['fixtures'].append({'name': name, 'centerLabel': expected_label, 'diagnostics': diagnostics})


try:
    u.SetCursorPos(u.GetSystemMetrics(0) // 2, u.GetSystemMetrics(1) // 2)
    process = subprocess.Popen([str(EXE), '--background'], env=env, creationflags=0x08000000)
    hwnd = wait(lambda: u.FindWindowW('CopilotReel.Controller', 'Copilot Reel'))
    process_id = w.DWORD()
    u.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
    assert process_id.value == process.pid, 'Only this fixture-owned application may be captured.'
    evidence = {'captureMethod': 'Windows.Graphics.Capture.CreateForWindow', 'hwnd': hwnd,
                'application': str(EXE), 'profiles': profiles, 'fixtures': []}
    command('--preview')
    wait(lambda: u.IsWindowVisible(hwnd))
    time.sleep(.25)
    clear_hover()
    capture('reel', '')
    hover(5, 7)
    capture('windows-settings', 'Windows Settings')
    hover(4, 7)
    capture('notepad', 'Notepad')
    command('--cancel')
    wait(lambda: not u.IsWindowVisible(hwnd))
    command('--settings')
    wait(lambda: u.IsWindowVisible(hwnd))
    time.sleep(.25)
    clear_hover()
    capture('settings', '')
    for index, (name, label) in enumerate([('language', 'Language'), ('appearance', 'Appearance'),
                                         ('preferred', 'Preferred agent'), ('manage', 'Manage apps'), ('add', 'Add app')]):
        hover(index, 5)
        capture('settings-' + name, label)
    key(ord('D'))
    hover(0, 5)
    capture('settings-language-zh', '语言')
    key(ord('A'))
    command('--cancel')
    wait(lambda: not u.IsWindowVisible(hwnd))
    command('--quit')
    assert process.wait(timeout=8) == 0
    (DATA / 'capture-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(OUTPUT)
finally:
    if process is not None and process.poll() is None:
        command('--quit')
        process.wait(timeout=8)
    u.SetCursorPos(original_pointer.x, original_pointer.y)
    if original_foreground:
        u.SetForegroundWindow(original_foreground)

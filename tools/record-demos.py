"""Record the README's demo GIFs and stills through the interaction test seam.

Every demo runs an isolated, unpackaged test instance with a demo profile: official marks of
well-known apps, no personal apps, paths or settings, and launches that are logged instead of
opened (COPILOT_REEL_TEST_LAUNCH=log). Windows Graphics Capture records only the reel's own
window while the seam moves the synthetic pointer and sends the Copilot key, so the desktop,
the taskbar and other windows never appear. Nothing moves the real pointer or takes focus.

Each README language gets its own recordings, made with the reel in that language. A GIF is
composed over GitHub's dark (#0d1117) or light (#ffffff) page, sampled at 25 frames a second,
with a drawn pointer and a Copilot keycap that lights while the key is held. The reel's
opening is the first captured frame replayed through the opening motion of
design/vi/tokens.json, because capture can only start once the window is visible. Stills keep
the window's own alpha, so the reel and its shadow sit on either page.

    uv run --python 3.14 --with pillow python tools/record-demos.py [--output docs/readme]
        [--demos hold,hub,drag,about,stills] [--themes dark,light] [--languages en,zh,ja]
"""
import argparse
import bisect
import csv
import ctypes
import json
import shutil
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import reel_seam as seam  # noqa: E402

EXE = ROOT / 'build/Release/CopilotReel.exe'
CAPTURE = ROOT / 'build/Release/capture-window.exe'
PAGES = {'dark': (13, 17, 23), 'light': (255, 255, 255)}
LOCALES = {'en': 'en-US', 'zh': 'zh-CN', 'ja': 'ja-JP'}
# Well-known apps whose official marks the reel ships. Visual Studio Code is recognized by its
# install path, which does not need to exist because nothing is launched.
PROFILE = [('ChatGPT', 'https://chatgpt.com', ''), ('Claude', 'https://claude.ai', ''),
           ('Gemini', 'https://gemini.google.com', ''), ('Perplexity', 'https://www.perplexity.ai', ''),
           ('Visual Studio Code', r'C:\Program Files\Microsoft VS Code\Code.exe', ''),
           ('Notepad', 'notepad.exe', ''), ('Windows Settings', 'ms-settings:', '')]
NOTEPAD = {'en': 'Notepad', 'zh': '记事本', 'ja': 'メモ帳'}
FONTS = Path(r'C:\Windows\Fonts')
STEP = 400_000          # 40 ms in 100 ns ticks: 25 frames a second, a whole GIF delay
KEY_BAND, KEY_GAP = 64, 24
ACCENT = (96, 205, 255)

_kernel32 = ctypes.windll.kernel32
_frequency = ctypes.c_int64()
_kernel32.QueryPerformanceFrequency(ctypes.byref(_frequency))


def now():
    """QPC in 100 ns ticks: the clock of Windows Graphics Capture's SystemRelativeTime."""
    counter = ctypes.c_int64()
    _kernel32.QueryPerformanceCounter(ctypes.byref(counter))
    return counter.value * 10_000_000 // _frequency.value


class Timeline:
    """What the fixture did and when, on the capture's clock."""

    def __init__(self):
        self.pointer = []   # (ticks, x, y) in screen pixels
        self.key = []       # (ticks, held)
        self.clicks = []    # ticks
        self.hidden = None  # ticks at which the reel had closed

    def at_pointer(self, ticks):
        if not self.pointer:
            return None
        i = bisect.bisect_right([t for t, _, _ in self.pointer], ticks) - 1
        return self.pointer[max(i, 0)][1:]

    def held(self, ticks):
        state = False
        for t, held in self.key:
            if t <= ticks:
                state = held
        return state


class Demo:
    def __init__(self, name, theme, language, work, extra=None):
        self.name, self.theme, self.language = name, theme, language
        self.data = work / f'{name}-{theme}-{language}-{uuid.uuid4().hex[:6]}'
        self.data.mkdir(parents=True)
        seam.write_apps(self.data / 'apps.ini', PROFILE)
        (self.data / 'preferences.ini').write_text(f'[General]\r\nLanguage={LOCALES[language]}\r\n', encoding='utf-16',
                                                   newline='')
        work_area = seam.primary_work_area()
        self.anchor = ((work_area[0] + work_area[2]) // 2, (work_area[1] + work_area[3]) // 2)
        environment = {'COPILOT_REEL_TEST_LAUNCH': 'log'}
        environment.update(extra or {})
        self.reel = seam.Reel(EXE, self.data, 'demo-' + uuid.uuid4().hex[:10], pointer=self.anchor,
                              visual=f'theme={theme};material=solid;dpi=192', extra=environment)
        self.timeline = Timeline()
        self.capture = None
        self.frames = self.data / 'frames'

    # Fixture actions, each recorded on the timeline ------------------------------------------
    def move(self, point, steps=1, seconds=0.0):
        start = self.timeline.pointer[-1][1:] if self.timeline.pointer else point
        for i in range(1, steps + 1):
            ease = 1 - (1 - i / steps) ** 3
            p = (round(start[0] + (point[0] - start[0]) * ease), round(start[1] + (point[1] - start[1]) * ease))
            self.reel.pointer(p)
            self.timeline.pointer.append((now(), *p))
            if seconds:
                time.sleep(seconds / steps)

    def click(self, point, steps=10, seconds=.35):
        self.move(point, steps, seconds)
        time.sleep(.15)
        self.timeline.clicks.append(now())
        self.reel.click(point)

    def press(self, point):
        self.timeline.clicks.append(now())
        self.reel.press(point)

    def key(self, held):
        self.timeline.key.append((now(), held))
        assert self.reel.send_command(seam.HOLD_START if held else seam.HOLD_STOP, self.anchor)

    def close(self):
        """Close the whole menu at once."""
        assert self.reel.send_command(seam.CANCEL)
        self.reel.hidden()
        self.timeline.hidden = now()

    def record(self, milliseconds):
        """Start the capture on the visible, settled reel; returns once it is recording."""
        self.reel.sync()
        time.sleep(.45)
        self.capture = subprocess.Popen([str(CAPTURE), str(self.reel.hwnd), str(self.frames), '--sequence', str(milliseconds)],
                                        creationflags=seam.CREATE_NO_WINDOW, stdout=subprocess.DEVNULL)
        seam.wait(lambda: (self.frames / 'ready.txt').exists(), 'the capture', timeout=10)
        self.rect = self.reel.rect()

    def finish(self):
        assert self.capture.wait(timeout=30) == 0, 'The capture failed.'
        self.end = now()

    # Composition ------------------------------------------------------------------------------
    def compose(self, intro_ms=400, outro_ms=700, opening=True, keycap=True, finale=None):
        rows = list(csv.DictReader((self.frames / 'frames.csv').open()))
        frames = [(int(r['systemTicks']), Image.open(self.frames / f'frame-{r["frame"]}.png').convert('RGBA')) for r in rows]
        times = [t for t, _ in frames]
        size = frames[0][1].size
        page = PAGES[self.theme]
        width, height = size[0], size[1] + (KEY_BAND + KEY_GAP if keycap else 0)
        left, top = self.rect[0], self.rect[1]

        def canvas(reel_image, ticks, alpha=1.0, scale=1.0):
            im = Image.new('RGBA', (width, height), page + (255,))
            if reel_image is not None:
                r = reel_image
                if scale != 1.0:
                    r = r.resize((round(r.width * scale), round(r.height * scale)), Image.LANCZOS)
                if alpha < 1.0:
                    r = r.copy()
                    r.putalpha(r.getchannel('A').point(lambda a: round(a * alpha)))
                im.alpha_composite(r, ((width - r.width) // 2, (size[1] - r.height) // 2))
            if keycap:
                draw_keycap(im, self.theme, self.timeline.held(ticks))
            point = self.timeline.at_pointer(ticks)
            if point:
                pressed = any(0 <= ticks - c < 1_800_000 for c in self.timeline.clicks)
                draw_pointer(im, point[0] - left, point[1] - top, pressed)
            return im.convert('RGB')

        out = []
        start = times[0]
        if intro_ms:
            out.append((canvas(None, start - 10_000 * (intro_ms + 300)), intro_ms))
        if opening:
            # tokens.json motion: the opening fades in over 83 ms and grows from 0.96 over 167 ms, decelerating.
            fade, grow = 83, 167
            for i in range(1, 5):
                t = i * grow / 4
                ease = cubic_bezier(t / grow, 0, 0, 0, 1)
                out.append((canvas(frames[0][1], start - round(10_000 * (grow - t)), alpha=min(1.0, t / fade),
                                   scale=.96 + .04 * ease), 40))
        ticks = start
        hidden = self.timeline.hidden or self.end
        while ticks <= min(self.end, hidden + STEP):
            i = max(0, bisect.bisect_right(times, ticks) - 1)
            out.append((canvas(None if ticks >= hidden else frames[i][1], ticks), 40))
            ticks += STEP
        if finale:
            out.extend(finale(canvas, ticks))
        out.append((canvas(None, ticks + 10_000 * 400), outro_ms))
        return out

    def still(self, path):
        """The visible reel alone, with its alpha."""
        self.reel.sync()
        time.sleep(.6)
        assert subprocess.run([str(CAPTURE), str(self.reel.hwnd), str(path)], creationflags=seam.CREATE_NO_WINDOW,
                              stdout=subprocess.DEVNULL).returncode == 0, 'The still capture failed.'


def cubic_bezier(t, x1, y1, x2, y2):
    """y for x = t on a CSS cubic Bezier from (0, 0) to (1, 1)."""
    lo, hi = 0.0, 1.0
    for _ in range(40):
        mid = (lo + hi) / 2
        x = 3 * (1 - mid) ** 2 * mid * x1 + 3 * (1 - mid) * mid ** 2 * x2 + mid ** 3
        lo, hi = (mid, hi) if x < t else (lo, mid)
    s = (lo + hi) / 2
    return 3 * (1 - s) ** 2 * s * y1 + 3 * (1 - s) * s ** 2 * y2 + s ** 3


def draw_pointer(im, x, y, pressed):
    """A Windows arrow pointer with its tip at (x, y), and a ring while a click lands."""
    d = ImageDraw.Draw(im)
    if pressed:
        d.ellipse((x - 18, y - 18, x + 18, y + 18), outline=ACCENT + (255,), width=3)
    s = 1.6
    shape = [(0, 0), (0, 17), (4.5, 13), (7.5, 20), (10.5, 18.8), (7.5, 12), (13, 12)]
    d.polygon([(x + px * s, y + py * s) for px, py in shape], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=2)


def draw_keycap(im, theme, held):
    w, h = 176, 52
    x, y = (im.width - w) // 2, im.height - h - KEY_GAP // 2 - 6
    d = ImageDraw.Draw(im)
    dark = theme == 'dark'
    face = ACCENT if held else ((48, 54, 61) if dark else (234, 238, 242))
    edge = ACCENT if held else ((72, 79, 88) if dark else (208, 215, 222))
    ink = (0, 0, 0) if held else ((230, 237, 243) if dark else (31, 35, 40))
    shift = 3 if held else 0
    if not held:
        d.rounded_rectangle((x, y + 4, x + w, y + h + 4), radius=12, fill=(1, 4, 9) if dark else (208, 215, 222))
    d.rounded_rectangle((x, y + shift, x + w, y + h + shift), radius=12, fill=face, outline=edge, width=2)
    font = ImageFont.truetype(str(FONTS / 'seguisb.ttf'), 22)
    d.text((x + w / 2, y + h / 2 + shift), 'Copilot', font=font, fill=ink, anchor='mm')


def save_gif(frames, path, page):
    # One palette for every frame keeps flat areas from flickering between frames. Median cut
    # averages the page color with its neighbors, so the exact page color is added, or the GIF
    # would sit in a faintly different box on GitHub's page.
    picks = frames[::max(1, len(frames) // 6)][:6]
    sheet = Image.new('RGB', (frames[0][0].width, frames[0][0].height * len(picks)))
    for i, (image, _) in enumerate(picks):
        sheet.paste(image, (0, i * image.height))
    colors = sheet.quantize(colors=254, method=Image.Quantize.MEDIANCUT).getpalette()[:254 * 3]
    palette = Image.new('P', (1, 1))
    palette.putpalette(colors + [0, 0, 0] * (254 - len(colors) // 3) + list(page) + [0, 0, 0])
    images = []
    for image, _ in frames:
        indexed = image.quantize(palette=palette, dither=Image.Dither.NONE)
        # Pillow's palette lookup is approximate, so the page color goes back on exactly where the frame has it.
        exact = [channel.point(lambda v, want=want: 255 if v == want else 0) for channel, want in zip(image.split(), page)]
        indexed.paste(254, mask=ImageChops.multiply(ImageChops.multiply(exact[0], exact[1]), exact[2]))
        images.append(indexed)
    images[0].save(path, save_all=True, append_images=images[1:], duration=[d for _, d in frames], loop=0,
                   disposal=1, optimize=True)


# Demos ------------------------------------------------------------------------------------
def open_preview(d):
    d.reel.start()
    d.move(d.anchor)
    assert d.reel.send_command(seam.PREVIEW, d.anchor)
    seam.wait(d.reel.visible, 'the preview')


def open_page(d, hub_index):
    """From the main menu, + and then a hub entry by its slot."""
    state = d.reel.diagnostics()
    d.reel.click(d.reel.sector(len(state.names) - 1, len(state.names)))
    time.sleep(.5)
    hub = d.reel.diagnostics()
    d.reel.click(d.reel.sector(hub_index, len(hub.names)))
    time.sleep(.6)


def demo_hold(d):
    """Hold the Copilot key, point at Claude, let go: the reel commits and Claude opens."""
    d.reel.start()
    d.move(d.anchor)
    d.key(True)
    d.record(3600)
    state = d.reel.diagnostics()
    target = d.reel.sector(state.index('Claude'), len(state.names))
    time.sleep(.35)
    d.move(target, steps=14, seconds=.5)
    time.sleep(.8)
    d.key(False)
    d.reel.hidden()
    d.timeline.hidden = now()
    d.finish()
    assert 'Test launch logged: Claude' in d.reel.read_log(), 'The hold must commit Claude.'
    mark = Image.open(ROOT / 'assets/brands/claude.png').convert('RGBA')

    def finale(canvas, ticks):
        out = []
        for i in range(1, 9):
            t = i / 8
            size = round(112 + 32 * cubic_bezier(t, 0, 0, 0, 1))
            frame = canvas(None, ticks)
            icon = mark.resize((size, size), Image.LANCZOS)
            alpha = icon.getchannel('A').point(lambda a: round(a * min(1.0, t * 1.6)))
            frame.paste(icon, ((frame.width - size) // 2, (frame.height - KEY_BAND - KEY_GAP - size) // 2), alpha)
            out.append((frame, 40))
        out.append((out[-1][0], 1200))
        return out
    return d.compose(finale=finale)


def demo_hub(d):
    """The + hub: the language steps EN, ZH, JA in place, and every name follows."""
    open_preview(d)
    d.record(7500)
    state = d.reel.diagnostics()
    d.click(d.reel.sector(len(state.names) - 1, len(state.names)))
    time.sleep(.7)
    hub = d.reel.diagnostics()
    d.move(d.reel.sector(0, len(hub.names)), steps=10, seconds=.35)
    time.sleep(.6)
    later = d.reel.canvas(*seam.LATER_CHEVRON_DIP)
    for _ in range(3):
        d.click(later, steps=8, seconds=.25)
        time.sleep(.9)
    d.move(d.reel.center_point(), steps=8, seconds=.3)
    time.sleep(.4)
    d.close()
    d.finish()
    # It starts on the open main menu, so its first frame, which shows wherever a GIF does not play, is the reel.
    return d.compose(keycap=False, opening=False, intro_ms=0)


def demo_drag(d):
    """Manage apps: drag a tile to another slot, then drop one on the trash in the center."""
    open_preview(d)
    open_page(d, 4)  # Manage apps
    d.record(8000)
    names = d.reel.diagnostics().names
    count = len(names)
    gemini = names.index('Gemini')
    start = d.reel.sector(gemini, count)
    d.move(start, steps=10, seconds=.4)
    time.sleep(.25)
    d.press(start)
    for k in range(1, 13):
        d.move(d.reel.sector(gemini + k / 6, count))
        time.sleep(.04)
    time.sleep(.35)
    d.reel.release_button(d.reel.sector(gemini + 2, count))
    time.sleep(1.0)
    names = d.reel.diagnostics().names
    notepad = d.reel.sector(names.index(NOTEPAD[d.language]), count)
    d.move(notepad, steps=10, seconds=.4)
    time.sleep(.25)
    d.press(notepad)
    center = d.reel.center_point()
    d.move(center, steps=14, seconds=.55)
    time.sleep(.6)
    d.reel.release_button(center)
    time.sleep(1.1)
    d.close()
    d.finish()
    assert d.reel.parked(), 'The trash must take Notepad out of the reel.'
    return d.compose(keycap=False, opening=False, intro_ms=0)


def demo_about(d):
    """About: a switch turns over in place, then a check finds an update and offers it."""
    open_preview(d)
    open_page(d, 6)  # About
    d.record(8500)
    count = len(d.reel.diagnostics().names)
    d.move(d.reel.sector(0, count), steps=10, seconds=.4)
    time.sleep(1.0)
    d.click(d.reel.sector(3, count))   # Auto-install on
    time.sleep(.9)
    d.click(d.reel.sector(3, count))   # and off again
    time.sleep(.7)
    d.click(d.reel.sector(4, count))   # Check for updates
    seam.wait(lambda: d.reel.diagnostics()['updateStage'] == 'available', 'the offer', timeout=20)
    d.move(d.reel.sector(4, count), steps=3, seconds=.1)
    time.sleep(1.6)
    d.close()
    d.finish()
    return d.compose(keycap=False, opening=False, intro_ms=0)


def stills(d, output, suffix):
    """The README's first two steps, with the pointer drawn in: the reel just opened, and Claude selected."""
    d.reel.start()
    d.move(d.anchor)
    d.key(True)
    state = d.reel.diagnostics()
    for name, point in (('open', d.anchor), ('point', d.reel.sector(state.index('Claude'), len(state.names), radius=96))):
        d.move(point)
        path = output / f'step-{name}-{suffix}.png'
        d.still(path)
        left, top, _, _ = d.reel.rect()
        image = Image.open(path).convert('RGBA')
        draw_pointer(image, point[0] - left, point[1] - top, False)
        image.save(path, optimize=True)
    d.move(d.anchor)
    assert d.reel.send_command(seam.CANCEL)
    d.reel.hidden()


DEMOS = {'hold': demo_hold, 'hub': demo_hub, 'drag': demo_drag, 'about': demo_about}


class Feed:
    """A releases feed on 127.0.0.1 offering one version above the demo's 0.3.8; nothing is downloaded."""

    def __init__(self):
        import http.server
        body = json.dumps([{'tag_name': 'v0.3.9', 'draft': False, 'prerelease': False, 'assets': [
            {'name': 'CopilotReel_0.3.9.0_x64.msix', 'size': 1, 'digest': 'sha256:' + '0' * 64,
             'browser_download_url': 'http://127.0.0.1:9/none.msix'}]}]).encode()

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = f'http://127.0.0.1:{self.server.server_address[1]}/releases'
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/readme')
    parser.add_argument('--demos', default=','.join(list(DEMOS) + ['stills']))
    parser.add_argument('--themes', default='dark,light')
    parser.add_argument('--languages', default=','.join(LOCALES))
    parser.add_argument('--keep', action='store_true', help='Keep the raw frames and logs under artifacts/qa.')
    args = parser.parse_args()
    seam.assert_seam_binary(EXE)
    assert CAPTURE.is_file(), 'Build tools/build-capture-tool.ps1 first.'
    args.output.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'artifacts/qa' / ('demos-' + time.strftime('%Y%m%d-%H%M%S'))
    feed = Feed()
    extra = {'COPILOT_REEL_TEST_UPDATE_FEED': feed.url, 'COPILOT_REEL_TEST_VERSION': '0.3.8.0'}
    try:
        for language in args.languages.split(','):
            for theme in args.themes.split(','):
                for name in args.demos.split(','):
                    demo = Demo(name, theme, language, work, extra)
                    try:
                        if name == 'stills':
                            stills(demo, args.output, f'{theme}-{language}')
                            print(f'stills {theme}-{language}', flush=True)
                            continue
                        frames = DEMOS[name](demo)
                    finally:
                        demo.reel.shutdown()
                    path = args.output / f'demo-{name}-{theme}-{language}.gif'
                    save_gif(frames, path, PAGES[theme])
                    print(f'{path.name}: {len(frames)} frames, {path.stat().st_size // 1024} KiB', flush=True)
    finally:
        feed.close()
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()

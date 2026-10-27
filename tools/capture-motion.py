"""Record the reel's motion strips through the test seam: opening and selection, drilling into the hub
and back, and a commit, as Windows Graphics Capture frame sequences of the test instance's window.

    uv run --python 3.14 --with pillow python tools/capture-motion.py [--output output/runtime/v<version>]

Like tools/capture-seam.py, it drives a separate unpackaged test instance through its own channel, with
the same profiles and its harmless launch probe: it never moves the pointer, sends input or takes the
foreground, and the installed reel keeps running. Each strip is a sheet in the layout of the 0.3.4
strips (tools/capture-v034.py), which design/vi/source/pages.py (strip_frame) crops: 8 columns of 240 px
frames, each above a 24 px time row, below a 40 px header naming the events. Consecutive identical
frames are dropped, so a strip shows change rather than idle time; times are capture timestamps from the
start of the sequence. Requires tools/build.ps1 and tools/build-capture-tool.ps1.

Without --output the strips go to the run's evidence directory under artifacts/qa.
"""
import argparse
import csv
import importlib.util
import json
import subprocess
import sys
import time
import uuid
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import reel_seam as seam  # noqa: E402

# tools/capture-seam.py's profiles, slots and capture tool, so the strips show the stills' reel.
_spec = importlib.util.spec_from_file_location('capture_seam', ROOT / 'tools' / 'capture-seam.py')
stills = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(stills)
COLUMNS, CELL, TIME_ROW, HEADER, KEEP = 8, 240, 24, 40, 48
BACKGROUND = (32, 32, 32)   # dark, solid, as the 0.3.4 strips


def sequence(reel, directory, name, duration, actions):
    """Records `duration` ms of frames while `actions` (offset seconds, label, callable) run once capture is ready."""
    frames = directory / ('frames-' + name)
    capture = subprocess.Popen([str(stills.CAPTURE), str(reel.hwnd), str(frames), '--sequence', str(duration)],
                               creationflags=seam.CREATE_NO_WINDOW)
    seam.wait(lambda: (frames / 'ready.txt').exists(), 'capture readiness')
    began = time.perf_counter()
    events = []
    for offset, label, action in actions:
        delay = offset - (time.perf_counter() - began)
        if delay > 0:
            time.sleep(delay)
        events.append({'event': label, 'elapsedMs': round((time.perf_counter() - began) * 1000, 1)})
        action()
    assert capture.wait(timeout=duration / 1000 + 15) == 0, 'capture-window.exe failed'
    with open(frames / 'frames.csv', newline='') as handle:
        rows = list(csv.DictReader(handle))
    return frames, rows, events


def sheet(output, name, frames, rows, events):
    """The strip: changed frames only, at most 48, in rows of eight under a header naming the events."""
    images = [(Image.open(frames / f'frame-{row["frame"]}.png').convert('RGBA'), float(row['captureMilliseconds'])) for row in rows]
    kept = []
    for image, stamp in images:
        if not kept or image.tobytes() != kept[-1][0].tobytes():
            kept.append((image, stamp))
    kept = kept[:KEEP]
    count = (len(kept) + COLUMNS - 1) // COLUMNS
    canvas = Image.new('RGB', (COLUMNS * CELL, count * (CELL + TIME_ROW) + HEADER), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 15)
    ink = (230, 230, 230)
    draw.text((8, 10), name + '  ·  ' + '  ·  '.join(f'{e["event"]} @{e["elapsedMs"]:.0f} ms' for e in events), fill=ink, font=font)
    for index, (image, stamp) in enumerate(kept):
        x, y = (index % COLUMNS) * CELL, HEADER + (index // COLUMNS) * (CELL + TIME_ROW)
        thumb = image.resize((CELL - 8, CELL - 8), Image.LANCZOS)
        canvas.paste(thumb, (x + 4, y), thumb)
        draw.text((x + 8, y + CELL - 6), f'{stamp:.1f} ms', fill=ink, font=font)
    path = output / f'motion-{name}.png'
    canvas.save(path)
    return path, [round(stamp, 1) for _, stamp in kept]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, help='where the strips go; default the evidence directory')
    args = parser.parse_args()
    assert stills.EXE.exists() and stills.PROBE.exists() and stills.CAPTURE.exists(), \
        'Run tools/build.ps1 and tools/build-capture-tool.ps1 first.'
    evidence = ROOT / 'artifacts' / 'qa' / f'motion-{time.strftime("%Y%m%d-%H%M%S")}-{uuid.uuid4().hex[:6]}'
    evidence.mkdir(parents=True)
    output = args.output.resolve() if args.output else evidence
    output.mkdir(parents=True, exist_ok=True)
    data = evidence / 'data'
    data.mkdir()
    stills.write_profiles(data, {})
    work = seam.primary_work_area()
    anchor = ((work[0] + work[2]) // 2, (work[1] + work[3]) // 2)
    reel = seam.Reel(stills.EXE, data, run_id='motion-' + uuid.uuid4().hex[:12], pointer=anchor,
                     visual='theme=dark;material=solid', extra=stills.UPDATER)
    strips = []
    try:
        reel.start()
        reel.send_command(seam.PREVIEW, anchor)
        stills.wait_visible(reel, True)
        reel.sync()
        time.sleep(stills.SETTLE_SECONDS)
        claude, gemini, probe, plus = (stills.slot(reel, i, 8) for i in (1, 2, 6, 7))

        # Close, then open, then the seat appears on Claude and travels to Gemini.
        frames, rows, events = sequence(reel, evidence, 'open-select', 1400, [
            (.05, 'cancel', lambda: reel.send_command(seam.CANCEL)),
            (.30, 'open', lambda: reel.send_command(seam.PREVIEW, anchor)),
            (.75, 'hover Claude', lambda: reel.pointer(claude)),
            (1.05, 'hover Gemini', lambda: reel.pointer(gemini))])
        strips.append(('open-select', frames, rows, events))
        # Drill into the hub from the +, the tile handing off to the back chevron, then Esc back out.
        frames, rows, events = sequence(reel, evidence, 'drill', 1300, [
            (.05, 'hover +', lambda: reel.pointer(plus)),
            (.30, 'click + (drill in)', lambda: reel.click(plus)),
            (.85, 'Esc (drill out)', lambda: reel.key(seam.VK_ESCAPE))])
        strips.append(('drill', frames, rows, events))
        # Commit on the probe: the chosen tile holds while the rest leaves, and the probe launches once.
        before = reel.launches()
        frames, rows, events = sequence(reel, evidence, 'commit', 700, [
            (.05, 'hover Probe', lambda: reel.pointer(probe)),
            (.30, 'click Probe (commit)', lambda: reel.click(probe))])
        strips.append(('commit', frames, rows, events))
        seam.wait(lambda: reel.launches() == before + 1, 'the probe launch')
        reel.stop()
    finally:
        reel.shutdown()
    record = []
    for name, frames, rows, events in strips:
        path, stamps = sheet(output, name, frames, rows, events)
        record.append({'name': name, 'events': events, 'frames': len(rows), 'kept': len(stamps), 'stamps': stamps,
                       'sheet': str(path)})
        print(f'{path}: {len(stamps)} of {len(rows)} frames')
    (evidence / 'motion.json').write_text(json.dumps({'captureMethod': 'Windows.Graphics.Capture.CreateForWindow, --sequence',
                                                       'variant': 'theme=dark;material=solid', 'strips': record}, indent=2),
                                          encoding='utf-8')
    print(json.dumps({'evidence': str(evidence / 'motion.json'), 'output': str(output)}))


if __name__ == '__main__':
    main()

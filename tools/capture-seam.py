"""Capture the reel's still states through the interaction test seam and compare them with a reference set.

The application runs as an isolated, unpackaged test instance (tests/reel_seam.py): it never moves the
pointer, sends input, takes the foreground or touches the installed instance. Each variant captures
Main, a selected vendor tile, the hub, two hub selections and an edge-gated hold (the still set of
tools/capture-v034.py), and from 0.3.6 'Your apps', an app tile dragged over another slot and over the
armed center trash (the drag is canceled with Escape, so the order never changes), and an app's actions
page without reorder chevrons, and from 0.3.8 About with its first tile and a switch. Every image that also
exists in the reference folder is compared pixel by pixel, in every channel; a channel may differ by
--tolerance levels (default 2, because at rest some anti-aliased edges render up to two levels away), and
every changed pixel is counted either way.
0.3.6 draws the reel at 0.8 of its DIP geometry, so 0.3.4 and 0.3.5 stills are no longer comparable, and
0.3.7 adds Reel size and 0.3.8 About to the hub, 0.3.9 sets text below 13 px in Segoe UI Variable Small
and widens the name line, and 0.3.10 draws the probe apps, which carry no icon, as the generic glyph: the
reference set is output/runtime/v0.3.10, published with --no-reference. Frosted stills show what lies behind
the window, so they match only while that stays the same.

Last, each variant opens Add from the hub once agent discovery has completed and captures every Add page
with the pointer on the plate center, so nothing is selected. Add lists what is installed on this machine,
so these captures go to the evidence directory, not the output folder, and have no reference. Instead
each tile is cropped at its slot and checked against the artwork identities in the diagnostics
(menuArt): tiles with different identities must differ in at least one pixel, and tiles with equal
identities must be identical, on one page and across all Add pages of the variant. Frosted tiles are translucent over an acrylic plate that shows what lies
behind the window, so on frosted variants the result is recorded but not asserted.

The run fails (exit status 1, "passed": false in results.json) when a compared image differs beyond the
tolerance or has another size, when a reference state has no reference image, when Add tiles contradict
their identities, or when a variant cannot be captured. --no-reference records every still without a
comparison, to publish a new reference set after a deliberate visual change.

    uv run --python 3.14 --with pillow python tools/capture-seam.py [--reference output/runtime/v0.3.10]
        [--output output/runtime/vX.Y.Z] [--variants dark-solid,light-solid,...] [--tolerance 2] [--no-reference]

Without --output the stills go to the run's evidence directory under artifacts/qa.
"""
import argparse
import itertools
import json
import math
import subprocess
import sys
import time
import uuid
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import reel_seam as seam  # noqa: E402

EXE = ROOT / 'build/Release/CopilotReel.exe'
PROBE = ROOT / 'build/Release/launch_probe.exe'
CAPTURE = ROOT / 'build/Release/capture-window.exe'
# The same profiles as tools/capture-v034.py, so Main is comparable with the 0.3.4 captures.
PROFILES = [('ChatGPT', 'https://chatgpt.com', ''), ('Claude', 'https://claude.ai', ''),
            ('Gemini', 'https://gemini.google.com', ''), ('Perplexity', 'https://www.perplexity.ai', ''),
            ('Notepad', 'notepad.exe', ''), ('Windows Settings', 'ms-settings:', ''),
            ('Probe', str(PROBE), '--radial-probe=capture')]
VARIANTS = [('dark-solid', 'theme=dark;material=solid'), ('light-solid', 'theme=light;material=solid'),
            ('dark-frosted', 'theme=dark;material=frosted'), ('light-frosted', 'theme=light;material=frosted'),
            ('contrast', 'contrast=on'), ('dark-100', 'theme=dark;material=solid;dpi=96'),
            ('dark-150', 'theme=dark;material=solid;dpi=144')]
# Each must have a reference image.
REFERENCE_STATES = ('main', 'main-claude', 'hub', 'hub-appearance', 'hub-language', 'manage', 'manage-drag',
                    'manage-trash', 'entry', 'entry-app', 'about', 'about-author', 'about-switch', 'gated')
# About shows its updater as an installed reel does: a feed that is never asked (nothing checks by itself
# in a test instance) and a version, so the switches and the update tile are enabled.
UPDATER = {'COPILOT_REEL_TEST_UPDATE_FEED': 'http://127.0.0.1:9/releases', 'COPILOT_REEL_TEST_VERSION': '0.3.8.0'}
# Every animation has come to rest: captured after 0.35 s, a seat that had just travelled still differed
# from its resting pixels by up to three levels, while 1 s and 3 s captures are identical.
SETTLE_SECONDS = 1.0
ADD_APP, ADD_TITLE, NEXT = 'Add app', 'Add an agent', 'Next'
# A square inside the 40 DIP tile, clear of its neighbours and of the 48 DIP seat.
TILE_CROP_DIP = 36
# The tile check looks within one device pixel of the computed slot, absorbing a rounding tie at the snap.
SHIFTS = [(dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)]


def slot(reel, index, count, radius=seam.SECTOR_RADIUS_DIP):
    bounds = seam.w.RECT()
    seam.user32.GetWindowRect(reel.hwnd, seam.ctypes.byref(bounds))
    left, top, right, bottom = bounds.left, bounds.top, bounds.right, bounds.bottom
    scale = (right - left) / seam.CANVAS_DIP
    x, y = (left + right) / 2, (top + bottom) / 2
    angle = index * 2 * math.pi / count
    return round(x + math.sin(angle) * radius * scale), round(y - math.cos(angle) * radius * scale)


def write_profiles(directory):
    content = f'[CopilotReel]\r\nVersion=1\r\nCount={len(PROFILES)}\r\n'
    for index, (title, target, arguments) in enumerate(PROFILES):
        content += (f'[App{index}]\r\nName={title}\r\nKind=shell\r\nTarget={seam.encode_ini(target)}\r\n'
                    f'Arguments={arguments}\r\nWorkingDirectory=\r\n')
    (directory / 'apps.ini').write_text(content, encoding='utf-16', newline='')
    (directory / 'preferences.ini').write_text('[General]\r\nLanguage=en-US\r\n', encoding='utf-16', newline='')


def still(reel, output, variant, name, label):
    reel.sync()
    time.sleep(SETTLE_SECONDS)
    values = reel.diagnostics()
    assert values.get('centerLabel') == label, (variant, name, label, values.get('centerLabel'))
    path = output / f'{variant}-{name}.png'
    subprocess.run([str(CAPTURE), str(reel.hwnd), str(path)], creationflags=seam.CREATE_NO_WINDOW, check=True, timeout=10)
    return name, path


def wait_visible(reel, visible):
    seam.wait(lambda: bool(seam.user32.IsWindowVisible(reel.hwnd)) == visible, 'visible' if visible else 'hidden')


def capture_add(reel, evidence, variant, opened):
    """Open Add from the hub once discovery has completed and capture every page with nothing selected.

    opened is the run.log offset from before the hub opened. Returns one record per page."""
    discovered = int(seam.wait(lambda: seam.DISCOVERY.search(reel.read_log()[opened:]), 'agent discovery',
                               timeout=30).group(1))
    reel.choose(ADD_APP)
    pages = []
    while True:
        # The plate center selects nothing, so no seat or hover name covers a tile.
        reel.pointer(reel.center_point())
        values = reel.diagnostics()
        assert values.page == seam.PAGE_PRESETS and values.title == ADD_TITLE and values.selected == -1, values.text
        assert len(values.art) == len(values.names), ('menuArt must name every entry', values.names, values.art)
        state, path = still(reel, evidence, variant, f'add-{len(pages) + 1}', '')
        after = reel.diagnostics()
        assert (after.names, after.art) == (values.names, values.art), 'The Add page changed during its capture.'
        pages.append({'state': state, 'path': path, 'width': reel.rect()[2] - reel.rect()[0],
                      'presetOffset': int(values['presetOffset']),
                      'addCatalog': int(values['addCatalog']), 'addInstalled': int(values['addInstalled']),
                      'discovered': discovered, 'names': values.names, 'art': values.art})
        if NEXT not in values.names:
            return pages
        assert len(pages) < 10, 'Add page paging must end.'
        reel.choose(NEXT)


def capture_variant(variant, override, output, data):
    directory = data / variant
    directory.mkdir(parents=True)
    write_profiles(directory)
    work = seam.primary_work_area()
    anchor = ((work[0] + work[2]) // 2, (work[1] + work[3]) // 2)
    reel = seam.Reel(EXE, directory, run_id='cap-' + uuid.uuid4().hex[:12], pointer=anchor, visual=override, extra=UPDATER)
    shots = []
    try:
        reel.start()
        reel.send_command(seam.PREVIEW, anchor)
        wait_visible(reel, True)
        shots.append(still(reel, output, variant, 'main', ''))
        reel.pointer(slot(reel, 1, 8))
        shots.append(still(reel, output, variant, 'main-claude', 'Claude'))
        reel.send_command(seam.CANCEL)
        wait_visible(reel, False)
        reel.pointer(anchor)
        reel.send_command(seam.SETTINGS, anchor)
        wait_visible(reel, True)
        shots.append(still(reel, output, variant, 'hub', ''))
        hub = reel.diagnostics().names
        reel.pointer(slot(reel, hub.index('Appearance'), len(hub)))
        shots.append(still(reel, output, variant, 'hub-appearance', 'Appearance'))
        reel.pointer(slot(reel, hub.index('Language'), len(hub)))
        shots.append(still(reel, output, variant, 'hub-language', 'Language'))
        # 0.3.6: Your apps, then Claude lifted over Perplexity's slot, then over the armed trash.
        reel.pointer(slot(reel, hub.index('Manage apps'), len(hub)))
        reel.click(slot(reel, hub.index('Manage apps'), len(hub)))
        reel.pointer(anchor)
        names = reel.diagnostics().names
        shots.append(still(reel, output, variant, 'manage', ''))
        order = seam.read_app_names(directory / 'apps.ini')
        claude, count = names.index('Claude'), len(names)
        reel.press(slot(reel, claude, count))
        reel.path([slot(reel, claude + .5, count), slot(reel, claude + 1.5, count), slot(reel, claude + 2, count)])
        assert reel.diagnostics()['dragTarget'] == f'slot:{claude + 2}', reel.diagnostics().text
        shots.append(still(reel, output, variant, 'manage-drag', ''))
        reel.pointer(reel.center_point())
        assert reel.diagnostics()['dragTarget'] == 'trash', reel.diagnostics().text
        shots.append(still(reel, output, variant, 'manage-trash', ''))
        # Escape puts the tile back, and the release that follows is not a click.
        reel.key(seam.VK_ESCAPE)
        reel.release_button(reel.center_point())
        assert seam.read_app_names(directory / 'apps.ini') == order, seam.read_app_names(directory / 'apps.ini')
        # An app's actions page: the app itself in slot 0, without reorder chevrons.
        reel.pointer(slot(reel, claude, count))
        reel.click(slot(reel, claude, count))
        reel.pointer(anchor)
        values = reel.diagnostics()
        assert values.page == seam.PAGE_ENTRY and values.get('adjust') == 'none', values.text
        shots.append(still(reel, output, variant, 'entry', ''))
        reel.pointer(slot(reel, 0, len(values.names)))
        shots.append(still(reel, output, variant, 'entry-app', 'Claude'))
        reel.send_command(seam.CANCEL)
        wait_visible(reel, False)
        # 0.3.8: About from the hub, its first tile (the reel's mark, named by the version and the
        # author) and the auto-check switch, which is on.
        reel.pointer(anchor)
        reel.send_command(seam.SETTINGS, anchor)
        wait_visible(reel, True)
        hub = reel.diagnostics().names
        reel.pointer(slot(reel, hub.index('About'), len(hub)))
        reel.click(slot(reel, hub.index('About'), len(hub)))
        reel.pointer(anchor)
        values = reel.diagnostics()
        assert values.page == seam.PAGE_ABOUT and len(values.names) == 5, values.text
        shots.append(still(reel, output, variant, 'about', ''))
        reel.pointer(slot(reel, 0, 5))
        shots.append(still(reel, output, variant, 'about-author', values.names[0]))
        reel.pointer(slot(reel, 2, 5))
        shots.append(still(reel, output, variant, 'about-switch', values.names[2]))
        reel.send_command(seam.CANCEL)
        wait_visible(reel, False)
        corner = (work[0] + 24, work[1] + 24)
        reel.pointer(corner)
        reel.send_command(seam.HOLD_START, corner)
        wait_visible(reel, True)
        shots.append(still(reel, output, variant, 'gated', ''))
        reel.send_command(seam.CANCEL)
        wait_visible(reel, False)
        # Add, machine-dependent: opening the hub starts discovery, and Add is read once it has completed.
        opened = len(reel.read_log())
        reel.pointer(anchor)
        reel.send_command(seam.SETTINGS, anchor)
        wait_visible(reel, True)
        pages = capture_add(reel, data, variant, opened)
        reel.send_command(seam.CANCEL)
        wait_visible(reel, False)
        reel.stop()
    finally:
        reel.shutdown()
    return shots, pages


def tile_box(index, count, scale, shift=(0, 0)):
    """The TILE_CROP_DIP square of slot index of count, in device pixels of a capture.

    Slot i sits at i*360/count degrees clockwise from 12 o'clock on the 112 DIP orbit. The renderer
    snaps each tile's offset to whole device pixels (SnapDip, std::round), so its center rounds the same way."""
    angle = -math.pi / 2 + 2 * math.pi * index / count
    x = math.floor((seam.CENTER_DIP + seam.SECTOR_RADIUS_DIP * math.cos(angle)) * scale + .5) + shift[0]
    y = math.floor((seam.CENTER_DIP + seam.SECTOR_RADIUS_DIP * math.sin(angle)) * scale + .5) + shift[1]
    half = round(TILE_CROP_DIP * scale / 2)
    return x - half, y - half, x + half, y + half


def check_tiles(page, asserted, seen):
    """Tile pixels against artwork identities on one Add page and against every identity seen on the
    earlier Add pages of the variant (the renderer keeps cached artwork across pages, so a shared cache
    entry can pair tiles on different pages); returns (record, violations) and adds this page to seen."""
    image = Image.open(page['path']).convert('RGBA')
    assert image.size == (page['width'], page['width']), f'The capture must be the window: {image.size} != {page["width"]}'
    scale = image.width / seam.CANVAS_DIP
    names, art = page['names'], page['art']
    crops = [{shift: image.crop(tile_box(i, len(art), scale, shift)) for shift in SHIFTS} for i in range(len(art))]
    pairs, violations = [], []
    for i, j in itertools.combinations(range(len(art)), 2):
        same = art[i] == art[j]
        counts = [differing_pixels(crops[i][(0, 0)], crops[j][shift])[0] for shift in SHIFTS]
        fewest = min(counts)
        ok = fewest == 0 if same else fewest > 0
        pairs.append({'tiles': [i, j], 'sameIdentity': same, 'differingPixels': counts[SHIFTS.index((0, 0))],
                      'fewestDifferingPixels': fewest, 'ok': ok})
        if not ok:
            violations.append(f'tiles {i} {names[i]!r} and {j} {names[j]!r} share {art[i]!r} but differ in {fewest} pixels'
                              if same else
                              f'tiles {i} {names[i]!r} ({art[i]}) and {j} {names[j]!r} ({art[j]}) have different '
                              'identities but identical pixels')
    earlier = []
    for i in range(len(art)):
        for identity, (name, state, crop) in seen.items():
            same = art[i] == identity
            fewest = min(differing_pixels(crop, crops[i][shift])[0] for shift in SHIFTS)
            ok = fewest == 0 if same else fewest > 0
            earlier.append({'tile': i, 'earlierState': state, 'earlierName': name, 'sameIdentity': same,
                            'fewestDifferingPixels': fewest, 'ok': ok})
            if not ok:
                violations.append(f'tile {i} {names[i]!r} ({art[i]}) and {name!r} on {state} ({identity}) '
                                  + (f'share an identity but differ in {fewest} pixels' if same else
                                     'have different identities but identical pixels'))
    for i in range(len(art)):
        seen.setdefault(art[i], (names[i], page['state'], crops[i][(0, 0)]))
    record = {'state': page['state'], 'file': str(page['path'].relative_to(ROOT)), 'presetOffset': page['presetOffset'],
              'addCatalog': page['addCatalog'], 'addInstalled': page['addInstalled'], 'discovered': page['discovered'],
              'names': names, 'art': art, 'scale': scale, 'cropPixels': 2 * round(TILE_CROP_DIP * scale / 2),
              'tileCheck': 'asserted' if asserted else 'recorded only: frosted tiles show the acrylic plate behind them',
              'violations': violations, 'pairs': pairs, 'earlierPages': earlier}
    return record, violations if asserted else []


def differing_pixels(a, b, tolerance=0):
    """(count, box) of pixels where some channel of two RGBA images of one size differs by more than tolerance.

    getbbox() of an RGBA difference looks at alpha only, so it would miss every color change between
    two opaque pixels; the per-pixel maximum over all channels does not."""
    channels = ImageChops.difference(a, b).split()
    mask = channels[0]
    for channel in channels[1:]:
        mask = ImageChops.lighter(mask, channel)
    return sum(mask.histogram()[tolerance + 1:]), mask.point(lambda v: 255 if v > tolerance else 0).getbbox()


def compare(path, reference, tolerance):
    if not reference.exists():
        return None
    a, b = Image.open(path).convert('RGBA'), Image.open(reference).convert('RGBA')
    if a.size != b.size:
        return {'reference': str(reference.relative_to(ROOT)), 'sizeMismatch': [a.size, b.size]}
    changed, _ = differing_pixels(a, b)
    differing, box = differing_pixels(a, b, tolerance)
    return {'reference': str(reference.relative_to(ROOT)), 'differingPixels': differing, 'box': box,
            'changedPixels': changed, 'tolerance': tolerance}


def describe(comparison):
    if comparison is None:
        return 'new'
    if 'sizeMismatch' in comparison:
        return 'size'
    within = comparison['changedPixels'] - comparison['differingPixels']
    return str(comparison['differingPixels']) + (f' ({within} within tolerance)' if within else '')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', default='output/runtime/v0.3.10')
    parser.add_argument('--no-reference', action='store_true',
                        help='Record every still without comparing it, to publish a new reference set.')
    # Routine runs write to the evidence directory; publishing a version's stills to
    # output/runtime/vX.Y.Z is an explicit choice, so tracked evidence is never rewritten by accident.
    parser.add_argument('--output', default='')
    parser.add_argument('--variants', default=','.join(v for v, _ in VARIANTS))
    # At rest, anti-aliased edges render up to two 8-bit levels away from 0.3.4: two contrast tile corners
    # since 0.3.5, and the light hub-language seat, whose 0.3.4 reference was captured just short of rest.
    # A seat captured mid-travel differs by up to 18 levels. 0 makes every change fail.
    parser.add_argument('--tolerance', type=int, default=2,
                        help='Channel levels a reference pixel may differ by (default 2; 0 is exact).')
    args = parser.parse_args()
    for path in (EXE, PROBE, CAPTURE):
        assert path.exists(), f'{path} is missing; run tools/build.ps1 and tools/build-capture-tool.ps1.'
    seam.assert_no_input_injection(Path(__file__), ROOT / 'tests/reel_seam.py')
    seam.assert_seam_binary(EXE)
    reference = (ROOT / args.reference).resolve()
    stamp = time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]
    data = ROOT / 'artifacts/qa' / f'capture-seam-{stamp}'
    output = (ROOT / args.output).resolve() if args.output else data
    output.mkdir(parents=True, exist_ok=True)
    data.mkdir(parents=True, exist_ok=True)
    wanted = [name for name in args.variants.split(',') if name]
    unknown = sorted(set(wanted) - {v for v, _ in VARIANTS})
    if unknown:
        raise SystemExit('Unknown variants: ' + ', '.join(unknown))
    evidence = {'captureMethod': 'Windows.Graphics.Capture.CreateForWindow of a seam test instance',
                'inputInjection': 'none', 'binarySha256': seam.sha256(EXE), 'reference': str(reference),
                'referenceStates': list(REFERENCE_STATES), 'referenceTolerance': args.tolerance,
                'addPageCheck': f'{TILE_CROP_DIP} DIP crop per slot, within one device pixel; different menuArt '
                                'identities must differ in a pixel, equal ones must be identical; machine-dependent, '
                                'never compared with a reference',
                'variants': []}
    failures = []
    for variant, override in VARIANTS:
        if variant not in wanted:
            continue
        entry = {'variant': variant, 'override': override, 'shots': [], 'addPages': []}
        evidence['variants'].append(entry)
        try:
            shots, pages = capture_variant(variant, override, output, data)
        except Exception as error:
            entry['error'] = f'{type(error).__name__}: {error}'
            failures.append(f'{variant}: capture failed: {entry["error"]}')
            print(variant, 'capture failed:', entry['error'])
            continue
        for state, path in shots:
            comparison = None if args.no_reference else compare(path, reference / path.name, args.tolerance)
            entry['shots'].append({'state': state, 'file': str(path.relative_to(ROOT)), 'comparison': comparison})
            if args.no_reference:
                continue
            if comparison is None:
                if state in REFERENCE_STATES:
                    failures.append(f'{path.name}: no reference image for a reference state')
            elif 'sizeMismatch' in comparison:
                failures.append(f'{path.name}: size {comparison["sizeMismatch"][0]} != reference {comparison["sizeMismatch"][1]}')
            elif comparison['differingPixels']:
                failures.append(f'{path.name}: {comparison["differingPixels"]} pixels differ by more than '
                                f'{args.tolerance} levels in {comparison["box"]}')
        # Machine-dependent and outside the reference comparison: the tiles are checked against their identities.
        seen = {}
        for page in pages:
            try:
                record, violations = check_tiles(page, 'material=frosted' not in override, seen)
            except AssertionError as error:
                entry['addPages'].append({'state': page['state'], 'error': str(error), 'violations': [str(error)],
                                          'art': page['art'], 'tileCheck': 'failed'})
                failures.append(f'{page["path"].name}: {error}')
                continue
            entry['addPages'].append(record)
            failures += [f'{page["path"].name}: {violation}' for violation in violations]
        print(variant, ', '.join(f"{s['state']}={describe(s['comparison'])}" for s in entry['shots']))
        print(variant, ', '.join(f"{p['state']}={len(p['art'])} tiles, "
                                 f"{len(set(p['art']))} identities, {len(p['violations'])} violations"
                                 + {'asserted': '', 'failed': ' (check failed)'}.get(p['tileCheck'], ' (not asserted)')
                                 for p in entry['addPages']))
    evidence['passed'] = not failures
    evidence['failures'] = failures
    (data / 'results.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    for failure in failures:
        print('FAIL', failure, file=sys.stderr)
    print(json.dumps({'passed': evidence['passed'], 'failures': len(failures), 'evidence': str(data / 'results.json')}))
    return 0 if evidence['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

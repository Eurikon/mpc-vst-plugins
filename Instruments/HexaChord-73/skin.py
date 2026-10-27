import argparse
import copy
import json
import math
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / 'build'
DESIGN = OUTPUT / 'design'
THEMES = ('c4c6c8', '6dabcf', 'e6a04b', '8cc4aa')
CHORD_LABELS = ('m9', 'M9', 'm7', 'M7', 'sus4', 'm11', 'triad', 'oct')
CHORD_GEOMETRY = {'PERFORM': ((224, 422), 89, 54), 'CHORD': ((365, 396), 192, 108)}
KNOB_FRAMES = 128
# Tall faders need fewer frames to stay below MPC's 16384px image limit.
FADER_FRAMES = 64
FADER_SIZE = 196
ENGINE_CONTROLS = (
    ('FELT CHARACTER', (('Hammer', 'FeltHammer'), ('Tine', 'FeltTine'), ('Bark', 'FeltBark')),
     'DAMPING', 'FeltDamping', 'SHORT,NORMAL,LONG'),
    ('ANA CHARACTER', (('Detune', 'AnaDetune'), ('Drift', 'AnaDrift'), ('Filter Env', 'AnaFilterEnv')),
     'WAVE', 'AnaWave', 'SAW,PULSE,MIX'),
    ('DCO CHARACTER', (('Pulse Width', 'DcoPulseWidth'), ('PWM Depth', 'DcoPwmDepth'), ('PWM Rate', 'DcoPwmRate')),
     'WAVE', 'DcoWave', 'SAW,PULSE,BOTH'),
    ('PRISM CHARACTER', (('Colour', 'PrismColour'), ('Partial', 'PrismPartial'), ('Strike', 'PrismStrike')),
     'RING', 'PrismRing', 'SHORT,NORMAL,LONG'),
)
PAGES = ('PERFORM', 'SYNTH', 'CHORD')
QLINKS = 'SynthModel,ChordType,Inversion,SubOsc,Cutoff,Resonance,TapeWow,WowSpeed,Attack,Decay,Sustain,Release,ChorusMix,TapeEcho,ShimmerVerb,OutputGain'


def svg(body, width=1280, height=800):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">{body}</svg>'


def text(x, y, label, color, size=20, anchor='start'):
    return f'<text x="{x}" y="{y}" fill="#{color}" font-family="monospace" font-size="{size}" font-weight="700" text-anchor="{anchor}" dominant-baseline="middle">{label}</text>'


def chord_position(index, page='PERFORM'):
    center, radius, _ = CHORD_GEOMETRY[page]
    angle = math.radians(index * 45 - 90)
    return (round(center[0] + radius * math.cos(angle)),
            round(center[1] + radius * math.sin(angle)))


def panel(accent, page):
    pieces = [
        '<defs><pattern id="dots" width="16" height="16" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r="1" fill="#27272a"/></pattern></defs>',
        '<rect width="1280" height="800" fill="#0c0c0e"/>',
        '<rect width="1280" height="800" fill="url(#dots)" opacity=".4"/>',
    ]
    if page == 'PERFORM':
        pieces.extend([
            '<path d="M24 180H1256" stroke="#27272a"/>',
            f'<g data-title-pill="HexChord 73" transform="translate(418,104)"><rect width="444" height="58" rx="29" fill="#242424" stroke="#{accent}" stroke-width="3"/><path d="M334 2H415A27 27 0 0 1 415 56H302Z" fill="#{accent}"/><g font-family="Titillium Web" font-weight="700" font-size="44" letter-spacing="1" text-anchor="middle"><text x="164" y="45" fill="#{accent}">HEXCHORD</text><text x="380" y="45" fill="#1a1a1a">73</text></g></g>',
        ])
    boxes = {
        'PERFORM': ((24, 198, 400, 506), (442, 198, 394, 342), (854, 198, 402, 342), (442, 554, 814, 150)),
        'SYNTH': ((24, 102, 1232, 128), (24, 244, 654, 460), (692, 244, 564, 460)),
        'CHORD': ((24, 102, 682, 602), (724, 102, 532, 602)),
    }
    for x, y, w, h in boxes[page]:
        pieces.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="16" fill="#18181b" fill-opacity=".65" stroke="#27272a" stroke-width="1.5"/>')
    headings = {
        'PERFORM': ((45, 214, 'VOICE ARCHITECTURE'), (462, 224, 'VCF &amp; DRIFT'), (874, 224, 'DIMENSION &amp; SPACE'), (462, 576, 'PAD ENVELOPE')),
        'SYNTH': ((45, 124, 'SYNTH ENGINE'), (45, 266, ENGINE_CONTROLS[THEMES.index(accent)][0]),
                  (713, 266, 'FILTER &amp; DRIFT'), (713, 478, 'ENVELOPE')),
        'CHORD': ((45, 124, 'CHORD'), (745, 124, 'VOICING')),
    }
    pieces.extend(text(x, y, title, accent, 20 if page == 'PERFORM' else 22) for x, y, title in headings[page])
    if page == 'SYNTH':
        pieces.append(text(350, 585, ENGINE_CONTROLS[THEMES.index(accent)][2], accent, 20, 'middle'))
        return svg('<g transform="translate(0,-86)">' + ''.join(pieces) + '</g>', 1280, 628)
    if page == 'PERFORM':
        pieces.append('<path d="M45 584H403" stroke="#27272a"/>')
    (cx, cy), radius, _ = CHORD_GEOMETRY[page]
    points = ' '.join(f'{cx + radius * 1.157 * math.cos(math.radians(i * 60 - 30)):.1f},{cy + radius * 1.157 * math.sin(math.radians(i * 60 - 30)):.1f}' for i in range(6))
    pieces.append(f'<polygon points="{points}" fill="#121215" stroke="#3f3f46" stroke-width="1.5" stroke-dasharray="4 3"/>')
    for fraction in (.26, .52, .76, 1):
        pieces.append(f'<circle cx="{cx}" cy="{cy}" r="{radius * fraction:.2f}" fill="none" stroke="#27272a" stroke-dasharray="3 3"/>')
    for index in range(8):
        x, y = chord_position(index, page)
        pieces.append(f'<path d="M{cx} {cy}L{x} {y}" stroke="#27272a"/>')
    return svg('<g transform="translate(0,-86)">' + ''.join(pieces) + '</g>', 1280, 628)


def knob(accent):
    point = lambda angle, radius: (48 + radius * math.sin(math.radians(angle)),
                                   48 - radius * math.cos(math.radians(angle)))
    start = point(-135, 42)
    end = point(135, 42)
    track = f'M{start[0]:.3f} {start[1]:.3f}A42 42 0 1 1 {end[0]:.3f} {end[1]:.3f}'
    tick_paths = []
    for angle in (-135, 0, 135):
        inner, outer = point(angle, 44), point(angle, 46)
        tick_paths.append(f'M{inner[0]:.3f} {inner[1]:.3f}L{outer[0]:.3f} {outer[1]:.3f}')
    highlight_start, highlight_end = point(-68, 32), point(68, 32)
    highlight = f'M{highlight_start[0]:.3f} {highlight_start[1]:.3f}A32 32 0 0 1 {highlight_end[0]:.3f} {highlight_end[1]:.3f}'
    frames = []
    for index in range(KNOB_FRAMES):
        angle = -135 + index * 270 / (KNOB_FRAMES - 1)
        body = f'<path data-knob-part="track" d="{track}" fill="none" stroke="#303036" stroke-width="3.6" stroke-linecap="round"/>'
        body += f'<path data-knob-part="ticks" d="{"".join(tick_paths)}" fill="none" stroke="#666670" stroke-width="1.2" stroke-linecap="round"/>'
        if index:
            end = point(angle, 42)
            arc = f'M{start[0]:.3f} {start[1]:.3f}A42 42 0 {int(angle > 45)} 1 {end[0]:.3f} {end[1]:.3f}'
            body += f'<path data-knob-part="value" d="{arc}" fill="none" stroke="#{accent}" stroke-width="3.6" stroke-linecap="round"/>'
        body += '<circle data-knob-part="base" cx="48" cy="49" r="34" fill="#111114"/>'
        body += '<circle data-knob-part="face" cx="48" cy="48" r="33" fill="#25252a" stroke="#414149" stroke-width="1.5"/>'
        body += f'<path data-knob-part="highlight" d="{highlight}" fill="none" stroke="#575760" stroke-width="1" stroke-linecap="round"/>'
        inner, outer = point(angle, 15), point(angle, 27)
        body += f'<path data-knob-part="pointer" d="M{inner[0]:.3f} {inner[1]:.3f}L{outer[0]:.3f} {outer[1]:.3f}" stroke="#{accent}" stroke-width="3.6" stroke-linecap="round"/>'
        frames.append(f'<g transform="translate(0,{index * 96})">{body}</g>')
    return svg(''.join(frames), 96, 96 * KNOB_FRAMES)


def segment(accent, selected=False, circular=False):
    color = f'#{accent}' if selected else '#242429'
    edge = f'#{accent}' if selected else '#45454f'
    if circular:
        body = f'<circle cx="27" cy="27" r="24.5" fill="{color}" stroke="{edge}" stroke-width="1.5"/>'
        return svg(body, 54, 54)
    return svg(f'<rect x="1" y="1" width="198" height="46" rx="1" fill="{color}" stroke="{edge}" stroke-width="1.5"/>', 200, 48)


def generate():
    DESIGN.mkdir(parents=True, exist_ok=True)
    (DESIGN / 'knob-base.svg').unlink(missing_ok=True)
    lines = [
        'theme_bg=0C0C0E', 'theme_panel=18181B', 'theme_line=27272A',
        'theme_ink=E4E4E7', 'theme_ink_dim=A1A1AA', 'theme_accent=6DABCF',
        'theme_seg_active_tx=09090B', 'theme_lcd=09090B',
        'label_scale=0.9', 'scale_names=1', 'art_css=../skin.css',
    ]
    perform = (
        (540, 297, 'Cutoff', 'Cutoff'), (738, 297, 'Resonance', 'Resonance'),
        (540, 439, 'Tape Wow', 'TapeWow'), (738, 439, 'Wow Speed', 'WowSpeed'),
        (958, 297, 'Chorus', 'ChorusMix'), (1158, 297, 'Tape Echo', 'TapeEcho'),
        (958, 439, 'Shimmer', 'ShimmerVerb'), (1158, 439, 'Level', 'OutputGain'),
        (534, 616, 'Attack', 'Attack'), (743, 616, 'Decay', 'Decay'),
        (952, 616, 'Sustain', 'Sustain'), (1160, 616, 'Release', 'Release'),
        (224, 615, 'Sub Bass', 'SubOsc'),
    )
    synth = tuple((x, y, label, key) for y, entries in (
        (350, (('Cutoff', 'Cutoff'), ('Resonance', 'Resonance'), ('Tape Wow', 'TapeWow'), ('Wow Speed', 'WowSpeed'))),
        (554, (('Attack', 'Attack'), ('Decay', 'Decay'), ('Sustain', 'Sustain'), ('Release', 'Release'))),
    ) for x, (label, key) in zip((765, 900, 1035, 1170), entries))
    for index, accent in enumerate(THEMES):
        (DESIGN / f'knob-{index}.svg').write_text(knob(accent))
        for state in (False, True):
            suffix = 'on' if state else 'off'
            (DESIGN / f'segment-{index}-{suffix}.svg').write_text(segment(accent, state))
            (DESIGN / f'segment-large-{index}-{suffix}.svg').write_text(segment(accent, state))
            (DESIGN / f'node-{index}-{suffix}.svg').write_text(segment(accent, state, True))
            (DESIGN / f'node-large-{index}-{suffix}.svg').write_text(segment(accent, state, True))
            (DESIGN / f'character-switch-{index}-{suffix}.svg').write_text(segment(accent, state))
    for path in DESIGN.glob('damping-*.svg'):
        path.unlink()
    for page in PAGES:
        lines.append(f'[tab {page}]')
        for index, accent in enumerate(THEMES):
            filename = f'panel-{page.lower()}-{index}.svg'
            (DESIGN / filename).write_text(panel(accent, page))
            condition = f' when=SynthModel:{index}'
            lines.append(f'art file="design/{filename}"{condition}')
            size = '' if page == 'PERFORM' else '-large'
            button = f' img=design/segment{size}-{index}-off.svg img_on=design/segment{size}-{index}-on.svg'
            if page != 'CHORD':
                geometry = 'cx=224 cy=248 sw=181 sh=34 rows=2' if page == 'PERFORM' else 'cx=640 cy=187 sw=270 sh=64 rows=1'
                lines.append(f'enum_h {geometry} label="" key=SynthModel options="Felt 73,Ana 78,Poly DCO,Prism Bell"' + button + condition)
            if page != 'SYNTH':
                geometry = 'cx=224 cy=558 sw=83 sh=32 rows=1' if page == 'PERFORM' else 'cx=990 cy=235 sw=228 sh=68 rows=2'
                lines.append(f'enum_h {geometry} label="" key=Inversion options="ROOT,1ST,2ND,SPREAD"' + button + condition)
                (cx, cy), _, diameter = CHORD_GEOMETRY[page]
                lines.append(f'enum_h cx={cx} cy={cy} label="" key=ChordType options="' + ','.join(CHORD_LABELS) + f'" sw={diameter} sh={diameter} rows=4 img=design/node{size}-{index}-off.svg img_on=design/node{size}-{index}-on.svg' + condition)
            controls = perform if page == 'PERFORM' else synth if page == 'SYNTH' else ((990, 524, 'Sub Bass', 'SubOsc'),)
            if page == 'SYNTH':
                _, sliders, _, switch, options = ENGINE_CONTROLS[index]
                for x, (label, key) in zip((140, 350, 560), sliders):
                    lines.append(f'slider_v cx={x} cy=400 w=72 h=120 label="{label}" key={key} look=fader' + condition)
                lines.append(f'enum_h cx=350 cy=650 sw=152 sh=52 rows=1 label="" key={switch} options="{options}" img=design/character-switch-{index}-off.svg img_on=design/character-switch-{index}-on.svg' + condition)
            for x, y, label, key in controls:
                radius = (25 if key == 'SubOsc' else 28) if page == 'PERFORM' else 52
                lines.append(f'knob cx={x} cy={y} r={radius} label="{label}" key={key} ink_dim={accent} strip=design/knob-{index}.svg frames={KNOB_FRAMES}' + condition)
        lines.append(f'qlinks "{page}" = {QLINKS}')
    (OUTPUT / 'layout.conf').write_text('\n'.join(lines) + '\n')


def fader_strip(skin, accent, filename):
    from PIL import Image, ImageDraw

    strip = Image.new('RGBA', (FADER_SIZE, FADER_SIZE * FADER_FRAMES))
    for frame in range(FADER_FRAMES):
        image = Image.new('RGBA', (FADER_SIZE, FADER_SIZE))
        drawing = ImageDraw.Draw(image)
        drawing.rectangle((93, 19, 103, 177), fill='#09090b', outline='#3f3f46')
        for tick in range(5):
            y = 24 + tick * 35
            drawing.line((64, y, 78, y), fill='#71717a', width=2)
            drawing.line((118, y, 132, y), fill='#71717a', width=2)
        y = round(164 - frame * 140 / (FADER_FRAMES - 1))
        drawing.rectangle((96, y, 100, 164), fill='#' + accent)
        drawing.rectangle((71, y - 11, 125, y + 11), fill='#3f3f46', outline='#a1a1aa', width=2)
        drawing.line((79, y, 117, y), fill='#' + accent, width=3)
        strip.paste(image, (0, frame * FADER_SIZE))
    strip.save(Path(skin) / filename)


def engine_sliders(skin, definitions, components):
    filenames = {theme: f'engine-fader-{theme}.png' for theme in range(4)}
    for theme, filename in filenames.items():
        fader_strip(skin, THEMES[theme], filename)
    previous_images = set()
    previous_types = set()
    for component in components:
        source_type = component['componentData']['type']
        if not source_type.startswith(('shSlider_v_', 'shEngineFader_')):
            continue
        bindings = component['handle remapping']['map']
        parameter = next(value['value'] for value in bindings if value['key'] == 'Data')
        theme, slot = divmod(int(parameter.split()[-1]) - 18, 4)
        if theme not in filenames or slot >= 3:
            raise ValueError('Native engine slider has an unexpected parameter binding')
        if component['bounds']['additionalInvalidatingHandles'] != [f'IndexedEnabling/{theme}/4/Parameter 2']:
            raise ValueError('Native engine slider must follow its selected engine')
        kind = f'shEngineFader_{theme}'
        if kind not in definitions:
            definitions[kind] = copy.deepcopy(definitions[source_type])
        definition = definitions[kind]
        if source_type != kind:
            previous_types.add(source_type)
        component['componentData']['type'] = kind
        center = (140, 350, 560)[slot]
        component['bounds']['bounds'] = f'{center - 98} 216 196 252'
        for child in definition['componentsData']:
            kind = child['componentData']['type']
            if kind == 'Knob':
                native = child['componentData']['data']
                previous_images.add(native['filmStrip'])
                native['filmStrip'] = filenames[theme]
                native['numFrames'] = FADER_FRAMES
                child['bounds']['bounds'] = '0 0 196 196'
            elif kind == 'Label':
                native = child['componentData']['data']
                is_name = native['type'] == 'Name'
                child['bounds']['bounds'] = '0 198 196 24' if is_name else '0 224 196 26'
                native['textStyle']['font']['height'] = 20 if is_name else 22
                native['textStyle']['colour'] = 'ffe4e4e7' if is_name else 'ff' + THEMES[theme]
            elif kind == 'Focus':
                child['bounds']['bounds'] = '0 0 196 252'
    for kind in previous_types:
        del definitions[kind]
    for previous in previous_images | {'felt-fader.png'}:
        if previous not in filenames.values():
            (Path(skin) / previous).unlink(missing_ok=True)


def voicing_indicators(skin, page):
    from PIL import Image, ImageDraw

    (cx, cy), radius, _ = CHORD_GEOMETRY[page]
    diameter = 12 if page == 'PERFORM' else 24
    dot_radius = 4 if page == 'PERFORM' else 8
    result = []
    for theme, accent in enumerate(THEMES):
        filename = f'voicing-{page.lower()}-{theme}.png'
        image = Image.new('RGBA', (diameter, diameter))
        drawing = ImageDraw.Draw(image)
        middle = diameter // 2
        drawing.ellipse((middle - dot_radius, middle - dot_radius,
                         middle + dot_radius, middle + dot_radius), fill='#' + accent)
        image.save(Path(skin) / filename)
        for inversion in range(3):
            for chord in range(1 if inversion == 0 else 8):
                angle = math.radians(chord * 45 - 90)
                distance = radius * inversion * .26
                x = round(cx + distance * math.cos(angle)) - middle
                y = round(cy + distance * math.sin(angle)) - 86 - middle
                conditions = [f'IndexedEnabling/{theme}/4/Parameter 2',
                              f'IndexedEnabling/{inversion}/4/Parameter 1']
                if inversion:
                    conditions.append(f'IndexedEnabling/{chord}/8/Parameter 0')
                result.append({
                    'version': 2,
                    'componentData': {'version': 1, 'name': f'VoicingIndicator_{theme}_{inversion}_{chord}',
                                      'type': 'Image', 'data': {'version': 2, 'imageType': 'Regular',
                                                               'colour': '0', 'image': filename}},
                    'handle remapping': {'version': 1, 'map': []},
                    'bounds': {'version': 2, 'acceptsHWFocus': 'No', 'showWhenDataModelInvalid': 'Show',
                               'whenVisible': 'Always', 'boundsType': 'Absolute',
                               'bounds': f'{x} {y} {diameter} {diameter}',
                               'additionalInvalidatingHandles': conditions},
                })
    return result


def chord_touch(skin):
    from PIL import Image

    (cx, cy), radius, diameter = CHORD_GEOMETRY['CHORD']
    reach = radius + diameter // 2
    Image.new('RGBA', (1, 1)).save(Path(skin) / 'chord-touch.png')
    return {
        'version': 2,
        'componentData': {'version': 1, 'name': 'ChordTouch', 'type': 'XYPad',
                          'data': {'version': 1, 'xPositionHandle': 'Parameter 16',
                                   'yPositionHandle': 'Parameter 17', 'image': 'chord-touch.png'}},
        'handle remapping': {'version': 1, 'map': []},
        'bounds': {'version': 1, 'acceptsHWFocus': 'No', 'showWhenDataModelInvalid': 'Show',
                   'whenVisible': 'Always', 'boundsType': 'Absolute',
                   'bounds': f'{cx - reach} {cy - 86 - reach} {reach * 2} {reach * 2}'},
    }


def finalize(skin):
    path = Path(skin) / 'TUI.json'
    data = json.loads(path.read_text())
    moved = 0

    document = data['pageData']
    definitions = {item['key']: item['value'] for item in document['componentDefinitions']['localComponentDefinitions']}
    for tab in document['tabs']:
        page = tab['tabName']
        if page == 'SYNTH':
            engine_sliders(skin, definitions, definitions[tab['componentName']]['componentsData'])
        if page not in CHORD_GEOMETRY:
            continue
        diameter = CHORD_GEOMETRY[page][2]
        components = definitions[tab['componentName']]['componentsData']
        if page == 'PERFORM':
            knob_types = {value['componentData']['type'] for value in components
                          if value['componentData']['type'].startswith('shKnob')}
            for knob_type in knob_types:
                for component in definitions[knob_type]['componentsData']:
                    label_data = component['componentData']
                    if label_data['type'] != 'Label':
                        continue
                    role = label_data['data']['type']
                    if role not in ('Name', 'Value'):
                        continue
                    label_data['data']['textStyle']['font']['height'] = 20 if role == 'Name' else 22
                    if role == 'Value':
                        x, y, width, _ = component['bounds']['bounds'].split()
                        component['bounds']['bounds'] = f'{x} {y} {width} 26'
        components[:] = [value for value in components
                         if not value['componentData']['name'].startswith(('VoicingIndicator_', 'ChordTouch'))]
        for value in components:
            match = re.fullmatch(r'shSeg_ChordType_([0-7])(?:_[0-9a-f]+)?', value['componentData']['type'])
            if match:
                x, y = chord_position(int(match[1]), page)
                value['bounds']['bounds'] = f'{x - diameter // 2} {y - 86 - diameter // 2} {diameter} {diameter}'
                moved += 1
        first_control = next(index for index, value in enumerate(components)
                             if value['componentData']['type'] != 'Image')
        components[first_control:first_control] = voicing_indicators(skin, page)
        if page == 'CHORD':
            components.append(chord_touch(skin))
    if moved != 64:
        raise RuntimeError(f'Expected 64 themed chord buttons, found {moved}')
    document['componentDefinitions']['localComponentDefinitions'] = [
        {'key': key, 'value': value} for key, value in definitions.items()]
    path.write_text(json.dumps(data, indent=4) + '\n')
    from metadata import write_factory_presets
    write_factory_presets(Path(skin).parent)


def preview(skin, output):
    import importlib.util
    from PIL import Image

    filename = Path(os.environ.get('MPC_VST', str(ROOT.parents[1] / 'toolchain/upstream'))) / 'tools/studio.py'
    spec = importlib.util.spec_from_file_location('hexchord_skin_studio', filename)
    studio = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(studio)
    components = studio.tab_components

    def visible_components(tab, definitions):
        values = components(tab, definitions)
        for value in values:
            if value['componentData']['type'] == 'XYPad':
                with Image.open(Path(skin) / value['componentData']['data']['image']) as image:
                    if image.convert('RGBA').getchannel('A').getbbox() is not None:
                        raise ValueError('Preview may omit only invisible XY touch controls')
        return [value for value in values if value['componentData']['type'] != 'XYPad']

    studio.tab_components = visible_components
    for path, name in studio.preview(skin, output):
        print(f'{name}: {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--finalize', metavar='SKIN_DIR')
    action.add_argument('--preview', metavar='SKIN_DIR')
    parser.add_argument('-o', default=str(OUTPUT / 'preview_%d.png'))
    arguments = parser.parse_args()
    if arguments.finalize:
        finalize(arguments.finalize)
    elif arguments.preview:
        preview(arguments.preview, arguments.o)
    else:
        generate()

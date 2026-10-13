import re
import shlex
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / 'build' / 'design'
THEMES = [
    ('Default', 'ffc050', 'ffb000', 'ffd385', '99a489'),
    ('Copper', 'e9824b', 'f69b64', 'c87950', 'b3a296'),
    ('Glacier', '83c9de', '91d7ed', '70a6b4', '96a9ae'),
    ('Moss', 'b8c681', 'c8cf8a', '99a775', 'a2aa92'),
    ('Amethyst', 'bba0df', 'c3abe9', '9a83b7', 'a5a0b3'),
    ('Ember', 'e69687', 'eea08d', 'b87e73', 'b1a19b'),
]


def mix(first, second, amount):
    a, b = bytes.fromhex(first), bytes.fromhex(second)
    return ''.join(f'{round(x + (y - x) * amount):02x}' for x, y in zip(a, b))


def recolor(text, theme):
    name, accent, deep, pale, muted = theme
    colors = {'ffc050': accent, 'ffb000': deep, 'ffd385': pale, '99a489': muted}
    if name != 'Default':
        colors.update({key: mix('1a1a1a', accent, .42) for key in ('8a6020', '635037')})
        colors['e6b060'] = accent
        colors.update({key: mix('282828', muted, .2) for key in ('3a3a3a', '37332d')})
        colors['ebebe4'] = mix(pale, 'ffffff', .65)
    return re.sub(r'#([0-9a-fA-F]{6})', lambda match: '#' + colors.get(match[1].lower(), match[1]), text)


def generate():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    text = (ROOT / 'layout.template.conf').read_text()
    top, *sections = text.split('[tab ')
    top = top.replace('art_css=skin.css', 'art_css=../skin.css')
    top = top.replace('=assets/', '=../assets/')
    top = '\n'.join(line for line in top.splitlines() if not line.startswith('seg_img'))
    result = [top]
    panel = (ROOT / 'assets/panel.svg').read_text()
    brand = re.search(r'<g style="font-family:.*?</g>', panel, re.S).group(0)
    for section in sections:
        tab, content = section.split(']\n', 1)
        widgets = [line for line in content.splitlines() if line and not line.startswith(('art ', 'qlinks '))]
        result.append(f'[tab {tab}]')
        for index, theme in enumerate(THEMES):
            background = recolor(panel.replace(brand, 'BRAND'), theme).replace('BRAND', brand)
            boxes = []
            for line in widgets:
                if not line.startswith('knob '):
                    continue
                fields = dict(token.split('=', 1) for token in shlex.split(line)[1:])
                x, y = int(fields['cx']), int(fields['cy'])
                boxes.append(f'<rect x="{x - 75}" y="{y - 6}" width="150" height="32" rx="5" fill="#1a1a1a" stroke="url(#value-border)" stroke-width="1.5"/>')
            background = background.replace('</svg>', ''.join(boxes) + '</svg>')
            filename = f'panel-{tab.lower()}-{index}.svg'
            (OUTPUT / filename).write_text(background)
            result.append(f'art file="design/{filename}" when=theme:{index}')
            for line in widgets:
                if line.startswith('knob '):
                    line += f' ink_dim={theme[2]} img=design/knob-{index}.svg base=../assets/knob-base.svg'
                else:
                    line += f' img=design/rubber-off.svg img_on=design/rubber-{index}.svg'
                result.append(line + f' when=theme:{index}')
            result.append(f'popup cx=1090 cy=134 w=250 h=42 label="" key=theme options="Default,Copper,Glacier,Moss,Amethyst,Ember" accent={theme[1]} when=theme:{index}')
        result.extend(line for line in content.splitlines() if line.startswith('qlinks '))
    source = ROOT / 'assets/rubber-off.svg'
    (OUTPUT / 'rubber-off.svg').write_bytes(source.read_bytes())
    for index, theme in enumerate(THEMES):
        (OUTPUT / f'knob-{index}.svg').write_text(recolor((ROOT / 'assets/knob.svg').read_text(), theme))
        button = recolor((ROOT / 'assets/rubber-on.svg').read_text(), theme)
        if index:
            pale = bytes.fromhex(theme[3])
            edge = ','.join(str(round(x * ratio, 2)) for x, ratio in zip(pale, (197.75 / 255, 150.5 / 211, 66.5 / 133)))
            button = button.replace('rgb(197.75,150.5,66.5)', f'rgb({edge})')
        (OUTPUT / f'rubber-{index}.svg').write_text(button)
    (ROOT / 'build/layout.conf').write_text('\n'.join(result) + '\n')


if __name__ == '__main__':
    generate()

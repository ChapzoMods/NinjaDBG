#!/usr/bin/env python3
"""Render a terminal-style screenshot of NinjaDBG v1.0.5 decomp CLI."""
from PIL import Image, ImageDraw, ImageFont
import re

with open('/tmp/cli_decomp_output.txt', 'r') as f:
    lines = f.readlines()

def strip_ansi(s):
    return re.sub(r'\x1b\[[0-9;]*m', '', s)

lines = [strip_ansi(l).rstrip() for l in lines]
# Limit to first 70 lines for the screenshot
lines = lines[:70]

CHAR_W = 8
CHAR_H = 16
MARGIN = 20
WIDTH = max(len(l) for l in lines) * CHAR_W + MARGIN * 2
HEIGHT = len(lines) * CHAR_H + MARGIN * 2 + 40

img = Image.new('RGB', (WIDTH, HEIGHT), (20, 22, 31))
draw = ImageDraw.Draw(img)

try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 14)
    title_font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 14)
except:
    font = ImageFont.load_default()
    title_font = font

# Title bar
draw.rectangle([(0, 0), (WIDTH, 30)], fill=(37, 42, 64))
draw.ellipse([(10, 10), (20, 20)], fill=(255, 95, 86))
draw.ellipse([(28, 10), (38, 20)], fill=(255, 189, 46))
draw.ellipse([(46, 10), (56, 20)], fill=(39, 201, 63))
draw.text((WIDTH // 2 - 120, 8), "ninjadb --cli  —  NinjaDBG v1.0.5 (decomp)", fill=(230, 232, 240), font=title_font)

y = 40
for line in lines:
    color = (230, 232, 240)
    s = line.strip()
    if s.startswith('_') or s.startswith('|'):
        color = (0, 255, 225)
    elif s.startswith('v1.0.5'):
        color = (0, 255, 225)
    elif 'AVAILABLE' in s:
        color = (74, 222, 128)
    elif 'NOT INSTALLED' in s:
        color = (255, 180, 84)
    elif s.startswith('Decompiler backend') or s.startswith('Backend') or s.startswith('current selection'):
        color = (122, 183, 255)
    elif s.startswith('===') or s.startswith('---'):
        color = (138, 143, 163)
    elif s.startswith('Installation') or s.startswith('Examples') or s.startswith('CLI commands') or s.startswith('Backends'):
        color = (255, 180, 84)
    elif s.startswith('#') or s.startswith('(ninjadb)'):
        color = (122, 183, 255)
    elif 'sudo' in s or 'pip3' in s or 'git clone' in s or 'cd retdec' in s:
        color = (74, 222, 128)
    draw.text((MARGIN, y), line, fill=color, font=font)
    y += CHAR_H

out = '/home/z/my-project/download/ninjadb_decomp_v1.0.5.png'
img.save(out)
print(f'Saved: {out}  ({img.size[0]}x{img.size[1]})')

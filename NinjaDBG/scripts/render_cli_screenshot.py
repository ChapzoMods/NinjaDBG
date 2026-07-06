#!/usr/bin/env python3
"""Render a terminal-style screenshot of NinjaDBG v1.0.4 CLI output."""
from PIL import Image, ImageDraw, ImageFont
import os

# Read the captured CLI output
with open('/tmp/cli_output.txt', 'r') as f:
    lines = f.readlines()

# Strip ANSI codes (basic)
import re
def strip_ansi(s):
    return re.sub(r'\x1b\[[0-9;]*m', '', s)

lines = [strip_ansi(l).rstrip() for l in lines]

# Terminal dimensions
CHAR_W = 8
CHAR_H = 16
MARGIN = 20
WIDTH = max(len(l) for l in lines) * CHAR_W + MARGIN * 2
HEIGHT = len(lines) * CHAR_H + MARGIN * 2 + 40  # extra for title bar

# Create image with dark background
img = Image.new('RGB', (WIDTH, HEIGHT), (20, 22, 31))
draw = ImageDraw.Draw(img)

# Try to load a monospace font
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 14)
    title_font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 14)
except:
    font = ImageFont.load_default()
    title_font = font

# Draw title bar
draw.rectangle([(0, 0), (WIDTH, 30)], fill=(37, 42, 64))
# Three dots (macOS-style)
draw.ellipse([(10, 10), (20, 20)], fill=(255, 95, 86))
draw.ellipse([(28, 10), (38, 20)], fill=(255, 189, 46))
draw.ellipse([(46, 10), (56, 20)], fill=(39, 201, 63))
# Title
draw.text((WIDTH // 2 - 100, 8), "ninjadb --cli  —  NinjaDBG v1.0.4", fill=(230, 232, 240), font=title_font)

# Draw each line
y = 40
for line in lines:
    # Color: cyan for the banner, yellow for >>, green for script output
    color = (230, 232, 240)  # default light
    if line.strip().startswith('_') or line.strip().startswith('|'):
        color = (0, 255, 225)  # cyan banner
    elif line.strip().startswith('>>'):
        color = (255, 180, 84)  # yellow RIP marker
    elif line.strip().startswith('[script]'):
        color = (74, 222, 128)  # green script output
    elif line.strip().startswith('ADDRESS') or line.strip().startswith('---'):
        color = (138, 143, 163)  # dim header
    elif ' Attached' in line or 'Script completed' in line or 'Detaching' in line or 'Running' in line:
        color = (122, 183, 255)  # blue info
    elif line.strip().startswith('v1.0.4'):
        color = (0, 255, 225)
    draw.text((MARGIN, y), line, fill=color, font=font)
    y += CHAR_H

# Save
out = '/home/z/my-project/download/ninjadb_cli_v1.0.4.png'
img.save(out)
print(f'Saved: {out}  ({img.size[0]}x{img.size[1]})')

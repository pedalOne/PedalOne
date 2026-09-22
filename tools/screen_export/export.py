#!/usr/bin/env python3
"""Render unmodified firmware drawing functions with its installed Arduino_GFX library."""
from pathlib import Path
import re, shutil, subprocess, tempfile
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[2]
LIB=Path.home()/'Documents/Arduino/libraries/GFX_Library_for_Arduino/src'
FONTS=Path.home()/'Documents/Arduino/libraries/Adafruit_GFX_Library'
BUILD=Path(tempfile.mkdtemp(prefix='pedalone-screens-'))
for name in ['Arduino_GFX.cpp','Arduino_GFX.h','Arduino_G.cpp','Arduino_G.h','gfxfont.h']:
 shutil.copy(LIB/name,BUILD/name)
shutil.copytree(LIB/'font',BUILD/'font')
for name in ['Arduino.h','Print.h','Arduino_DataBus.h']:shutil.copy(Path(__file__).parent/name,BUILD/name)
(BUILD/'Adafruit_GFX.h').write_text('#pragma once\n#include "Arduino_GFX.h"\n')
src=(ROOT/'PedalOne.ino').read_text()
# Match balanced function bodies while ignoring braces inside strings/comments.
def functions(src):
 out={}
 for m in re.finditer(r'^([\w:*<>]+(?:\s+[\w:*<>]+)*)\s*\b(\w+)\(([^;{}]*?)\)\s*\{',src,re.M):
  start=m.end()-1; depth=0
  tokens=re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]',src[start:])
  for t in tokens:
   if t.group()=='{':depth+=1
   elif t.group()=='}':depth-=1
   if depth==0:
    out[m[2]]=(m[1]+' '+m[2]+'('+m[3]+')',src[m.start():start+t.end()]);break
 return out
gpxsrc=(ROOT/'gpx_display.inc').read_text()
sharedsrc=(ROOT/'shared_map_display.inc').read_text()
f=functions(src+'\n'+gpxsrc+'\n'+sharedsrc)
roots='drawReadyScreen drawMenu drawStatus drawOtaUpdate drawConfirmation drawBluetoothPage drawDisplayPage drawCountdown drawRideCanceled drawRidePage drawSummary drawRidesList drawSaveRidePrompt drawNavigationPage drawAncsTest drawPedalOneSplashFrame drawRouteLibrary drawStartRouteMenu drawGpxRoute drawFreeRideMapPage drawLiveGpxPage drawRidarPage'.split()
overrides={'endFrame':'void endFrame() {}','endAnimatedFrame':'void endAnimatedFrame() {}','loadSummaryRoute':'void loadSummaryRoute() {}'}
selected=set()
def add(n):
 if n in selected:return
 selected.add(n)
 if n in overrides:return
 for c in re.findall(r'\b(\w+)\s*\(',re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', '', f[n][1])):
  if c in f or c in overrides:add(c)
for n in roots:add(n)
head='#include "Arduino_GFX.h"\n#include <vector>\n#include <fstream>\n'
head+='#include "gpx_navigation.h"\n'
head+='\n'.join(x for x in src.splitlines() if x.startswith('#include <Fonts/') or x.startswith('#include "Arial_'))+'\n'
head+=src[src.index('constexpr int SCREEN_SIZE'):src.index('constexpr char BLE_SERVICE_UUID')]
head+=src[src.index('constexpr uint32_t LOCATION_TIMEOUT_MS'):src.index('#pragma pack(push')]
head+=src[src.index('#pragma pack(push'):src.index('Arduino_DataBus *bus')]
globals=src[src.index('AppState appState'):src.index('int us(float')]
# Hardware objects are replaced by deterministic fixture providers.
globals='\n'.join(l for l in globals.splitlines() if not re.match(r'(gpx::|GpxLibrary |SharedMapLibrary |File |esp_|portMUX|OnboardGps |BLECharacteristic)',l))
head+=globals+'\ngpx::Simulation gpxSimulation;\nfloat navArrowYOffset=0,navArrowScale=1;\n'
head+=src[src.index('constexpr uint32_t SPLASH_DOT_COMPLETE_MS'):src.index('float splashProgress(')]
head+=gpxsrc[:gpxsrc.index('void drawRouteFilename')]
head+=sharedsrc[:sharedsrc.index('void clearSharedMapTileCache')]
head+=(Path(__file__).parent/'fixtures.h').read_text()+'\n'
for n in sorted(selected):
 if n in overrides:head+=overrides[n]+'\n'
 else:head+=f[n][0]+';\n'
for n in sorted(selected):
 if n not in overrides:head+=re.sub(r'\s*=\s*[^,\)]+(?=[,\)])','',f[n][0])+f[n][1][len(f[n][0]):]+'\n' if False else re.sub(r'\s*=\s*[^,\)]+(?=[,\)])','',f[n][0])+f[n][1][f[n][1].index('{'):]+'\n'
head+=(Path(__file__).parent/'scenes.cpp').read_text()
(BUILD/'export.cpp').write_text(head)
cmd=['clang++','-std=c++17','-w','-Wno-c++11-narrowing','-I'+str(BUILD),'-I'+str(ROOT),'-I'+str(FONTS),str(BUILD/'export.cpp'),str(BUILD/'Arduino_GFX.cpp'),str(BUILD/'Arduino_G.cpp'),'-o',str(BUILD/'export')]
r=subprocess.run(cmd,stderr=subprocess.PIPE,text=True)
if r.returncode:print(r.stderr[:16000]);print('Build:',BUILD);raise SystemExit(1)
output = ROOT/'docs/screens'
subprocess.run([str(BUILD/'export')],cwd=output,check=True)
for ppm in output.glob('*.ppm'):
 Image.open(ppm).save(ppm.with_suffix('.png'), optimize=True)
 ppm.unlink()

screens = sorted(output.glob('[0-9][0-9]*-*.png'))
thumb, label, columns = 220, 34, 4
rows = (len(screens) + columns - 1) // columns
gallery = Image.new('RGB', (columns * thumb, rows * (thumb + label)), (22, 22, 22))
draw = ImageDraw.Draw(gallery)
try:
 font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 14)
except OSError:
 font = ImageFont.load_default()
for index, path in enumerate(screens):
 image = Image.open(path).convert('RGB').resize((thumb, thumb), Image.Resampling.LANCZOS)
 x, y = index % columns * thumb, index // columns * (thumb + label)
 gallery.paste(image, (x, y))
 label_text = path.stem.split('-', 1)[1].replace('-', ' ').title()
 draw.text((x + 8, y + thumb + 7), label_text, font=font, fill='white')
gallery.save(output/'gallery.png', optimize=True)

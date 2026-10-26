"""Rasterize the Soft Orbit brand geometry for Windows-required package/ICO assets.
The actual wheel UI is rendered from native vector geometry.
Requires Pillow; outputs are build-time assets, not an animation frame sequence.
"""
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw

root=Path(__file__).resolve().parents[1]
out=root/'packaging/assets'
out.mkdir(parents=True,exist_ok=True)
# The mark's geometry is iconography.orbitMark, which src/orbit_mark.h also draws.
mark=json.loads((root/'design/vi/tokens.json').read_text(encoding='utf-8'))['iconography']['orbitMark']
size,c=mark['canvas'],mark['center']
scale=8
im=Image.new('RGBA',(size*scale,size*scale),(0,0,0,0))
d=ImageDraw.Draw(im)
def box(x,y,w,h): return tuple(int(v*scale) for v in (x,y,x+w,y+h))
def ring(radius): return box(c-radius,c-radius,2*radius,2*radius)
d.rounded_rectangle(box(0,0,size,size),radius=mark['plateRadius']*scale,fill=mark['plate'])
d.arc(ring(mark['outerRadius']),start=mark['outerFrom'],end=mark['outerTo'],fill='white',width=mark['stroke']*scale)
d.arc(ring(mark['innerRadius']),start=mark['innerFrom'],end=mark['innerTo'],fill='white',width=mark['stroke']*scale)
angle=math.radians(mark['dotAngle']);dot=mark['dotRadius']
x=c+mark['outerRadius']*math.cos(angle);y=c+mark['outerRadius']*math.sin(angle)
d.ellipse(box(x-dot,y-dot,2*dot,2*dot),fill='white')
resample=Image.Resampling.LANCZOS
for name,size in [('Square44x44Logo',44),('Square150x150Logo',150),('StoreLogo',50)]:
    im.resize((size,size),resample).save(out/f'{name}.png')
im.resize((256,256),resample).save(out/'CopilotReel.ico',sizes=[(s,s) for s in [16,24,32,48,64,128,256]])
# The reel's own mark among the brands, as plated artwork: About's first tile shows it.
im.resize((256,256),resample).save(root/'assets/brands/copilot-reel.png')
print('Generated 3 package PNGs, Soft Orbit ICO and the copilot-reel brand PNG.')

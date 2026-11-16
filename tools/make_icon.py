# Generates FalseAudio icon PNGs + ICO (Pillow only). Run: python tools/make_icon.py
from PIL import Image, ImageDraw, ImageChops
import os
out = os.path.join(os.path.dirname(__file__), "..", "resources", "icon")
BOLT = [(13,2),(4.5,13.5),(11,13.5),(10,22),(19.5,10.5),(13,10.5),(14,2)]
def lerp(a,b,t): return tuple(int(a[i]+(b[i]-a[i])*t) for i in range(3))
def render(size, small=False):
    S = 8192 if size<=64 else 4096
    S = max(S, size*4)
    margin = 0.03 if small else 0.06
    m = int(S*margin); r = int(S*(0.22 if not small else 0.2))
    mask = Image.new("L",(S,S),0)
    ImageDraw.Draw(mask).rounded_rectangle([m,m,S-m,S-m],r,fill=255)
    top,bot=(103,232,249),(14,150,180)
    grad = Image.new("RGB",(S,S))
    gd = ImageDraw.Draw(grad)
    step=max(1,S//512)
    for y in range(0,S,step):
        t=y/S; gd.rectangle([0,y,S,y+step],fill=lerp(top,(34,211,238),min(t*1.6,1)) if t<.625 else lerp((34,211,238),bot,(t-.625)/.375))
    img = Image.new("RGBA",(S,S),(0,0,0,0)); img.paste(grad,(0,0),mask)
    # bolt: bbox 4.5..19.5 x 2..22 ; scale to fill
    frac = 0.66 if small else 0.58
    sc = S*frac/20.0
    cx,cy = 12*sc, 12*sc
    ox,oy = S/2-12*sc, S/2-12*sc
    pts=[(ox+x*sc, oy+y*sc) for x,y in BOLT]
    if not small:  # soft drop shadow
        sh=Image.new("L",(S,S),0); ImageDraw.Draw(sh).polygon([(x,y+S*.015) for x,y in pts],fill=70)
        from PIL import ImageFilter
        sh=sh.filter(ImageFilter.GaussianBlur(S*.012))
        img.alpha_composite(Image.merge("RGBA",(Image.new("L",(S,S),0),)*3+(ImageChops.multiply(sh,mask),)))
    ImageDraw.Draw(img).polygon(pts,fill=(6,18,26,255))
    return img.resize((size,size),Image.LANCZOS)
sizes=[16,24,32,48,64,128,256,1024]
ims={}
for s in sizes:
    im=render(s, small=s<=32); ims[s]=im
    im.save(os.path.join(out,f"FalseAudio_{s}.png" if s!=1024 else "FalseAudio_1024.png"))
ims[256].save(os.path.join(out,"FalseAudio.ico"),format="ICO",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)],append_images=[])

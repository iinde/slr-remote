"""Close-up views of the USB end: assembled, back half alone, front half alone (both as assembled, and exploded)."""
import sys, render_references as rr
from PIL import Image, ImageDraw, ImageFont
b=rr.b; OUT=rr.Path(sys.argv[1]); OUT.mkdir(parents=True,exist_ok=True); tag=sys.argv[2] if len(sys.argv)>2 else 'usb'
A=rr.ACTUAL['handheld_back']; Fr=rr.ACTUAL['handheld_front']
sets=[('Assembled',[(A,'#8c9eac'),(Fr,'#d0ccc3')]),('Back half only',[(A,'#8c9eac')]),('Front half only',[(Fr,'#d0ccc3')]),('Exploded',[(A,'#8c9eac'),(Fr.translate((0,0,7)),'#d0ccc3')])]
views=[((0,-70,14),(0,1,14),(0,0,1),11),((38,-55,42),(0,1,14),(0,0,1),13),((-30,-40,-18),(0,1,13),(0,0,1),13)]
W,H=800,640; img=Image.new('RGB',(len(sets)*W,len(views)*H+70),'white'); d=ImageDraw.Draw(img)
for i,(lab,items) in enumerate(sets):
    d.text((i*W+20,16),lab,font=ImageFont.truetype(rr.font,38),fill=(30,30,30))
    for j,(pos,foc,up,sc) in enumerate(views):
        img.paste(rr.view(items,pos,foc,up,sc,size=(W,H),display=False),(i*W,70+j*H))
img.save(OUT/f'{tag}_closeup.png'); print(OUT/f'{tag}_closeup.png')

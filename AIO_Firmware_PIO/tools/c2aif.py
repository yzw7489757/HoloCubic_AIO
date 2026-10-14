# Convert lv_font_conv "--format lvgl --no-compress" C output into AIF1 streaming font for SD card.
# Layout (little endian):
#  header 16B: 'AIF1', u16 count, u8 line_height, u8 base_line, u8 bpp, u8 reserved, u16 rec_size,
#              i8 underline_pos, u8 underline_thick, u16 reserved
#  u32 codepoints[count] (sorted ascending)
#  records[count], each rec_size bytes: u16 adv_w(1/16 px), u8 box_w, u8 box_h, i8 ofs_x, i8 ofs_y,
#              u16 bitmap_len, bitmap (LVGL v8 packed bits, no row padding), zero padded
import re,struct,sys
src,out=sys.argv[1],sys.argv[2]
s=open(src).read()
bm=s[s.index("glyph_bitmap[] = {"):s.index("};",s.index("glyph_bitmap[] = {"))]
parts=re.split(r'(?m)^\s*/\* U\+([0-9A-Fa-f]+) .*\*/\s*$',bm)
cps=[];bmps=[]
for i in range(1,len(parts),2):
    cps.append(int(parts[i],16))
    bmps.append(bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{1,2})\b',parts[i+1])))
dsc=re.findall(r'\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}',s)[1:]
assert len(dsc)==len(cps),(len(dsc),len(cps))
assert cps==sorted(cps)
lh=int(re.search(r'\.line_height = (\d+)',s).group(1)); bl=int(re.search(r'\.base_line = (\d+)',s).group(1))
bpp=int(re.search(r'\.bpp = (\d+)',s).group(1))
up=int(re.search(r'\.underline_position = (-?\d+)',s).group(1)); ut=int(re.search(r'\.underline_thickness = (\d+)',s).group(1))
maxb=max(len(b) for b in bmps); rec=8+maxb; rec=(rec+3)//4*4
o=bytearray(b'AIF1'+struct.pack('<HBBBBHbBH',len(cps),lh,bl,bpp,0,rec,up,ut,0))
o+=struct.pack('<%dI'%len(cps),*cps)
for (bi,adv,w,h,ox,oy),b in zip(dsc,bmps):
    r=struct.pack('<HBBbbH',int(adv),int(w),int(h),int(ox),int(oy),len(b))+b
    o+=r+bytes(rec-len(r))
open(out,'wb').write(o)
print(out,"glyphs",len(cps),"rec",rec,"size",len(o),"lh",lh,"bl",bl,"bpp",bpp)

"""Read/write MIDI Box .icn icons: u16 LE width, u16 LE height, then 1bpp rows (MSB first, rows padded to bytes)."""
def load(path):
    b=open(path,'rb').read(); w=b[0]|b[1]<<8; h=b[2]|b[3]<<8; bpr=(w+7)//8
    return w,h,[[ (b[4+r*bpr+(c>>3)]>>(7-(c&7)))&1 for c in range(w)] for r in range(h)]
def save(path,w,h,px):
    bpr=(w+7)//8; out=bytearray([w&255,w>>8,h&255,h>>8])
    for r in range(h):
        row=bytearray(bpr)
        for c in range(w):
            if px[r][c]: row[c>>3]|=1<<(7-(c&7))
        out+=row
    open(path,'wb').write(out)

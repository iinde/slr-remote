"""Copy only the U8g2 fonts ui.cpp uses out of the 40 MB u8g2_fonts.c, so the build is quick."""
import re, sys
src, ui, out = sys.argv[1:4]
names = sorted(set(re.findall(r"u8g2_font_\w+", open(ui).read())))
text = open(src, encoding="latin-1").read()
with open(out, "w", encoding="latin-1") as f:
    f.write('#include "clib/u8g2.h"\n')
    for n in names:
        m = re.search(r"const uint8_t %s\[\d*\] U8G2_FONT_SECTION\(\"%s\"\) =.*?;\n" % (n, n), text, re.S)
        if not m: sys.exit("font not found: " + n)
        f.write(m.group(0))
print("fonts:", ", ".join(names))

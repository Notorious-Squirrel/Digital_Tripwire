from PIL import Image
import sys

if len(sys.argv) < 2:
    print("Usage: python3 convert_image.py <image.png> [output.h]")
    sys.exit(1)

img = Image.open(sys.argv[1]).convert("RGB").resize((200, 200))

out_file = sys.argv[2] if len(sys.argv) > 2 else "squirrel_egg.h"

W, H = img.size
with open(out_file, "w") as f:
    f.write("#ifndef SQUIRREL_EGG_H\n#define SQUIRREL_EGG_H\n\n")
    f.write(f"#define SQUIRREL_EGG_HEIGHT {H}\n#define SQUIRREL_EGG_WIDTH {W}\n\n")
    f.write(f"// array size is {W * H * 2}\n")
    f.write("static const uint16_t squirrel_egg[] PROGMEM = {\n")

    pixels = list(img.getdata())
    for i, (r, g, b) in enumerate(pixels):
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        if i % 16 == 0:
            f.write("  ")
        f.write(f"0x{rgb565:04X}")
        if i < len(pixels) - 1:
            f.write(",")
        if i % 16 == 15 or i == len(pixels) - 1:
            f.write("\n")
        else:
            f.write(" ")

    f.write("};\n\n#endif\n")

print(f"Written {out_file} ({W*H*2} bytes, {W}x{H})")

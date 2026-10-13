"""Generate the 16-tile pixel atlas (256x16, one 16x16 tile per material).

Tile order must match Tiles:: in Source/PixelArt3D/Private/IslandBuilder.cpp.
Row 0 of each tile is its TOP edge (side faces show grass on top).
Run with any Python that has Pillow:  python Tools/gen_atlas.py
"""
import os
import random
from PIL import Image

T = 16
NAMES = ["flat", "grass", "dirt", "brick", "pillar", "wood", "water", "sand",
         "leaves", "roof", "metal", "cobble", "grass_side", "dark_stone", "flowers", "checker"]


def hexc(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def tile(name, rnd):
    px = [[(255, 0, 255)] * T for _ in range(T)]
    pick = lambda cols, w=None: hexc(rnd.choices(cols, weights=w)[0])
    for y in range(T):
        for x in range(T):
            if name == "flat":
                c = (235, 235, 235)
            elif name == "grass":
                c = pick(["#5c9a3a", "#6aab44", "#4d8833", "#7cbb4d"], [70, 12, 12, 6])
            elif name == "dirt":
                c = pick(["#8a6142", "#7a5338", "#9a6e4a", "#6d4a32"], [60, 15, 15, 10])
            elif name == "brick":
                row = y // 4
                off = (row % 2) * 4
                mortar = y % 4 == 3 or (x + off) % 8 == 7
                c = hexc("#59616f") if mortar else pick(["#8c95a3", "#7d8694", "#99a2af"], [70, 15, 15])
            elif name == "pillar":
                edge = x in (0, 15) or y % 8 == 7
                c = hexc("#a39b8a") if edge else pick(["#cdc6b5", "#bdb6a5", "#d9d3c5"], [75, 15, 10])
            elif name == "wood":
                c = hexc("#6b4425") if y % 5 == 4 or (x == 7 and y % 10 < 4) else pick(["#a8743f", "#966636", "#b88350"], [70, 15, 15])
            elif name == "water":
                c = hexc("#a8d4e6") if (x + y * 3) % 11 == 0 else pick(["#3d84ae", "#4a92bb"], [80, 20])
            elif name == "sand":
                c = pick(["#d9c38f", "#cbb27c", "#e4d29f"], [70, 20, 10])
            elif name == "leaves":
                c = pick(["#3e7e39", "#4e9442", "#2f6630", "#5aa64b"], [45, 25, 20, 10])
            elif name == "roof":
                c = hexc("#7a2e26") if y % 4 == 3 else pick(["#b4473b", "#a23e33"], [80, 20])
            elif name == "metal":
                c = hexc("#c9ced6") if y < 3 else pick(["#9aa1ad", "#8a92a0"], [80, 20])
            elif name == "cobble":
                gap = (x + (y // 4) * 3) % 5 == 0 or y % 4 == 0
                c = hexc("#6f6a62") if gap else pick(["#a7a196", "#968f84", "#b3ae a3".replace(" ", "")], [60, 25, 15])
            elif name == "grass_side":
                edge = 3 + (x * 7 + 3) % 3
                if y < edge:
                    c = pick(["#5c9a3a", "#6aab44", "#4d8833"], [70, 18, 12])
                elif y == edge:
                    c = hexc("#4d7d2c") if rnd.random() < 0.5 else pick(["#7a5338", "#8a6142"])
                else:
                    c = pick(["#8a6142", "#7a5338", "#9a6e4a", "#6d4a32"], [60, 15, 15, 10])
            elif name == "dark_stone":
                seam = x % 8 == 7 or y % 8 == 7
                c = hexc("#4f5662") if seam else pick(["#7c8492", "#717987", "#88909d"], [65, 20, 15])
            elif name == "flowers":
                c = pick(["#5c9a3a", "#6aab44", "#4d8833"], [70, 18, 12])
                if (x * 5 + y * 3) % 17 == 0:
                    c = hexc("#f2d14b") if (x + y) % 2 else hexc("#e86a92")
            elif name == "checker":
                # 2x2-texel checker with a 1-texel border: shows texel density at a glance
                if x == 0 or y == 0:
                    c = hexc("#3a3f48")
                else:
                    c = hexc("#e8e2d4") if ((x // 2) + (y // 2)) % 2 == 0 else hexc("#c45a3c")
            px[y][x] = c
    return px


def main():
    img = Image.new("RGB", (T * len(NAMES), T))
    for i, name in enumerate(NAMES):
        rnd = random.Random(1000 + i * 97)
        px = tile(name, rnd)
        for y in range(T):
            for x in range(T):
                img.putpixel((i * T + x, y), px[y][x])
    out = os.path.join(os.path.dirname(__file__), "SourceArt", "T_Atlas.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    img.resize((img.width * 8, img.height * 8), Image.NEAREST).save(out.replace(".png", "_preview.png"))
    print("wrote", out)


if __name__ == "__main__":
    main()

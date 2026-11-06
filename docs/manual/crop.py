# Нарезает скриншоты и спектрограммы теста: python3 crop.py <папка теста> <папка img руководства> <папка картинок README>
import sys
from PIL import Image

src, out, assets = (a.rstrip("/") + "/" for a in sys.argv[1:4])


def save(im, name):
    im.convert("RGB").save(out + name + ".jpg", quality=88)


# координаты — в базовом размере окна 1120×760
TOOLBAR = (14, 14, 1106, 66)
VIEW = (14, 76, 1106, 462)
PANEL = (14, 472, 1106, 746)
LEFT = (14, 472, 578, 746)
RIGHT = (578, 472, 1106, 746)

canvas = Image.open(src + "ui_rubber_canvas.png")
live = Image.open(src + "ui_rubber_live.png")
imprint = Image.open(src + "ui_rubber_imprint.png")
add = Image.open(src + "ui_rubber_add.png")
save(live, "main")
save(Image.open(src + "ui_oxygen_canvas.png"), "main_oxygen")
save(canvas.crop(TOOLBAR), "toolbar")
save(canvas.crop(VIEW), "view_canvas")
save(live.crop(VIEW), "view_live")
save(imprint.crop(VIEW), "view_imprint")
save(add.crop(VIEW), "view_add")
save(Image.open(src + "ui_rubber_empty.png").crop(VIEW), "view_empty")
save(canvas.crop(LEFT), "panel_picture")
save(canvas.crop(RIGHT), "panel_paint")
save(imprint.crop(RIGHT), "panel_mask")
save(add.crop(RIGHT), "panel_add")
save(canvas.crop((14, 690, 600, 746)), "footer")

specs = ["enso", "misty_mountains", "bamboo_organ", "carve_mountains", "sliding_tones", "hidden_signature"]
for s in specs:
    save(Image.open(src + "spec_" + s + ".png"), "spec_" + s)

# картинки README
for n in ["ui_rubber_live", "ui_oxygen_canvas", "ui_rubber_imprint"]:
    Image.open(src + n + ".png").save(assets + n + ".png", optimize=True)
ims = [Image.open(src + "spec_" + s + ".png") for s in specs]
w, h = ims[0].size
grid = Image.new("RGB", (w * 2 + 12, h * 3 + 24), (15, 16, 18))
for i, im in enumerate(ims):
    grid.paste(im, ((i % 2) * (w + 12), (i // 2) * (h + 12)))
grid.save(assets + "spectrograms.png", optimize=True)

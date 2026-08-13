# -*- coding: utf-8 -*-
"""生成 DSHCtl 应用图标 app.ico (仅开发期使用)"""
from PIL import Image, ImageDraw, ImageFont

S = 256
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# 圆角矩形底
r = 56
d.rounded_rectangle([6, 6, S - 6, S - 6], radius=r, fill=(26, 95, 180, 255))
# 顶部高光条
d.rounded_rectangle([6, 6, S - 6, S // 3 + 20], radius=r, fill=(38, 118, 212, 255))

# 白色 "DSH"
try:
    font = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 104)
except OSError:
    font = ImageFont.load_default()
bbox = d.textbbox((0, 0), "DSH", font=font)
w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
d.text(((S - w) / 2 - bbox[0], (S - h) / 2 - bbox[1]), "DSH", font=font, fill=(255, 255, 255, 255))

img.save("Src/MainWindow/app.ico", format="ICO",
         sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("app.ico written")

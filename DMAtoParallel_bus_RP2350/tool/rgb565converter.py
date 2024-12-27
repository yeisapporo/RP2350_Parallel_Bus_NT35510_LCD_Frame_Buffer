from PIL import Image
import numpy as np

# 画像を読み込む
image = Image.open("input_image.png").convert("RGB")
width, height = image.size

# ピクセルデータをRGB565形式に変換
pixels = np.array(image, dtype=np.uint16)  # 画像データを8ビットとして読み込む

# 各チャンネルを16ビット計算用に変換し、RGB565のデータを生成
r = (pixels[:, :, 0] >> 3).astype(np.uint16)  # 赤を5ビットに縮小
g = (pixels[:, :, 1] >> 2).astype(np.uint16)  # 緑を6ビットに縮小
b = (pixels[:, :, 2] >> 3).astype(np.uint16)  # 青を5ビットに縮小

# RGB565の16ビットデータを生成
rgb565 = (r << 11) | (g << 5) | b

# バイナリデータとして保存
with open("output_image.rgb565", "wb") as f:
    for row in rgb565:
        f.write(row.tobytes())  # 1行分を一括でバイナリ出力

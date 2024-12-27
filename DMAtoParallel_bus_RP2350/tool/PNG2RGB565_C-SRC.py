from PIL import Image
import numpy as np


def binary_to_c_array(binary_data, width, height):
    """
    Convert binary data to C source code representation.

    Args:
        binary_data (bytes): Binary data in RGB565 format.
        width (int): Width of the image.
        height (int): Height of the image.

    Returns:
        str: Formatted C source code as a string.
    """
    # Convert binary data to a list of 8-bit integers
    data = [byte for byte in binary_data]

    # Format the data into lines with 16 values each
    lines = []
    for i in range(0, len(data), 32):  # 32 values per line
        line = ", ".join(f"0x{value:02x}" for value in data[i : i + 32])
        lines.append(line)

    # Combine the lines into a C array format
    c_array = (
        f"const uint8_t image_data[] = {{\n"
        f"// Image dimensions: {width}x{height}\n"
        f"    " + ",\n    ".join(lines) + "\n"  # Indent each line appropriately
        f"}};"
    )

    return c_array


# Load image and convert to RGB565
image = Image.open("ysiiop.png").convert("RGB")
width, height = image.size
pixels = np.array(image, dtype=np.uint16)

# Convert to RGB565 format
r = (pixels[:, :, 0] >> 3).astype(np.uint16)
g = (pixels[:, :, 1] >> 2).astype(np.uint16)
b = (pixels[:, :, 2] >> 3).astype(np.uint16)
rgb565 = (r << 11) | (g << 5) | b

# Save binary RGB565 data
binary_data = b"".join(row.tobytes() for row in rgb565)
with open("output_image.rgb565", "wb") as f:
    f.write(binary_data)

# Generate C code
c_code = binary_to_c_array(binary_data, width, height)
print(c_code)
with open("image_data.h", "w") as f:
    f.write(c_code)

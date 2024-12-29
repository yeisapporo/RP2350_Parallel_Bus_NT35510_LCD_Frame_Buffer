def file_to_hex_array(file_path, output_var_name="data", output_file="data.h"):
    try:
        with open(file_path, "rb") as file:
            file_content = file.read()

        hex_array = [f"0x{byte:02X}" for byte in file_content]

        # Format as C uint8_t array
        hex_array_lines = []
        line_length = 16  # Number of elements per line

        for i in range(0, len(hex_array), line_length):
            line = ", ".join(hex_array[i : i + line_length])
            hex_array_lines.append(line)

        formatted_array = f"const uint8_t {output_var_name}[] = {{\n    " + ",\n    ".join(hex_array_lines) + "\n};"

        # Save to output file
        with open(output_file, "w") as output:
            output.write(f"#ifndef _DATA_H_\n#define _DATA_H_\n\n{formatted_array}\n\n#endif // _DATA_H_\n")

        return f"Hex array saved to {output_file}"

    except FileNotFoundError:
        return f"Error: File '{file_path}' not found."
    except Exception as e:
        return f"An error occurred: {e}"


# Example usage
if __name__ == "__main__":
    file_path = input("Enter the file path: ").strip()
    output_var_name = input("Enter the output variable name (default: data): ").strip() or "data"
    output_file = input("Enter the output file name (default: data.h): ").strip() or "data.h"

    result = file_to_hex_array(file_path, output_var_name, output_file)
    print(result)

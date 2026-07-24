import struct
import json
import sys

path = sys.argv[1]

with open(path, "rb") as f:
    data = f.read()

# GLB header
magic, version, length = struct.unpack_from("<III", data, 0)
assert magic == 0x46546C67  # "glTF"

# first chunk
chunk_length, chunk_type = struct.unpack_from("<II", data, 12)
assert chunk_type == 0x4E4F534A  # JSON

json_data = data[20:20+chunk_length]

gltf = json.loads(json_data)

print(json.dumps(gltf, indent=2))

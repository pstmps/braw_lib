import bmd_metadata


filename = "/Users/stiebing/Documents/scripting_base/metasort/temp_dir/20231103-generics/_incoming_bup/A003_04262223/A003_04262223_C022.braw"

metadata = bmd_metadata.read_metadata(filename)

for name, value in metadata.items():
    print(name, value)
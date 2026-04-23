import os
import re

INPUT_FILE = os.path.join(os.path.dirname(__file__), '..', '..', 'rocket_memory.btor2pp')
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), 'output_files')

# Ensure output directory exists
os.makedirs(OUTPUT_DIR, exist_ok=True)

with open(INPUT_FILE, 'r') as f:
    lines = f.readlines()

module_re = re.compile(r"; ==== Module '([A-Za-z0-9_]+)' ====")
end_module_re = re.compile(r"; ==== End of Module '([A-Za-z0-9_]+)' ====")

modules = []
current_module = None
current_lines = []

for line in lines:
    mod_start = module_re.match(line)
    mod_end = end_module_re.match(line)
    if mod_start:
        if current_module:
            modules.append((current_module, current_lines))
        current_module = mod_start.group(1)
        current_lines = [line]
    elif mod_end and current_module:
        current_lines.append(line)
        modules.append((current_module, current_lines))
        current_module = None
        current_lines = []
    elif current_module:
        current_lines.append(line)

# Write each module to its own file, removing comment lines
for mod_name, mod_lines in modules:
    out_path = os.path.join(OUTPUT_DIR, f"{mod_name}.btor2pp")
    with open(out_path, 'w') as out_f:
        for l in mod_lines:
            if not l.lstrip().startswith(';'):
                out_f.write(l)

print(f"Extracted {len(modules)} modules to {OUTPUT_DIR} (comments removed)")

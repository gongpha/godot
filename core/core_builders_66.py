from collections import OrderedDict
from io import TextIOWrapper

import methods

def make_66_header(target, source, env):
    SECTIONS = {
        "Built for": "BUILT_FOR",
        "Developers": "DEVELOPERS"
    }

    source_notice_text = source[0]
    source_readme = source[1]

    buffer_notice_text = methods.get_buffer(str(source_notice_text))
    buffer_readme = methods.get_buffer(str(source_readme))

    reading = False

    escaped = methods.to_escaped_cstring(buffer_notice_text.decode().strip())

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(f"inline constexpr const char *NOTICE_TEXT = \" {escaped}\";\n")

        # build for
        def close_section():
            file.write("\tnullptr,\n};\n\n")

        for line in buffer_readme.decode().splitlines():
            if line.startswith("    ") and reading:
                file.write(f'\t"{methods.to_escaped_cstring(line).strip()}",\n')
            elif line.startswith("## "):
                if reading:
                    close_section()
                    reading = False
                section = SECTIONS[line[3:].strip()]
                if section:
                    file.write(f"inline constexpr const char *{section}[] = {{\n")
                    reading = True

        if reading:
            close_section()

    
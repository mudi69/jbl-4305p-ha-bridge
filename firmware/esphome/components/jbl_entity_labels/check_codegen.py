"""Generate in memory and check labels without compiling, uploading, or printing secrets."""

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import logging
from pathlib import Path
import re

import esphome.codegen as cg
from esphome.__main__ import generate_cpp_contents
from esphome.config import read_config
from esphome.core import CORE

CORE.reset()
CORE.config_path = Path(__file__).resolve().parents[2] / "jbl-4305p-bridge.yaml"
logging.disable(logging.CRITICAL)
with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
    config = read_config({}, skip_external_update=True)
    assert config is not None, "Configuration validation failed; run the normal safe config check."
    CORE.config = config
    generate_cpp_contents(config)

code = CORE.cpp_main_section
entries = config["jbl_entity_labels"]["entries"]
string_literal = r'("(?:[^"\\]|\\.)*")'
for entry in entries:
    entity_id = str(entry["entity_id"])
    original = re.search(
        rf'App\.register_\w+\({entity_id}, {string_literal}, (\d+)(?:UL)?, (\d+)(?:UL)?\)', code
    )
    translated = re.search(
        rf'{entity_id}->configure_entity_\({string_literal}, (\d+)(?:UL)?, (\d+)(?:UL)?\)', code
    )
    assert original and translated, f"Missing registration or label for {entity_id}"
    assert original.group(2, 3) == translated.group(2, 3), f"Identity/flags changed: {entity_id}"
    assert translated.group(1) == str(cg.safe_exp(entry["name"])), entity_id
    assert len(re.findall(rf'App\.register_(?:button|binary_sensor|text_sensor|event)\({entity_id},', code)) == 1, f"Duplicate registration: {entity_id}"
    print(f'{entity_id}: {entry["name"]}; key={original.group(2)}; fields={original.group(3)} unchanged')

print(f"PASS: {len(entries)} labels preserve original entity keys/flags; no duplicate registration.")

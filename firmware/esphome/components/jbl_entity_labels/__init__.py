"""Translate API entity names while retaining this bridge's existing entity keys."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ENTITY_ID, CONF_NAME, __version__
from esphome.core import CORE, CoroPriority, coroutine_with_priority
from esphome.core.entity_helpers import finalize_entity_strings

CONF_ENTRIES = "entries"


def _validate(config):
    # These private codegen fields are deliberately pinned to the installed release.
    if __version__ != "2026.9.1":
        raise cv.Invalid("jbl_entity_labels requires ESPHome 2026.9.1")
    entries = config[CONF_ENTRIES]
    if len({str(entry[CONF_ENTITY_ID]) for entry in entries}) != len(entries):
        raise cv.Invalid("Each entity_id may have only one label")
    if len({entry[CONF_NAME] for entry in entries}) != len(entries):
        raise cv.Invalid("Entity labels must be distinct")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ENTRIES): cv.All(
                cv.ensure_list(
                    cv.Schema(
                        {
                            cv.Required(CONF_ENTITY_ID): cv.use_id(cg.EntityBase),
                            cv.Required(CONF_NAME): cv.All(cv._validate_entity_name, cv.Length(min=1)),
                        }
                    )
                ),
                cv.Length(min=1),
            )
        }
    ),
    _validate,
)


@coroutine_with_priority(CoroPriority.FINAL - 1)
async def to_code(config):
    for entry in config[CONF_ENTRIES]:
        entity = await cg.get_variable(entry[CONF_ENTITY_ID])
        path = CORE.config.get_path_for_id(entry[CONF_ENTITY_ID])[:-1]
        original = CORE.config.get_config_for_path(path)
        translated = original.copy()
        translated["_entity_name"] = entry[CONF_NAME]
        translated.pop("_entity_register_method", None)
        # Emit configure_entity_ in friend setup(), preserving the original hash and flags.
        finalize_entity_strings(entity, translated)

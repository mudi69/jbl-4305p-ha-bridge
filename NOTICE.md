# Licensing and source provenance

This project contains modified ESPHome components. Preserve the root `LICENSE` and component-level `LICENSE` files when redistributing source or firmware.

The ESPHome licensing split is retained: C/C++ runtime code and project runtime additions are provided under GPLv3; Python, configuration, documentation and other project additions are provided under MIT unless a file states otherwise. Third-party files keep their own copyright notices and licenses. No JBL trademarks or vendor firmware ownership is claimed; this is an independent interoperability project.

| Dependency | Source / pinned reference | License information |
| --- | --- | --- |
| ESPHome | https://github.com/esphome/esphome/tree/2026.9.1 ; vendored component provenance records commit `0b2b16e664ecfc0add37f3435db1c2140b8a687e` | root LICENSE and component LICENSE files: GPLv3 runtime / MIT other code |
| ESP-IDF | https://github.com/espressif/esp-idf/tree/v5.5.5 | `licenses/ESP-IDF-LICENSE`; bundled components retain their own notices |
| ESPHome libsodium port | https://github.com/esphome-libs/libsodium | `licenses/esphome-libsodium-LICENSE` and upstream component metadata |
| libsodium | included by the port above | `licenses/libsodium-LICENSE` |
| noise-c | https://github.com/esphome/noise-c | `licenses/noise-c-LICENSE` |

Exact local override changes and source hashes are recorded in each `provenance.json`. Release manifest hashes identify the precise project inputs. Runtime dependencies are resolved by the fixed ESPHome/ESP-IDF versions; dependency metadata is included with the release. The repository provides the modified runtime source and build configuration alongside the corresponding example binaries. Personal deployment credentials, raw captures and NVS backups are not source-code prerequisites and are deliberately excluded.

`requirements-build-windows.lock.txt` is an observed Windows/Python 3.13 dependency snapshot, not a universal cross-platform lock. Use `requirements-build.txt` and pinned YAML versions for the supported build procedure.

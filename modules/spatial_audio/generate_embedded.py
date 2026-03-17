#!/usr/bin/env python3
"""Generates spatial_audio_embedded.gen.h from the GDScript source files."""

import os

MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(MODULE_DIR, "spatial_audio_embedded.gen.h")

FILES_TO_EMBED = [
    ("plugin.gd", "SPATIAL_AUDIO_PLUGIN_GD"),
    ("spatial_audio_player_3d.gd", "SPATIAL_AUDIO_PLAYER_GD"),
    ("acoustic_body.gd", "ACOUSTIC_BODY_GD"),
    ("acoustic_material.gd", "ACOUSTIC_MATERIAL_GD"),
    ("spatial_reflection_navigation_agent_3d.gd", "SPATIAL_REFLECTION_NAV_GD"),
]

PLUGIN_CFG = """[plugin]

name="Spatial Audio Extended"
description="Built-in spatial audio with occlusion, reverb, and acoustic materials."
author="danikakes"
version="3.0.1"
script="plugin.gd"
"""


def escape_for_cpp(text):
    """Escape a string for embedding in a C++ raw string literal."""
    # Use R"DELIM(...)DELIM" style - just need to make sure DELIM doesn't appear in text
    return text


def main():
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("// Auto-generated - do not edit.")
    lines.append("// Contains embedded spatial audio GDScript files.")
    lines.append("")

    # Embed plugin.cfg
    lines.append('static const char *SPATIAL_AUDIO_PLUGIN_CFG = R"EMBED(')
    lines.append(PLUGIN_CFG.strip())
    lines.append(')EMBED";')
    lines.append("")

    for filename, varname in FILES_TO_EMBED:
        filepath = os.path.join(MODULE_DIR, filename)
        if not os.path.exists(filepath):
            print(f"Warning: {filepath} not found, skipping")
            continue
        with open(filepath, "r") as f:
            content = f.read()

        lines.append(f'static const char *{varname} = R"EMBED(')
        lines.append(content.rstrip())
        lines.append(')EMBED";')
        lines.append("")

    with open(OUTPUT, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Generated {OUTPUT}")


if __name__ == "__main__":
    main()

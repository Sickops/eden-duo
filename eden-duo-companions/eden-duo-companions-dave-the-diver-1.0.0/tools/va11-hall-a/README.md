# VA-11 Hall-A generators

The page itself (`packages/VA11HallA/dualscreen/manifest.json`) is maintained by hand. These
scripts make the generated art, Jill's face table and the guest load plan. Pillow is needed for
the art scripts; `gen_load_plan.py --verify` needs `lz4`.

| Script | Writes |
|---|---|
| `gen_bar_counter.py` | `bar_counter.png`, `bar_shadow.png`, `bar_top_dim.png`: the stand-by screen's counter, Jill's shadow, the light on the top |
| `gen_neon_dim.py` | `bg_neon_dim.png`, `bg_neon_tube.png`, `bg_neon_bloom.png`, `neon_mote.png`: the bar window's neon layers and dust |
| `gen_transitions.py` | `dither_1..16.png`: the pixel dissolve between Jill's bar and the doodles |
| `gen_easter_egg.py` | `egg_heart.png`, `egg_drawing.png`: the heart under Age and the note's drawing (from the artist's `art/lil_drawing.png`) |
| `gen_jill_faces.py` | `native/modules/va11_jill_faces.h` from `native/modules/va11_jill_faces/*.tsv`; `--check <romfs scripts/eng>` prints each entry beside its line |
| `gen_load_plan.py --verify <main>` | `guest/32D80C9A.../load-plan.json`, checked byte for byte against the player's own exefs `main` (base v0) |
| `build_ids.txt` | the `--build-id` list passed to `tools/build_dualscreen_package.py` |

All art scripts reproduce the committed PNGs byte for byte.

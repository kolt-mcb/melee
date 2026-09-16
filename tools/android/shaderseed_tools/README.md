# Shader seed

`tools/android/shaderseed/` ships in the APK's assets: one key set per
character (`c<ckind>.txt`), per stage (`s<stkind>.txt`), and `common.txt`.
At a match's loading screen the port compiles the union for the lineup,
so READY has nothing left to build (see pc_shc_prepare_match).

Regenerate after a change to the shader template or the specialisation
key (the keys are the specialisation values, so either invalidates them):

    tools/android/shaderseed_tools/seed_sweep.sh     # ~40 min, desktop
    python3 tools/android/shaderseed_tools/derive_seed.py \
        <per-lineup dir from the sweep> tools/android/shaderseed

`lineups.txt` is every character on three stages plus one pair on every
stage, drawn from the matrix suite's known-good combinations.

#!/usr/bin/env bash
set -euo pipefail
# Linux development build. No shared CMake or host changes.
sdk_root="${1:?SDK root required}"
output_dir="${2:?Output directory required}"
source_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
mkdir -p -- "$output_dir"
cat > "$output_dir/belmont.exports" <<'EXPORTS'
{ global: eden_dsmod_get_module; eden_dsmod_get_extensions; eden_dsmod_get_font_extensions; eden_dsmod_get_data_extensions; local: *; };
EXPORTS
compiler="${CXX:-c++}"
includes=(-I"$sdk_root/src" -I"$sdk_root/externals/bc_decoder")
prefix_maps=(-ffile-prefix-map="$sdk_root"=runtime -ffile-prefix-map="$source_dir"=modules)
common=(-std=c++20 -O2 -Wall -Wextra -Werror -pthread "${includes[@]}" "${prefix_maps[@]}")
"$compiler" -std=c++20 -O2 -fPIC -fvisibility=hidden -c \
  "${prefix_maps[@]}" \
  "$sdk_root/externals/bc_decoder/bc_decoder.cpp" -o "$output_dir/belmont-bc.o"
sources=("$source_dir/0100BA4026E94000.cpp" "$source_dir/belmont_assets.cpp" \
  "$source_dir/belmont_map_assets.cpp" "$source_dir/belmont_map_tiles.cpp" \
  "$source_dir/belmont_map_fog.cpp" "$source_dir/belmont_map_render.cpp" "$source_dir/belmont_world_map.cpp" \
  "$source_dir/belmont_map_labels.cpp" "$source_dir/belmont_map_landmarks.cpp" \
  "$source_dir/belmont_equipment_assets.cpp" "$source_dir/belmont_landmark_render.cpp" "$source_dir/belmont_objective_labels.cpp" "$source_dir/chained_echoes_unity.cpp")
objects=("$output_dir/belmont-bc.o")
for source in "${sources[@]}"; do
  object="$output_dir/$(basename -- "${source%.cpp}").o"
  "$compiler" "${common[@]}" -fPIC -fvisibility=hidden -c "$source" -o "$object"
  objects+=("$object")
done
"$compiler" "${common[@]}" -shared "${objects[@]}" \
  -llz4 -Wl,--no-undefined -Wl,--version-script="$output_dir/belmont.exports" \
  -o "$output_dir/0100BA4026E94000.so"
"$compiler" "${common[@]}" "$source_dir/belmont_reader_test.cpp" "${objects[@]}" \
  -llz4 -o "$output_dir/belmont_reader_test"
"$output_dir/belmont_reader_test"
for test in belmont_objectives belmont_equipment belmont_equipment_stats belmont_presentation belmont_pins belmont_inventory belmont_equipment_control belmont_native_pins belmont_map_control belmont_helper_mailbox belmont_helper_refs; do
  "$compiler" "${common[@]}" "$source_dir/${test}_test.cpp" -o "$output_dir/${test}_test"
  "$output_dir/${test}_test"
done
"${STRIP:-strip}" --strip-all "$output_dir/0100BA4026E94000.so"

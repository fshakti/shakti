#!/usr/bin/env python3
"""Generate or verify benchmarks/manifest.json against BUILTINS[] in src/builtin.c."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILTIN_C = ROOT / "src" / "builtin.c"
MANIFEST = ROOT / "benchmarks" / "manifest.json"

SKIP_DEFAULTS: dict[str, str] = {
    "input": "interactive stdin",
    "readline": "interactive stdin",
    "wait": "interactive stdin blocking",
    "input_get_hz": "input side channel",
    "input_set_hz": "input side channel",
    "input_get_x": "input side channel",
    "input_get_y": "input side channel",
    "input_get_wheel": "input side channel",
    "input_set_x": "input side channel",
    "input_set_y": "input side channel",
    "input_set_wheel": "input side channel",
    "input_get_qwerty": "input side channel",
    "input_set_own_gui": "input side channel",
    "input_qwerty_reload": "input side channel",
    "input_key_down": "input side channel",
    "input_keys_clear": "input side channel",
    "sh": "shell execution; environment-dependent",
    "machine": "host introspection with disk benchmark; environment-dependent",
    "talk_listen": "requires microphone and speech API",
    "talk_set_locale": "requires talk module hardware",
    "talk_set_model": "requires talk module hardware",
    "synth_open": "requires GUI/audio display",
    "synth_close": "requires GUI/audio display",
    "synth_alive": "requires GUI/audio display",
    "synth_tick": "requires GUI/audio display",
    "synth_set_steps": "requires GUI/audio display",
    "synth_steps": "requires GUI/audio display",
    "synth_set_metro": "requires GUI/audio display",
    "synth_metro_on": "requires GUI/audio display",
    "synth_set_metro_sound": "requires GUI/audio display",
    "synth_metro_sound": "requires GUI/audio display",
    "synth_set_mute": "requires GUI/audio display",
    "synth_mute_on": "requires GUI/audio display",
    "synth_note_on": "requires GUI/audio display",
    "synth_note_off": "requires GUI/audio display",
    "synth_set_bpm": "requires GUI/audio display",
    "synth_bpm": "requires GUI/audio display",
    "synth_set_level": "requires GUI/audio display",
    "synth_level": "requires GUI/audio display",
    "synth_set_cutoff": "requires GUI/audio display",
    "synth_cutoff": "requires GUI/audio display",
    "synth_set_reso": "requires GUI/audio display",
    "synth_reso": "requires GUI/audio display",
    "synth_set_seq_row": "requires GUI/audio display",
    "synth_play": "requires GUI/audio display",
    "synth_playing": "requires GUI/audio display",
    "synth_mouse_press": "requires GUI/audio display",
    "synth_mouse_release": "requires GUI/audio display",
    "synth_set_viz": "requires GUI/audio display",
    "synth_viz_mode": "requires GUI/audio display",
    "synth_vu": "requires GUI/audio display",
    "synth_set_row_note": "requires GUI/audio display",
    "synth_row_note": "requires GUI/audio display",
    "synth_looper_rec": "requires GUI/audio display",
    "synth_looper_play": "requires GUI/audio display",
    "synth_looper_clear": "requires GUI/audio display",
    "synth_looper_overdub": "requires GUI/audio display",
    "synth_looper_rec_on": "requires GUI/audio display",
    "synth_looper_play_on": "requires GUI/audio display",
    "synth_looper_has_loop": "requires GUI/audio display",
    "synth_set_tuning": "requires GUI/audio display",
    "synth_tuning": "requires GUI/audio display",
    "ipc_listen": "requires networked IPC server process",
    "ipc_accept": "requires networked IPC server process",
    "ipc_connect": "requires networked IPC server process",
    "ipc_send": "benchmarked via ipc_uds_roundtrip_bench / ipc_tcp_roundtrip_bench",
    "ipc_recv": "benchmarked via ipc_uds_roundtrip_bench / ipc_tcp_roundtrip_bench",
    "ipc_recv_nowait": "requires active IPC connection; covered by tests",
    "ipc_set_nonblock": "requires active IPC connection; covered by tests",
    "ipc_poll": "requires active IPC connection; covered by tests",
    "ipc_close": "part of IPC round-trip benchmarks",
    "rest_post": "benchmarked via rest_client_local_get_bench",
    "rest_put": "benchmarked via rest_client_local_get_bench",
    "rest_delete": "benchmarked via rest_client_local_get_bench",
    "rest_request": "benchmarked via rest_client_local_get_bench",
    "rest_accept": "benchmarked via rest_server_read_write_bench",
    "rest_close": "part of rest_server_read_write_bench",
    "pcm_open": "requires audio hardware",
    "pcm_write": "requires audio hardware",
    "pcm_close": "requires audio hardware",
    "gfx_open": "requires GUI display",
    "gfx_close": "requires GUI display",
    "gfx_alive": "requires GUI display",
    "gfx_available": "requires GUI display",
    "gfx_tick": "requires GUI display",
    "gfx_sync_keys": "requires GUI display",
    "gfx_clear": "requires GUI display",
    "gfx_fill_rect": "requires GUI display",
    "gfx_line": "requires GUI display",
    "gfx_fill_circle": "requires GUI display",
    "gfx_click_pending": "requires GUI display",
    "gfx_click_x": "requires GUI display",
    "gfx_click_y": "requires GUI display",
    "gfx_consume_click": "requires GUI display",
}

ACTIVE_CASES: dict[str, list[str]] = {
    "print": ["print_loop"],
    "len": ["len_str"],
    "range": ["range_1m"],
    "type": ["type_check"],
    "int": ["int_conv"],
    "float": ["float_conv"],
    "str": ["str_conv"],
    "list": ["list_from_ivec"],
    "bool": ["bool_conv"],
    "sum": ["sum_1m"],
    "avg": ["avg_1m"],
    "min": ["min_1m"],
    "max": ["max_1m"],
    "dot": ["dot_1m"],
    "mmul": ["mmul_f64", "mmul_f256", "mmul_i4"],
    "abs": ["abs_call", "abs_juxtapose"],
    "sqrt": ["sqrt_vec"],
    "floor": ["floor_vec"],
    "ceil": ["ceil_vec"],
    "exp": ["exp_vec"],
    "log": ["log_vec"],
    "sin": ["sin_vec"],
    "cos": ["cos_vec"],
    "tan": ["tan_vec"],
    "sort": ["sort_10k"],
    "reverse": ["reverse_10k"],
    "zip": ["zip_100k"],
    "enumerate": ["enumerate_100k"],
    "map": ["map_10k"],
    "filter": ["filter_10k"],
    "table": ["table_build"],
    "columns": ["columns_table"],
    "shape": ["shape_table"],
    "head": ["head_table"],
    "tail": ["tail_table"],
    "group_sum": ["group_sum_bench"],
    "append": ["append_10k"],
    "pop": ["pop_10k"],
    "keys": ["keys_dict"],
    "values": ["values_dict"],
    "load": ["load_csv"],
    "save": ["save_csv"],
    "repr": ["repr_list"],
    "clock": ["clock_call"],
    "timer": ["timer_call"],
    "read": ["read_file"],
    "write": ["write_file"],
    "readlines": ["readlines_file"],
    "listdir": ["listdir_tmp"],
    "walk": ["walk_tmp"],
    "stat": ["stat_file"],
    "path_join": ["path_join_bench"],
    "path_exists": ["path_exists_bench"],
    "path_isdir": ["path_isdir_bench"],
    "path_isfile": ["path_isfile_bench"],
    "path_basename": ["path_basename_bench"],
    "path_dirname": ["path_dirname_bench"],
    "path_splitext": ["path_splitext_bench"],
    "getcwd": ["getcwd_bench"],
    "mkdir": ["mkdir_bench"],
    "getenv": ["getenv_bench"],
    "re_findall": ["re_findall_bench"],
    "re_sub": ["re_sub_bench"],
    "re_match": ["re_match_bench"],
    "re_split": ["re_split_bench"],
    "json_loads": ["json_loads_bench"],
    "json_dumps": ["json_dumps_bench"],
    "json_load": ["json_load_bench"],
    "json_dump": ["json_dump_bench"],
    "sorted": ["sorted_10k"],
    "any": ["any_100k"],
    "all": ["all_100k"],
    "isinstance": ["isinstance_bench"],
    "hasattr": ["hasattr_bench"],
    "getattr": ["getattr_bench"],
    "chr": ["chr_bench"],
    "ord": ["ord_bench"],
    "hex": ["hex_bench"],
    "dict": ["dict_bench"],
    "ktable": ["ktable_bench"],
    "set": ["set_bench"],
    "next": ["next_bench"],
    "assert": ["assert_bench"],
    "datetime": ["datetime_bench"],
    "format_datetime": ["format_datetime_bench"],
    "date": ["date_bench"],
    "format_date": ["format_date_bench"],
    "time_ms": ["time_ms_bench"],
    "format_time": ["format_time_bench"],
    "save_context": ["save_context_bench"],
    "load_context": ["load_context_bench"],
    "ipc_shm_open": ["ipc_shm_cycle_bench"],
    "ipc_shm_close": ["ipc_shm_cycle_bench"],
    "ipc_rdma_available": ["ipc_rdma_available_bench"],
    "synth_load_sample": [
        "synth_load_sample_24bit",
        "synth_load_sample_16bit",
    ],
    "synth_sample_loaded": ["synth_load_sample_24bit", "synth_load_sample_16bit"],
    "synth_sample_name": ["synth_load_sample_24bit", "synth_load_sample_16bit"],
    "rest_get": ["rest_client_local_get_bench"],
    "rest_listen": ["rest_server_read_write_bench"],
    "rest_read": ["rest_server_read_write_bench"],
    "rest_write": ["rest_server_read_write_bench"],
    "graph_create": [
        "graph_add_bench",
        "graph_query_bench",
        "graph_path_bench",
        "graph_neighbors_bench",
        "graph_from_table_bench",
        "graph_clear_bench",
    ],
    "graph_add": ["graph_add_bench"],
    "graph_query": ["graph_query_bench"],
    "graph_neighbors": ["graph_neighbors_bench"],
    "graph_path": ["graph_path_bench"],
    "graph_from_table": ["graph_from_table_bench"],
    "graph_to_table": ["graph_query_bench"],
    "graph_count": ["graph_add_bench", "graph_from_table_bench"],
    "graph_clear": ["graph_clear_bench"],
}


def parse_builtins(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    for macro in ("SHAKTI_HAVE_LISSEN", "SHAKTI_HAVE_GOVEE"):
        text = re.sub(rf"#ifdef {macro}\n(.*?)#endif\n", "", text, flags=re.S)
    m = re.search(r"static const char \*BUILTINS\[\] = \{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"could not find BUILTINS[] in {path}")
    names = re.findall(r'"([^"]+)"', m.group(1))
    if not names:
        raise SystemExit("BUILTINS[] is empty")
    return names


def default_manifest(builtins: list[str]) -> dict:
    entries: dict[str, dict] = {}
    for name in builtins:
        if name in SKIP_DEFAULTS:
            entries[name] = {"status": "skip", "skip_reason": SKIP_DEFAULTS[name]}
        else:
            entries[name] = {"status": "active", "cases": ACTIVE_CASES.get(name, [f"{name}_bench"])}
    return {"version": 1, "builtins": entries}


def load_manifest() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def check_manifest(builtins: list[str], manifest: dict) -> int:
    errors: list[str] = []
    entries = manifest.get("builtins", {})
    for name in builtins:
        if name not in entries:
            errors.append(f"missing builtin entry: {name}")
            continue
        entry = entries[name]
        status = entry.get("status")
        if status == "skip":
            if not entry.get("skip_reason"):
                errors.append(f"{name}: skip entry missing skip_reason")
        elif status == "active":
            cases = entry.get("cases")
            if not cases:
                errors.append(f"{name}: active entry missing cases")
        else:
            errors.append(f"{name}: invalid status {status!r}")
    extra = sorted(set(entries) - set(builtins))
    for name in extra:
        errors.append(f"manifest entry not in BUILTINS[]: {name}")
    if errors:
        for err in errors:
            print(f"manifest check failed: {err}", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="verify manifest covers all builtins")
    ap.add_argument("--write", action="store_true", help="write default manifest.json")
    args = ap.parse_args()
    builtins = parse_builtins(BUILTIN_C)
    if args.write:
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        MANIFEST.write_text(json.dumps(default_manifest(builtins), indent=2) + "\n", encoding="utf-8")
        print(f"wrote {MANIFEST}")
        return 0
    if not MANIFEST.exists():
        print(f"manifest missing: {MANIFEST} (run with --write)", file=sys.stderr)
        return 1
    manifest = load_manifest()
    return check_manifest(builtins, manifest)


if __name__ == "__main__":
    raise SystemExit(main())

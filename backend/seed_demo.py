"""
Demo seed script — edit DEVICES and REPORTS below, then run:
    python seed_demo.py

Timestamps are seconds-since-epoch. Use the helper at the bottom
to convert "hours ago" offsets to unix timestamps if that's easier.
"""

import secrets
import time

import db as _db
from config import DATABASE_PATH

# ---------------------------------------------------------------------------
# EDIT HERE — Devices
# Each entry is just a label string. An API key is auto-generated.
# ---------------------------------------------------------------------------
DEVICES = [
    "Bike 01",
    "Bike 02",
    "Bike 03",
    "Bike 04",
    "Bike 05",
    ("Bike ESP32", "767aa98087416fc12491a1962c68ef91656249dc00c3646615140faab25a4c8f")
]

# ---------------------------------------------------------------------------
# EDIT HERE — Reports
# Fields:
#   device  : label from DEVICES list above
#   lat     : latitude  (float)
#   lon     : longitude (float)
#   score   : severity 1–100
#   ts      : unix timestamp — use hours_ago(n) for relative times,
#             or just put a raw integer
# ---------------------------------------------------------------------------
def hours_ago(n):
    return int(time.time()) - n * 3600

REPORTS = [

    # --- cluster 1: high variance, 4 separate reporters, 10 reports, mean of 50 - near 11th and rolla st
    {"device": "Bike 01", "lat": 37.95236, "lon": -91.77284, "score": 90, "ts": hours_ago(10)},
    {"device": "Bike 01", "lat": 37.95236, "lon": -91.77284, "score": 20, "ts": hours_ago(2)},
    {"device": "Bike 02", "lat": 37.95236, "lon": -91.77284, "score": 10, "ts": hours_ago(10)},
    {"device": "Bike 02", "lat": 37.95236, "lon": -91.77284, "score": 65, "ts": hours_ago(2)},
    {"device": "Bike 03", "lat": 37.95236, "lon": -91.77284, "score": 50, "ts": hours_ago(10)},
    {"device": "Bike 03", "lat": 37.95236, "lon": -91.77284, "score": 80, "ts": hours_ago(2)},
    {"device": "Bike 04", "lat": 37.95236, "lon": -91.77284, "score": 95, "ts": hours_ago(10)},
    {"device": "Bike 04", "lat": 37.95236, "lon": -91.77284, "score": 50, "ts": hours_ago(2)},
    {"device": "Bike 05", "lat": 37.95236, "lon": -91.77284, "score": 35, "ts": hours_ago(10)},
    {"device": "Bike 05", "lat": 37.95236, "lon": -91.77284, "score": 5, "ts": hours_ago(2)},


    # --- cluster 2: high variance, 5 separate reporters, 10 reports, mean of 60 - out front of runciple games
    {"device": "Bike 01", "lat": 37.952930, "lon": -91.771533, "score": 40, "ts": hours_ago(10)},
    {"device": "Bike 01", "lat": 37.952930, "lon": -91.771533, "score": 80, "ts": hours_ago(2)},
    {"device": "Bike 02", "lat": 37.952930, "lon": -91.771533, "score": 100, "ts": hours_ago(10)},
    {"device": "Bike 02", "lat": 37.952930, "lon": -91.771533, "score": 20, "ts": hours_ago(2)},
    {"device": "Bike 03", "lat": 37.952930, "lon": -91.771533, "score": 50, "ts": hours_ago(10)},
    {"device": "Bike 03", "lat": 37.952930, "lon": -91.771533, "score": 75, "ts": hours_ago(2)},
    {"device": "Bike 04", "lat": 37.952930, "lon": -91.771533, "score": 60, "ts": hours_ago(10)},
    {"device": "Bike 04", "lat": 37.952930, "lon": -91.771533, "score": 45, "ts": hours_ago(2)},
    {"device": "Bike 05", "lat": 37.952930, "lon": -91.771533, "score": 60, "ts": hours_ago(10)},
    {"device": "Bike 05", "lat": 37.952930, "lon": -91.771533, "score": 70, "ts": hours_ago(2)},


    # --- cluster 3: low variance, 4 separate reporters, 8 reports, mean of 60 - at the crosswalk at the northwest corner of 12th where it turns to rolla st.
    {"device": "Bike 01", "lat": 37.953162, "lon": -91.772478, "score": 50, "ts": hours_ago(10)},
    {"device": "Bike 01", "lat": 37.953162, "lon": -91.772478, "score": 70, "ts": hours_ago(2)},
    {"device": "Bike 02", "lat": 37.953162, "lon": -91.772478, "score": 65, "ts": hours_ago(10)},
    {"device": "Bike 02", "lat": 37.953162, "lon": -91.772478, "score": 55, "ts": hours_ago(2)},
    {"device": "Bike 03", "lat": 37.953162, "lon": -91.772478, "score": 60, "ts": hours_ago(10)},
    {"device": "Bike 03", "lat": 37.953162, "lon": -91.772478, "score": 51, "ts": hours_ago(2)},
    {"device": "Bike 04", "lat": 37.953162, "lon": -91.772478, "score": 60, "ts": hours_ago(10)},
    {"device": "Bike 04", "lat": 37.953162, "lon": -91.772478, "score": 69, "ts": hours_ago(2)},

    # --- cluster 4: 1 reporter, one report, score of 100 - on 12th between pine and elm 37.953201916008794, -91.77102714714331
    {"device": "Bike 04", "lat": 37.953201, "lon": -91.771027, "score": 100, "ts": hours_ago(2)},
]

# ---------------------------------------------------------------------------
# EDIT HERE — Settings overrides
# Any key listed here will override the default from config.py.
# Keys not listed will fall back to their defaults from config.py.
# ---------------------------------------------------------------------------
SETTINGS = {
    "clustering_radius_m":      15.0,
    "search_radius_m":          1500.0,
    "spam_cooldown_s":          0,
    "score_w_avg_severity":     0.5,
    "score_w_consistency":      0.3,
    "score_w_recency":          0.06,
    "score_w_total_reports":    0.06,
    "score_w_unique_reporters": 0.07,
    "learning_rate":            0.1
}

# ---------------------------------------------------------------------------
# Seed logic — no edits needed below
# ---------------------------------------------------------------------------
def main():
    conn = _db.get_db()
    _db.init_db(conn)   # seeds defaults into settings table

    # Apply overrides
    for key, value in SETTINGS.items():
        conn.execute(
            "INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)",
            (key, str(value)),
        )
    conn.commit()
    _db.load_settings(conn)
    print("Settings:")
    for key, value in SETTINGS.items():
        print(f"  {key} = {value}")
    print()

    # Insert devices, collect label -> db_id map
    device_ids = {}
    for entry in DEVICES:
        if isinstance(entry, tuple):
            label, api_key = entry
        else:
            label, api_key = entry, secrets.token_hex(32)
        cur = conn.execute(
            "INSERT INTO devices(api_key, label, created_at) VALUES (?, ?, ?)",
            (api_key, label, int(time.time())),
        )
        conn.commit()
        device_ids[label] = cur.lastrowid
        print(f"  Device  '{label}'  id={cur.lastrowid}  key={api_key}")

    print()

    # Insert reports
    received_at = int(time.time())
    for r in REPORTS:
        label = r["device"]
        if label not in device_ids:
            print(f"  SKIP — unknown device '{label}'")
            continue
        cluster_id, is_counted = _db.process_report(
            conn,
            device_db_id=device_ids[label],
            lat=r["lat"],
            lon=r["lon"],
            severity=r["score"],
            timestamp=r["ts"],
            received_at=received_at,
        )
        status = "accepted" if is_counted else "spam-rejected"
        print(f"  Report  device='{label}'  score={r['score']}  "
              f"({r['lat']:.4f}, {r['lon']:.4f})  → cluster {cluster_id}  [{status}]")

    conn.close()
    print("\nDone.")

if __name__ == "__main__":
    main()

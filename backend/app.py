import os
import secrets
import time
from functools import wraps

from flask import Flask, g, jsonify, request, abort, send_from_directory

import db as _db
import scoring
from config import DEFAULTS

app = Flask(__name__)

ADMIN_API_KEY = os.environ.get("ADMIN_API_KEY", "admin")


# ---------------------------------------------------------------------------
# Request lifecycle
# ---------------------------------------------------------------------------

_db_initialised = False


@app.before_request
def open_db():
    global _db_initialised
    g.db = _db.get_db()
    if not _db_initialised:
        _db.init_db(g.db)
        _db.load_settings(g.db)
        _db_initialised = True


@app.teardown_appcontext
def close_db(exc=None):
    db = g.pop("db", None)
    if db is not None:
        db.close()


# ---------------------------------------------------------------------------
# Auth decorators
# ---------------------------------------------------------------------------

def require_device_auth(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        api_key = request.headers.get("X-API-Key", "")
        row = g.db.execute(
            "SELECT id FROM devices WHERE api_key = ? AND is_active = 1",
            (api_key,),
        ).fetchone()
        if row is None:
            abort(401, description="Invalid or inactive device API key")
        g.device_db_id = row["id"]
        return f(*args, **kwargs)
    return wrapper


def require_admin_auth(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        if not ADMIN_API_KEY:
            abort(500, description="ADMIN_API_KEY not configured")
        key = request.headers.get("X-API-Key", "")
        if not secrets.compare_digest(key, ADMIN_API_KEY):
            abort(403, description="Forbidden")
        return f(*args, **kwargs)
    return wrapper


# ---------------------------------------------------------------------------
# Public endpoints
# ---------------------------------------------------------------------------

@app.get("/")
def index():
    return send_from_directory("static", "index.html")


@app.get("/admin")
def admin_page():
    return send_from_directory("static", "admin.html")


@app.get("/sim")
def sim_page():
    return send_from_directory("static", "simulation.html")


@app.get("/api/time")
def get_time():
    return jsonify({"unix": int(time.time())})


@app.get("/api/potholes")
def get_potholes():
    try:
        lat = float(request.args["lat"])
        lon = float(request.args["lon"])
    except (KeyError, ValueError):
        abort(400, description="lat and lon query params required")

    search_radius = _db.get_setting("search_radius_m")

    rows = g.db.execute(
        """
        SELECT id, lat, lon, report_count, unique_reporters,
               avg_severity, severity_variance, score,
               first_seen, last_seen,
               haversine_m(lat, lon, ?, ?) AS distance_m
        FROM   pothole_clusters
        WHERE  haversine_m(lat, lon, ?, ?) <= ?
        ORDER  BY distance_m
        """,
        (lat, lon, lat, lon, search_radius),
    ).fetchall()

    return jsonify([dict(r) for r in rows])


# ---------------------------------------------------------------------------
# Device endpoint
# ---------------------------------------------------------------------------

@app.post("/api/potholes")
@require_device_auth
def ingest_potholes():
    body = request.get_json(silent=True)
    if not body or "events" not in body:
        abort(400, description="JSON body with 'events' list required")

    events = body["events"]
    if not isinstance(events, list):
        abort(400, description="'events' must be a list")

    received_at = int(time.time())
    accepted = 0
    spam_rejected = 0

    for event in events:
        try:
            lat      = float(event["latitude"])
            lon      = float(event["longitude"])
            severity = int(event["score"])
            timestamp = int(event["timestamp"])
        except (KeyError, TypeError, ValueError):
            continue  # skip malformed event

        if not (-90.0 <= lat <= 90.0):
            continue
        if not (-180.0 <= lon <= 180.0):
            continue

        severity = max(1, min(100, severity))

        _, is_counted = _db.process_report(
            g.db,
            device_db_id=g.device_db_id,
            lat=lat,
            lon=lon,
            severity=severity,
            timestamp=timestamp,
            received_at=received_at,
        )

        if is_counted:
            accepted += 1
        else:
            spam_rejected += 1

    return jsonify({"accepted": accepted, "spam_rejected": spam_rejected}), 200


# ---------------------------------------------------------------------------
# Admin endpoints
# ---------------------------------------------------------------------------

@app.get("/api/devices")
@require_admin_auth
def list_devices():
    rows = g.db.execute(
        "SELECT id, label, created_at, is_active FROM devices ORDER BY created_at DESC"
    ).fetchall()
    return jsonify([dict(r) for r in rows])


@app.post("/api/devices/register")
@require_admin_auth
def register_device():
    body = request.get_json(silent=True) or {}
    label = body.get("label", "")
    api_key = secrets.token_hex(32)
    cur = g.db.execute(
        "INSERT INTO devices(api_key, label, created_at) VALUES (?, ?, ?)",
        (api_key, label, int(time.time())),
    )
    g.db.commit()
    return jsonify({"id": cur.lastrowid, "api_key": api_key, "label": label}), 201


@app.get("/api/settings")
@require_admin_auth
def get_settings():
    rows = g.db.execute("SELECT key, value FROM settings ORDER BY key").fetchall()
    return jsonify({r["key"]: r["value"] for r in rows})


@app.put("/api/settings")
@require_admin_auth
def update_settings():
    body = request.get_json(silent=True)
    if not body or not isinstance(body, dict):
        abort(400, description="JSON object required")

    updated = {}
    errors  = {}
    for key, value in body.items():
        if key not in DEFAULTS:
            errors[key] = "unknown setting"
            continue
        try:
            float(value)  # validate numeric
        except (TypeError, ValueError):
            errors[key] = "value must be numeric"
            continue
        g.db.execute(
            "INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)",
            (key, str(value)),
        )
        updated[key] = str(value)

    if updated:
        g.db.commit()
        _db.invalidate_settings_cache(g.db)

    return jsonify({"updated": updated, "errors": errors})


@app.post("/api/potholes/<int:cluster_id>/fixed")
@require_admin_auth
def mark_fixed(cluster_id):
    row = g.db.execute(
        "SELECT id FROM pothole_clusters WHERE id = ?", (cluster_id,)
    ).fetchone()
    if row is None:
        abort(404, description="Cluster not found")

    with g.db:
        g.db.execute(
            "DELETE FROM device_cluster_log WHERE cluster_id = ?", (cluster_id,)
        )
        g.db.execute("DELETE FROM reports WHERE cluster_id = ?", (cluster_id,))
        g.db.execute(
            "DELETE FROM pothole_clusters WHERE id = ?", (cluster_id,)
        )

    return jsonify({"deleted": cluster_id})


@app.post("/api/potholes/<int:cluster_id>/false-positive")
@require_admin_auth
def mark_false_positive(cluster_id):
    row = g.db.execute(
        "SELECT id FROM pothole_clusters WHERE id = ?", (cluster_id,)
    ).fetchone()
    if row is None:
        abort(404, description="Cluster not found")

    # Nudge weights before deleting so the feature vector is still accessible
    scoring.apply_false_positive_nudge(cluster_id, g.db)

    with g.db:
        g.db.execute(
            "DELETE FROM device_cluster_log WHERE cluster_id = ?", (cluster_id,)
        )
        g.db.execute("DELETE FROM reports WHERE cluster_id = ?", (cluster_id,))
        g.db.execute(
            "DELETE FROM pothole_clusters WHERE id = ?", (cluster_id,)
        )

    return jsonify({"deleted": cluster_id, "weights_nudged": True})


# ---------------------------------------------------------------------------
# Simulation endpoints (no auth — internal / demo tooling only)
# ---------------------------------------------------------------------------

@app.get("/api/sim/devices")
def sim_list_devices():
    rows = g.db.execute(
        "SELECT id, label FROM devices WHERE is_active = 1 ORDER BY label"
    ).fetchall()
    return jsonify([dict(r) for r in rows])


@app.post("/api/simulate")
def simulate_report():
    body = request.get_json(silent=True) or {}
    try:
        device_id = int(body["device_id"])
        lat       = float(body["latitude"])
        lon       = float(body["longitude"])
        score     = max(1, min(100, int(body.get("score", 50))))
        timestamp = int(body.get("timestamp", int(time.time())))
    except (KeyError, TypeError, ValueError) as exc:
        abort(400, description=f"Invalid parameters: {exc}")

    if not (-90.0 <= lat <= 90.0) or not (-180.0 <= lon <= 180.0):
        abort(400, description="Coordinates out of range")

    row = g.db.execute(
        "SELECT id FROM devices WHERE id = ? AND is_active = 1", (device_id,)
    ).fetchone()
    if row is None:
        abort(404, description="Device not found or inactive")

    _, is_counted = _db.process_report(
        g.db,
        device_db_id=row["id"],
        lat=lat,
        lon=lon,
        severity=score,
        timestamp=timestamp,
        received_at=int(time.time()),
    )
    return jsonify({"accepted": int(is_counted), "spam_rejected": int(not is_counted)})


# ---------------------------------------------------------------------------
# Error handlers
# ---------------------------------------------------------------------------

@app.errorhandler(400)
@app.errorhandler(401)
@app.errorhandler(403)
@app.errorhandler(404)
@app.errorhandler(500)
def handle_error(e):
    return jsonify({"error": str(e.description)}), e.code


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    app.run(host="192.168.137.1", port=5000, debug=False, ssl_context=("cert.pem", "key.pem"))

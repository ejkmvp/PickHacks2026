import math
import sqlite3
import time

from config import DATABASE_PATH, DEFAULTS


# ---------------------------------------------------------------------------
# Haversine UDF
# ---------------------------------------------------------------------------

def _haversine_m(lat1, lon1, lat2, lon2):
    """Return great-circle distance in metres between two WGS-84 points."""
    R = 6_371_000.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2
    return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))


# ---------------------------------------------------------------------------
# Connection factory
# ---------------------------------------------------------------------------

def get_db():
    conn = sqlite3.connect(DATABASE_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    conn.create_function("haversine_m", 4, _haversine_m)
    return conn


# ---------------------------------------------------------------------------
# Schema
# ---------------------------------------------------------------------------

_DDL = [
    """
    CREATE TABLE IF NOT EXISTS devices (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        api_key    TEXT UNIQUE NOT NULL,
        label      TEXT,
        created_at INTEGER NOT NULL,
        is_active  INTEGER NOT NULL DEFAULT 1
    )
    """,
    """
    CREATE TABLE IF NOT EXISTS pothole_clusters (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        lat              REAL NOT NULL,
        lon              REAL NOT NULL,
        report_count     INTEGER NOT NULL DEFAULT 0,
        unique_reporters INTEGER NOT NULL DEFAULT 0,
        avg_severity     REAL NOT NULL DEFAULT 0.0,
        severity_variance REAL NOT NULL DEFAULT 0.0,
        score            REAL NOT NULL DEFAULT 0.0,
        first_seen       INTEGER NOT NULL,
        last_seen        INTEGER NOT NULL
    )
    """,
    """
    CREATE TABLE IF NOT EXISTS reports (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id   INTEGER NOT NULL REFERENCES devices(id),
        cluster_id  INTEGER NOT NULL REFERENCES pothole_clusters(id),
        lat         REAL NOT NULL,
        lon         REAL NOT NULL,
        severity    INTEGER NOT NULL,
        timestamp   INTEGER NOT NULL,
        received_at INTEGER NOT NULL,
        is_counted  INTEGER NOT NULL DEFAULT 1
    )
    """,
    """
    CREATE TABLE IF NOT EXISTS device_cluster_log (
        device_id          INTEGER NOT NULL REFERENCES devices(id),
        cluster_id         INTEGER NOT NULL REFERENCES pothole_clusters(id),
        last_contributed   INTEGER NOT NULL,
        contribution_count INTEGER NOT NULL DEFAULT 1,
        PRIMARY KEY (device_id, cluster_id)
    )
    """,
    """
    CREATE TABLE IF NOT EXISTS settings (
        key   TEXT PRIMARY KEY,
        value TEXT NOT NULL
    )
    """,
    "CREATE INDEX IF NOT EXISTS idx_reports_cluster  ON reports(cluster_id)",
    "CREATE INDEX IF NOT EXISTS idx_reports_device   ON reports(device_id)",
    "CREATE INDEX IF NOT EXISTS idx_dclog_device     ON device_cluster_log(device_id)",
]


def init_db(conn):
    """Create tables and seed default settings (idempotent)."""
    for stmt in _DDL:
        conn.execute(stmt)
    for key, value in DEFAULTS.items():
        conn.execute(
            "INSERT OR IGNORE INTO settings(key, value) VALUES (?, ?)",
            (key, value),
        )
    conn.commit()


# ---------------------------------------------------------------------------
# Settings cache
# ---------------------------------------------------------------------------

_settings_cache: dict = {}


def load_settings(conn):
    global _settings_cache
    rows = conn.execute("SELECT key, value FROM settings").fetchall()
    _settings_cache = {r["key"]: r["value"] for r in rows}
    # Fill in any keys that are in DEFAULTS but not yet in DB
    for k, v in DEFAULTS.items():
        _settings_cache.setdefault(k, v)


def get_setting(key, cast=float):
    raw = _settings_cache.get(key, DEFAULTS.get(key))
    if raw is None:
        raise KeyError(f"Unknown setting: {key}")
    return cast(raw)


def invalidate_settings_cache(conn):
    load_settings(conn)


# ---------------------------------------------------------------------------
# Core ingestion transaction
# ---------------------------------------------------------------------------

def process_report(conn, device_db_id, lat, lon, severity, timestamp, received_at):
    """
    Find-or-create cluster, spam-check, insert report, update cluster stats.

    Returns (cluster_id, is_counted).
    """
    import scoring  # local import to avoid circular at module load

    clustering_radius = get_setting("clustering_radius_m")
    spam_cooldown     = get_setting("spam_cooldown_s")

    with conn:  # begins/commits or rolls back
        # 1. Find nearest cluster within radius
        row = conn.execute(
            """
            SELECT id
            FROM   pothole_clusters
            WHERE  haversine_m(lat, lon, ?, ?) <= ?
            ORDER  BY haversine_m(lat, lon, ?, ?)
            LIMIT  1
            """,
            (lat, lon, clustering_radius, lat, lon),
        ).fetchone()

        # 2. Create cluster if none found
        if row is None:
            cur = conn.execute(
                """
                INSERT INTO pothole_clusters
                    (lat, lon, report_count, unique_reporters,
                     avg_severity, severity_variance, score,
                     first_seen, last_seen)
                VALUES (?, ?, 0, 0, 0.0, 0.0, 0.0, ?, ?)
                """,
                (lat, lon, received_at, received_at),
            )
            cluster_id = cur.lastrowid
            is_new_cluster = True
        else:
            cluster_id = row["id"]
            is_new_cluster = False

        # 3. Spam check
        log_row = conn.execute(
            """
            SELECT last_contributed
            FROM   device_cluster_log
            WHERE  device_id = ? AND cluster_id = ?
            """,
            (device_db_id, cluster_id),
        ).fetchone()

        if log_row and (received_at - log_row["last_contributed"]) < spam_cooldown:
            is_counted = 0
        else:
            is_counted = 1

        # 4. Insert report
        conn.execute(
            """
            INSERT INTO reports
                (device_id, cluster_id, lat, lon, severity,
                 timestamp, received_at, is_counted)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (device_db_id, cluster_id, lat, lon, severity,
             timestamp, received_at, is_counted),
        )

        # 5. Update cluster stats (only for counted reports)
        if is_counted:
            # New centroid from all counted reports
            centroid_row = conn.execute(
                """
                SELECT AVG(lat) AS lat, AVG(lon) AS lon,
                       COUNT(*) AS n
                FROM   reports
                WHERE  cluster_id = ? AND is_counted = 1
                """,
                (cluster_id,),
            ).fetchone()
            new_lat = centroid_row["lat"]
            new_lon = centroid_row["lon"]
            new_n   = centroid_row["n"]

            # Welford update for avg_severity and severity_variance
            cluster_row = conn.execute(
                "SELECT avg_severity, severity_variance, report_count, unique_reporters "
                "FROM pothole_clusters WHERE id = ?",
                (cluster_id,),
            ).fetchone()

            old_avg = cluster_row["avg_severity"]
            old_var = cluster_row["severity_variance"]
            old_n   = cluster_row["report_count"]  # counted reports before this one

            # Welford incremental update
            delta    = severity - old_avg
            new_avg  = old_avg + delta / new_n
            delta2   = severity - new_avg
            # variance stored as population variance (M2 / n)
            if old_n == 0:
                new_m2 = 0.0
            else:
                new_m2 = old_var * old_n + delta * delta2
            new_var = new_m2 / new_n if new_n > 0 else 0.0

            # unique_reporters: increment only if no prior log entry
            new_unique = cluster_row["unique_reporters"]
            if log_row is None:
                new_unique += 1

            # Upsert device_cluster_log
            if log_row is None:
                conn.execute(
                    """
                    INSERT INTO device_cluster_log
                        (device_id, cluster_id, last_contributed, contribution_count)
                    VALUES (?, ?, ?, 1)
                    """,
                    (device_db_id, cluster_id, received_at),
                )
            else:
                conn.execute(
                    """
                    UPDATE device_cluster_log
                    SET    last_contributed = ?,
                           contribution_count = contribution_count + 1
                    WHERE  device_id = ? AND cluster_id = ?
                    """,
                    (received_at, device_db_id, cluster_id),
                )

            # Update cluster
            conn.execute(
                """
                UPDATE pothole_clusters
                SET lat               = ?,
                    lon               = ?,
                    report_count      = ?,
                    unique_reporters  = ?,
                    avg_severity      = ?,
                    severity_variance = ?,
                    last_seen         = ?
                WHERE id = ?
                """,
                (new_lat, new_lon, new_n, new_unique,
                 new_avg, new_var, received_at, cluster_id),
            )

            # Compute new score
            timestamps = [
                r[0] for r in conn.execute(
                    "SELECT timestamp FROM reports WHERE cluster_id = ? AND is_counted = 1",
                    (cluster_id,),
                ).fetchall()
            ]
            new_score = scoring.compute_score(
                unique_reporters=new_unique,
                total_reports=new_n,
                avg_severity=new_avg,
                severity_variance=new_var,
                report_timestamps=timestamps,
            )
            conn.execute(
                "UPDATE pothole_clusters SET score = ? WHERE id = ?",
                (new_score, cluster_id),
            )

    return cluster_id, is_counted

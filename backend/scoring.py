import math
import time

from config import (
    DEFAULTS,
    MAX_UNIQUE_REPORTERS,
    MAX_TOTAL_REPORTS,
    MAX_SEVERITY_VAR,
)

# Weight key names in the same order used throughout this module
_WEIGHT_KEYS = [
    "score_w_unique_reporters",
    "score_w_total_reports",
    "score_w_avg_severity",
    "score_w_recency",
    "score_w_consistency",
]


def _clamp(value, lo, hi):
    return max(lo, min(hi, value))


def _get_weights():
    """Read current weights from the settings cache."""
    from db import get_setting  # local import to avoid circular at module level
    return [get_setting(k) for k in _WEIGHT_KEYS]


def _feature_vector(unique_reporters, total_reports, avg_severity,
                    severity_variance, report_timestamps):
    """Return list of five normalised features in [0, 1]."""
    from db import get_setting

    # f_unique
    u = min(unique_reporters, MAX_UNIQUE_REPORTERS)
    f_unique = math.log(u + 1) / math.log(MAX_UNIQUE_REPORTERS + 1)

    # f_total
    n = min(total_reports, MAX_TOTAL_REPORTS)
    f_total = math.log(n + 1) / math.log(MAX_TOTAL_REPORTS + 1)

    # f_severity
    f_severity = _clamp(avg_severity / 100.0, 0.0, 1.0)

    # f_recency
    lam = get_setting("score_decay_lambda")
    now = time.time()
    if report_timestamps:
        decayed = []
        for ts in report_timestamps:
            age_days = max(0.0, (now - ts) / 86400.0)
            decayed.append(math.exp(-lam * age_days))
        f_recency = sum(decayed) / len(decayed)
    else:
        f_recency = 0.0

    # f_consistency
    f_consistency = _clamp(1.0 - severity_variance / MAX_SEVERITY_VAR, 0.0, 1.0)

    return [f_unique, f_total, f_severity, f_recency, f_consistency]


def compute_score(unique_reporters, total_reports, avg_severity,
                  severity_variance, report_timestamps):
    """
    Compute cluster score in [0, 100].

    All inputs are derived from the pothole_clusters row and the list of
    counted report timestamps.
    """
    weights  = _get_weights()
    features = _feature_vector(
        unique_reporters, total_reports, avg_severity,
        severity_variance, report_timestamps,
    )

    w_sum = sum(weights)
    if w_sum < 1e-9:
        return 0.0

    raw = sum(w * f for w, f in zip(weights, features))
    return _clamp(raw / w_sum * 100.0, 0.0, 100.0)


def apply_false_positive_nudge(cluster_id, conn):
    """
    Gradient-based weight nudge for a false-positive cluster, then persist.

    Weights are nudged DOWN proportional to each feature contribution,
    clipped to [min_weight, ∞], and written back to the settings table.
    """
    from db import get_setting, invalidate_settings_cache

    row = conn.execute(
        """
        SELECT unique_reporters, report_count, avg_severity, severity_variance
        FROM   pothole_clusters
        WHERE  id = ?
        """,
        (cluster_id,),
    ).fetchone()

    if row is None:
        return

    timestamps = [
        r[0] for r in conn.execute(
            "SELECT timestamp FROM reports WHERE cluster_id = ? AND is_counted = 1",
            (cluster_id,),
        ).fetchall()
    ]

    features = _feature_vector(
        unique_reporters=row["unique_reporters"],
        total_reports=row["report_count"],
        avg_severity=row["avg_severity"],
        severity_variance=row["severity_variance"],
        report_timestamps=timestamps,
    )

    weights = _get_weights()
    w_sum   = sum(weights)
    S       = sum(w * f for w, f in zip(weights, features)) / w_sum if w_sum > 1e-9 else 0.0

    lr      = get_setting("learning_rate")
    min_w   = get_setting("min_weight")

    new_weights = [
        max(min_w, w - lr * S * f)
        for w, f in zip(weights, features)
    ]

    for key, new_val in zip(_WEIGHT_KEYS, new_weights):
        conn.execute(
            "INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)",
            (key, str(new_val)),
        )
    conn.commit()
    invalidate_settings_cache(conn)

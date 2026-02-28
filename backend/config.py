DATABASE_PATH = "potholes.db"

DEFAULTS = {
    "clustering_radius_m":      "15.0",
    "search_radius_m":          "1500.0",
    "spam_cooldown_s":          "3600",
    "score_w_unique_reporters": "0.25",
    "score_w_total_reports":    "0.20",
    "score_w_avg_severity":     "0.30",
    "score_w_recency":          "0.15",
    "score_w_consistency":      "0.10",
    "score_decay_lambda":       "0.1",
    "learning_rate":            "0.05",
    "min_weight":               "0.01",
}

MAX_UNIQUE_REPORTERS = 50
MAX_TOTAL_REPORTS    = 200
MAX_SEVERITY_VAR     = 2500.0

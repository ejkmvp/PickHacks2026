"""Clear all tables in the database."""
import sqlite3
from config import DATABASE_PATH

conn = sqlite3.connect(DATABASE_PATH)
conn.execute("PRAGMA foreign_keys=OFF")

tables = ["device_cluster_log", "reports", "pothole_clusters", "devices", "settings"]
for table in tables:
    conn.execute(f"DELETE FROM {table}")

conn.execute("PRAGMA foreign_keys=ON")
conn.commit()
conn.close()

print("Cleared:", ", ".join(tables))

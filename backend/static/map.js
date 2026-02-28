"use strict";

const CraterWatch = (() => {
    const DEFAULT_CENTER = [37.9514, -91.7713]; // Rolla, MO
    const DEFAULT_ZOOM   = 13;
    const LOAD_ZOOM      = 14;

    function scoreColor(score) {
        if (score >= 70) return "#ef4444";
        if (score >= 40) return "#f97316";
        return "#22c55e";
    }

    function severityLabel(score) {
        if (score >= 70) return "Severe";
        if (score >= 40) return "Moderate";
        return "Minor";
    }

    function initMap(containerId) {
        const map = L.map(containerId).setView(DEFAULT_CENTER, DEFAULT_ZOOM);
        L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
            maxZoom: 19,
            attribution:
                '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
        }).addTo(map);
        return map;
    }

    async function fetchPotholes(lat, lng, apiKey) {
        const headers = {};
        if (apiKey) headers["X-API-Key"] = apiKey;
        const res = await fetch(`/api/potholes?lat=${lat}&lon=${lng}`, { headers });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
    }

    return { DEFAULT_CENTER, DEFAULT_ZOOM, LOAD_ZOOM, scoreColor, severityLabel, initMap, fetchPotholes };
})();

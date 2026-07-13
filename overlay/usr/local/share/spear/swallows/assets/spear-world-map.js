/**
 * SpearWorldMap — equirectangular world map with fleet/threat pins,
 * tooltips, and toggle. Image ratio 2058×1036; object-fit:fill.
 *
 * Projection (synced to map image):
 *   x% = (lon + 180) / 360 * 100
 *   y% = (90 - lat) / 180 * 100
 */
(function (global) {
  "use strict";

  function esc(s) {
    return String(s ?? "").replace(/[&<>"']/g, (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c])
    );
  }

  function proj(lat, lon) {
    const x = ((Number(lon) + 180) / 360) * 100;
    const y = ((90 - Number(lat)) / 180) * 100;
    return {
      x: Math.min(99.6, Math.max(0.4, x)),
      y: Math.min(99.6, Math.max(0.4, y)),
    };
  }

  function SpearWorldMap(root, opts) {
    this.root = typeof root === "string" ? document.querySelector(root) : root;
    this.opts = Object.assign(
      {
        mapSrc: "assets/maps/world-equirectangular.jpg",
        showFleet: true,
        showThreats: true,
        onSelect: null,
      },
      opts || {}
    );
    this.points = [];
    this._build();
  }

  SpearWorldMap.prototype._build = function () {
    const el = this.root;
    el.classList.add("spear-world-map");
    el.innerHTML = `
      <div class="map-stage">
        <img class="map-img" alt="World equirectangular map 2058x1036" src="${esc(this.opts.mapSrc)}" width="2058" height="1036" />
        <div class="map-shade"></div>
        <div class="map-toolbar">
          <button type="button" data-mode="fleet" class="on">Fleet</button>
          <button type="button" data-mode="threat" class="on threat">Threats</button>
          <button type="button" data-mode="both" class="on">Both</button>
        </div>
        <div class="map-legend">2058×1036 equirectangular · pins locked to image box · hover tooltips</div>
        <div class="tip" id="spearMapTip"></div>
      </div>
    `;
    this.stage = el.querySelector(".map-stage");
    const img = el.querySelector(".map-img");
    img.onerror = () => {
      img.style.background = "#123";
      img.alt = "map image missing";
    };
    this.tip = el.querySelector(".tip");
    const self = this;
    el.querySelectorAll(".map-toolbar button").forEach((btn) => {
      btn.addEventListener("click", () => {
        const mode = btn.dataset.mode;
        if (mode === "both") {
          self.opts.showFleet = true;
          self.opts.showThreats = true;
        } else if (mode === "fleet") {
          self.opts.showFleet = true;
          self.opts.showThreats = false;
        } else if (mode === "threat") {
          self.opts.showFleet = false;
          self.opts.showThreats = true;
        }
        el.querySelectorAll(".map-toolbar button").forEach((b) => {
          b.classList.remove("on");
          if (mode === "both") b.classList.add("on");
          else if (b.dataset.mode === mode) b.classList.add("on");
        });
        if (mode === "both") {
          el.querySelectorAll(".map-toolbar button").forEach((b) => b.classList.add("on"));
        }
        self.render();
      });
    });
  };

  SpearWorldMap.prototype.setPoints = function (points) {
    this.points = points || [];
    this.render();
  };

  SpearWorldMap.prototype.render = function () {
    const el = this.root;
    const stage = this.stage || el.querySelector(".map-stage") || el;
    stage.querySelectorAll(".pin").forEach((p) => p.remove());
    const tip = this.tip;
    const self = this;
    let nF = 0,
      nT = 0,
      nR = 0;
    this.points.forEach((pt) => {
      const kind = pt.kind || (pt.threat ? "threat" : "fleet");
      if (kind === "threat" || pt.threat) {
        if (!self.opts.showThreats) return;
        nT++;
      } else if (kind === "rack") {
        if (!self.opts.showFleet) return;
        nR++;
      } else {
        if (!self.opts.showFleet) return;
        nF++;
      }
      if (pt.lat == null || pt.lon == null) return;
      const p = proj(pt.lat, pt.lon);
      const d = document.createElement("div");
      const isThreat = kind === "threat" || !!pt.threat;
      const isDead = isThreat || pt.dead || pt.status === "off" || pt.status === "DEAD" || pt.status === "OUSTED";
      d.className =
        "pin " +
        (kind === "rack" ? "rack" : isThreat ? "threat" : "fleet") +
        (isDead ? " dead" : "") +
        (pt.selected ? " sel" : "");
      if (isDead) d.dataset.flag = "skull";
      d.style.left = p.x + "%";
      d.style.top = p.y + "%";
      const title =
        pt.tooltip ||
        pt.title ||
        [pt.name, pt.id, pt.lat + "," + pt.lon].filter(Boolean).join(" · ");
      d.title = title; // native tooltip fallback
      d.addEventListener("mouseenter", (ev) => {
        tip.className = "tip show" + (isThreat ? " threat-tip" : "");
        const nl = (s) => esc(s || "").replace(/\n/g, "<br>");
        // Full enemy / fleet dossier text in rich tooltip
        const full = pt.tooltip || pt.detail || "";
        tip.innerHTML = full
          ? `<b>${esc(pt.title || pt.name || pt.id || "point")}</b><br>${nl(full)}`
          : `<b>${esc(pt.title || pt.name || pt.id || "point")}</b>` +
            (pt.subtitle ? `<br>${esc(pt.subtitle)}` : "") +
            `<br><span style="color:#7ad7ff">${esc(pt.lat)}, ${esc(pt.lon)}</span>` +
            (pt.path ? `<br><span style="color:#c9a8c0">${esc(pt.path)}</span>` : "");
        tip.style.left = p.x + "%";
        tip.style.top = p.y + "%";
      });
      d.addEventListener("mouseleave", () => {
        tip.classList.remove("show");
      });
      d.addEventListener("click", (e) => {
        e.stopPropagation();
        if (typeof self.opts.onSelect === "function") self.opts.onSelect(pt);
      });
      stage.appendChild(d);
    });
    const leg = (this.stage || el).querySelector(".map-legend");
    if (leg) {
      leg.textContent = `☠ = dead/threat flags · fleet ${nF} · racks ${nR} · dead ${nT} · pirate map`;
    }
  };

  /** Load country centroids + threat GPS into map points (lightweight). */
  SpearWorldMap.loadDefaultPoints = async function () {
    const points = [];
    try {
      const regs = await fetch("/api/regions?ts=" + Date.now()).then((r) => r.json());
      (regs.regions || []).forEach((r) => {
        if (r.lat == null) return;
        points.push({
          id: r.code,
          kind: "fleet",
          threat: false,
          lat: r.lat,
          lon: r.lon,
          title: `${r.code} · ${r.name}`,
          subtitle: `${r.racks || 500} racks · zone ${r.zone || "?"} · ${r.status || "ok"}`,
          detail: `Country centroid · click to open fleet`,
          tooltip: `${r.code} ${r.name} · ${r.lat},${r.lon} · ${r.racks} racks`,
          region: r.code,
          name: r.name,
        });
      });
    } catch (_) {}
    try {
      const kills = await fetch("/api/dossiers?ts=" + Date.now()).then((r) => r.json());
      (kills.kills || []).forEach((k) => {
        if (k.kind !== "ip_kill" && k.target_type !== "ip") return;
        const g = k.gps || {};
        if (g.lat == null && k.lat == null) return;
        const lat = g.lat ?? k.lat;
        const lon = g.lon ?? k.lon;
        const loc = [g.city, g.region, g.country].filter(Boolean).join(", ");
        points.push({
          id: k.id || k.target,
          kind: "threat",
          threat: true,
          dead: true,
          status: k.status || "DEAD",
          flag: "skull_crossbones",
          lat,
          lon,
          title: `☠ DEAD ${k.target || k.id}`,
          subtitle: `${k.vector || ""} · ${k.status || "DEAD"} · rekill ${k.rekill_count ?? "—"}`,
          detail: loc || g.isp || "",
          path: k.reason || "",
          tooltip: `☠ ${k.target} · DEAD · ${lat},${lon} · ${loc}`,
          target: k.target,
        });
      });
    } catch (_) {}
    return points;
  };

  SpearWorldMap.proj = proj;
  global.SpearWorldMap = SpearWorldMap;
})(typeof window !== "undefined" ? window : globalThis);

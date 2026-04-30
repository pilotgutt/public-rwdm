import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# ── Configuration ─────────────────────────────────────────────────────────────
SCRIPT_DIR  = Path(__file__).parent
DISTANCES   = list(range(0, 20, 2))      # 0, 2, 4, … 18 cm

ROLL_WINDOW  = 10
CONFIRM      = 12


# ── Otsu threshold ────────────────────────────────────────────────────────────
def otsu_threshold(values: np.ndarray) -> float:
    candidates = np.unique(values.astype(int))
    best_thresh, best_var = candidates[0], np.inf
    for t in candidates:
        lo = values[values <= t]
        hi = values[values >  t]
        if len(lo) == 0 or len(hi) == 0:
            continue
        var = len(lo)/len(values)*np.var(lo) + len(hi)/len(values)*np.var(hi)
        if var < best_var:
            best_var, best_thresh = var, float(t)
    return best_thresh


# ── Water-window detection ────────────────────────────────────────────────────
def detect_water_window(rssi_series: pd.Series) -> tuple[int, int]:
    vals      = rssi_series.values.astype(float)
    smoothed  = pd.Series(vals).rolling(ROLL_WINDOW, center=True,
                                        min_periods=1).median().values
    threshold = otsu_threshold(smoothed)
    n         = len(smoothed)

    start_idx, end_idx = None, None

    run = 0
    for i in range(n):
        if smoothed[i] < threshold:
            run += 1
            if run >= CONFIRM:
                start_idx = i - CONFIRM + 1; break
        else:
            run = 0

    if start_idx is None:
        return 0, n - 1

    run = 0
    for i in range(start_idx + CONFIRM, n):
        if smoothed[i] > threshold:
            run += 1
            if run >= CONFIRM:
                end_idx = i - CONFIRM; break
        else:
            run = 0

    return start_idx, (end_idx if end_idx is not None else n - 1)


# ── CSV parsers ───────────────────────────────────────────────────────────────
def parse_rssi(path: Path) -> pd.DataFrame:
    df   = pd.read_csv(path)
    rssi = df[df["Source"] == "RSSI"].copy()
    rssi["dBm"] = (rssi["Line"]
                   .str.replace(r'["\s]', "", regex=True)
                   .str.replace("dBm", "", regex=False)
                   .astype(float))
    return rssi[["Timestamp", "dBm"]].reset_index(drop=True)


def parse_interval(path: Path) -> pd.DataFrame:
    df  = pd.read_csv(path)
    adv = df[df["Source"] == "Advertising Interval"].copy()
    adv["ms"] = (adv["Line"]
                 .str.replace(r'["\s]', "", regex=True)
                 .str.replace("ms", "", regex=False)
                 .astype(float))
    return adv[["Timestamp", "ms"]].reset_index(drop=True)


def ts_to_sec(ts_series: pd.Series) -> pd.Series:
    t = pd.to_datetime(ts_series, format="%H:%M:%S.%f")
    return (t - t.iloc[0]).dt.total_seconds()


# ── Load one file ─────────────────────────────────────────────────────────────
def load_file(path: Path):
    rssi_df     = parse_rssi(path)
    interval_df = parse_interval(path)

    start, end = detect_water_window(rssi_df["dBm"])
    water_rssi = rssi_df["dBm"].iloc[start:end + 1]

    rssi_times = ts_to_sec(rssi_df["Timestamp"])
    t_start    = rssi_times.iloc[start]
    t_end      = rssi_times.iloc[end]

    int_times  = ts_to_sec(interval_df["Timestamp"])
    water_int  = interval_df["ms"][(int_times >= t_start) & (int_times <= t_end)]

    meta = dict(start=start, end=end,
                n_total=len(rssi_df), n_water=len(water_rssi),
                duration=t_end - t_start)
    return water_rssi.mean(), water_int.mean(), meta


# ── Collect all distances ─────────────────────────────────────────────────────
def collect_data(prefix: str):
    rssi_vals, int_vals, dists = [], [], []
    for d in DISTANCES:
        fname = SCRIPT_DIR / f"{prefix}_{d}CM.csv"
        if not fname.exists():
            print(f"  [skip] {fname.name} not found")
            continue
        r, iv, m = load_file(fname)
        rssi_vals.append(r)
        int_vals.append(iv)
        dists.append(d)
        print(f"  {fname.name}  window [{m['start']}:{m['end']}]  "
              f"({m['n_water']}/{m['n_total']} samples, {m['duration']:.1f}s)  "
              f"RSSI {r:.1f} dBm  latency {iv:.1f} ms")
    return np.array(dists), np.array(rssi_vals), np.array(int_vals)


print("Loading tap water …")
fv_dist, fv_rssi, fv_int = collect_data("FV")

print("\nLoading salt water …")
sv_dist, sv_rssi, sv_int = collect_data("SV")


# ── Plot ──────────────────────────────────────────────────────────────────────
C_FV = "#2196F3"
C_SV = "#E53935"

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
fig.patch.set_facecolor("#0D1117")

def style_ax(ax, title, xlabel, ylabel):
    ax.set_facecolor("#161B22")
    ax.set_title(title,   color="#E6EDF3", fontsize=11, pad=8)
    ax.set_xlabel(xlabel, color="#C9D1D9", fontsize=9)
    ax.set_ylabel(ylabel, color="#C9D1D9", fontsize=9)
    ax.tick_params(colors="#8B949E", labelsize=8)
    for sp in ax.spines.values(): sp.set_edgecolor("#30363D")
    ax.grid(True, color="#21262D", linewidth=0.7, linestyle="--")

MK  = dict(marker="o", markersize=5, linewidth=2)
LEG = dict(fontsize=8, facecolor="#161B22", labelcolor="#C9D1D9", edgecolor="#30363D")

if len(fv_dist): ax1.plot(fv_dist, fv_rssi, color=C_FV, label="Tap water",  **MK)
if len(sv_dist): ax1.plot(sv_dist, sv_rssi, color=C_SV, label="Salt water", **MK)
style_ax(ax1, "Signal Strength vs. Distance", "Distance (cm)", "RSSI (dBm)")
ax1.legend(**LEG)

if len(fv_dist): ax2.plot(fv_dist, fv_int, color=C_FV, label="Tap water",  **MK)
if len(sv_dist): ax2.plot(sv_dist, sv_int, color=C_SV, label="Salt water", **MK)
style_ax(ax2, "Latency vs. Distance", "Distance (cm)", "Latency (ms)")
ax2.legend(**LEG)

fig.suptitle(
    "BLE Underwater Transmission — Tap Water vs. Salt Water\n"
    "(averages over auto-detected underwater window)",
    color="#E6EDF3", fontsize=13, fontweight="bold", y=1.02,
)

plt.tight_layout()
out = SCRIPT_DIR / "signal_plots.png"
plt.savefig(out, dpi=150, bbox_inches="tight", facecolor=fig.get_facecolor())
print(f"\nSaved → {out.name}")
plt.show()
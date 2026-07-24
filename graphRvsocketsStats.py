import pandas as pd
import matplotlib.pyplot as plt
import os
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
RESULTS_DIR = SCRIPT_DIR / "/home/andrewh8/src/perftest-rvma/results/csv_tables"


# CSV file list
files = {
    "rsocket_stream_lat.csv": {
        "label": "rsocket-stream",
        "size_col": "size_bytes",
        "lat_col": "avg_send",
        "std_col": "send_stddev",
        "label_offset": (0, 8)
    },

    "rvsocket_stream_progress.csv": {
        "label": "rvsocket-stream",
        "size_col": "size_bytes",
        "lat_col": "avg_send",
        "std_col": "send_stddev",
        "label_offset": (0, 16)
    },


    "ibv_rc_pingpong_lat.csv": {
        "label": "IB Verbs",
        "size_col": "size_bytes",
        "lat_col": "avg rtt",
        "label_offset": (0, 24)
    },

    "rvma_lat.csv": {
        "label": "RVMA",
        "size_col": "size_bytes",
        "lat_col": "avg rtt",
        "std_col": "stddev",
        "label_offset": (0, 32)
    }
}

# Plot
plt.figure(figsize=(10, 6))

def load_batch(filepath, batch_num, points_per_batch=11):
    df = pd.read_csv(filepath)

    start = batch_num * points_per_batch
    end = start + points_per_batch

    return df.iloc[start:end]


# Can change this to use different batch of data
BATCH_TO_USE = 1

for filename, config in files.items():

    filepath = RESULTS_DIR / filename

    df = load_batch(filepath, BATCH_TO_USE)

    sizes = df[config["size_col"]]
    latency = df[config["lat_col"]]

    if "std_col" in config:
        std_dev = df[config["std_col"]]
    else:
        std_dev = latency * 0.10 # Placeholder for now

    line = plt.errorbar(
            sizes,
            latency,
            yerr=std_dev,
            marker='o',
            capsize=4,
            label=config["label"]
        )

    line_color = line.lines[0].get_color()

    for x, y in zip(sizes, latency):
        plt.annotate(
            f"{y:.2f}",
            (x, y),
            textcoords="offset points",
            xytext=config["label_offset"],
            ha="center",
            fontsize=8,
            color=line_color
        )


plt.xscale("log", base=2)

plt.xlabel("Message Size (Bytes)")
plt.ylabel("Average Round-Trip Latency (µs)")

plt.grid(True)

plt.legend()

plt.xticks(
    [1, 4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576],
    ["1", "4", "16", "64", "256", "1KiB", "4KiB", "16KiB", "64KiB", "256KiB", "1MiB"]
)

plt.title("Round-Trip Latency of RVMA vs. IB Verbs")

plt.savefig(
    "/home/andrewh8/src/perftest-rvma/results/graphs/rvsocket" + str(BATCH_TO_USE) + ".png",
    dpi=300,
    bbox_inches="tight"
)
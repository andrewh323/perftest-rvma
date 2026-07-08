import pandas as pd
import matplotlib.pyplot as plt
import os
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
RESULTS_DIR = SCRIPT_DIR / "results"


# CSV file list
files = {
    "rsocket_stream_lat.csv": {
        "label": "rsocket-stream",
        "size_col": "size_bytes",
        "lat_col": "avg_send"
    },

    "rvsocket_stream_progress.csv": {
        "label": "rvsocket-stream",
        "size_col": "size_bytes",
        "lat_col": "avg_send"
    },
    
    "rvma_lat.csv": {
        "label": "Raw RVMA",
        "size_col": "size_bytes",
        "lat_col": "avg rtt"
    }
}

# Plot
plt.figure(figsize=(10, 7))

def load_batch(filepath, batch_num, points_per_batch=11):
    df = pd.read_csv(filepath)

    start = batch_num * points_per_batch
    end = start + points_per_batch

    return df.iloc[start:end]


# Can change this to use different batch of data
BATCH_TO_USE = 3

for filename, config in files.items():

    filepath = RESULTS_DIR / filename

    df = load_batch(filepath, BATCH_TO_USE)

    print(filename)
    print(df)

    sizes = df[config["size_col"]]
    latency = df[config["lat_col"]]

    plt.plot(
        sizes,
        latency,
        marker='o',
        label=config["label"]
    )


plt.xscale("log", base=2)

plt.xlabel("Message Size (bytes)")
plt.ylabel("Average Send Latency (µs)")

plt.grid(True)

plt.legend()

plt.xticks(
    [1, 4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576],
    ["1", "4", "16", "64", "256", "1KiB", "4KiB", "16KiB", "64KiB", "256KiB", "1MiB"]
)

plt.title("Latency Comparison")

plt.savefig(
    "results/graphs/rvma_latency_comparison" + str(BATCH_TO_USE) + ".png",
    dpi=300,
    bbox_inches="tight"
)
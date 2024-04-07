"""
General Description:
This file parses the output files from the tests to create the graphs seen in the paper/report

Authors: Ethan Shama, Nathan Kowal

Reviewers: Nicholas Chivaran, Samantha Hawco
"""

import os
import glob
import re
import csv
import statistics
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
import pandas as pd
import numpy as np

BW = False

# Directory to parse .out files from

if BW:
    directory = './bw benchmark'
else:
    directory = './lat benchmark'

# Regular expressions to match lines of interest
size_trial_regex = re.compile(r'Test Successful for SIZE=(\d+), Trial (\d+).')
rvma_regex = re.compile(r'Total RVMA Elapsed time: (\d+) microseconds')
rdma_regex = re.compile(r'Total RDMA Elapsed time: (\d+) microseconds')
cold_start_rdma_regex = re.compile(r'Total RDMA Cold Start Elapsed time: (\d+) microseconds')
test_failed_regex = re.compile(r'Test Failed for SIZE=(\d+), Trial (\d+)\.')

# Data Storage
data = []
summary = {}

# iterate over all .out files in the given directory
for filepath in glob.glob(os.path.join(directory, '*.out')):
    # open the file
    with open(filepath, 'r') as f:
        for line in f:
            if test_failed_regex.match(line):
                continue

            match = size_trial_regex.match(line)
            if match:
                size, trial = map(int, match.groups())

            match = cold_start_rdma_regex.search(line)
            if match:
                cold_start_rdma_time = int(match.group(1))

            match = rdma_regex.search(line)
            if match:
                rdma_time = int(match.group(1))

            match = rvma_regex.search(line)
            if match:
                rvma_time = int(match.group(1))
                trial_data = [size, trial, rvma_time, rdma_time, cold_start_rdma_time]
                data.append(trial_data)

                # Save in a separate dictionary for calculations
                if size not in summary:
                    summary[size] = {'rvma': [], 'rdma': [], 'cold_start_rdma': []}
                summary[size]['rvma'].append(rvma_time)
                summary[size]['rdma'].append(rdma_time)
                summary[size]['cold_start_rdma'].append(cold_start_rdma_time)

# Sort data by size and trial
data.sort()

# Write data into CSV
results = []
with open(os.path.join(directory, 'benchmark_results.csv'), 'w', newline='') as csv_file:
    writer = csv.writer(csv_file)
    writer.writerow(['Size', 'Trial', 'RVMA Time (µs)', 'RDMA Time (µs)', 'Cold Start RDMA Time (µs)'])  # write the header
    writer.writerows(data)  # write individual trial results

    # Calculate and write the averages and std deviation for each size
    writer.writerow([])  # empty row
    writer.writerow(['Size', 'Average RVMA Time (µs)', 'Min RVMA Time', 'Max RVMA Time', 'Std Dev RVMA Time',
                     'Average RDMA Time (µs)', 'Min RDMA Time', 'Max RDMA Time', 'Std Dev RDMA Time',
                     'Average Cold Start RDMA Time (µs)', 'Min Cold Start RDMA Time', 'Max Cold Start RDMA Time',
                     'Std Dev Cold Start RDMA Time'])  # summary header
    for size in sorted(summary.keys()):
        rvma_avg = statistics.mean(summary[size]['rvma'])
        rvma_min = min(summary[size]['rvma'])
        rvma_max = max(summary[size]['rvma'])
        rvma_std = statistics.stdev(summary[size]['rvma'])

        rdma_avg = statistics.mean(summary[size]['rdma'])
        rdma_min = min(summary[size]['rdma'])
        rdma_max = max(summary[size]['rdma'])
        rdma_std = statistics.stdev(summary[size]['rdma'])

        cold_start_rdma_avg = statistics.mean(summary[size]['cold_start_rdma'])
        cold_start_rdma_min = min(summary[size]['cold_start_rdma'])
        cold_start_rdma_max = max(summary[size]['cold_start_rdma'])
        cold_start_rdma_std = statistics.stdev(summary[size]['cold_start_rdma'])

        writer.writerow(
            [size, rvma_avg, rvma_min, rvma_max, rvma_std, rdma_avg, rdma_min, rdma_max, rdma_std, cold_start_rdma_avg,
             cold_start_rdma_min, cold_start_rdma_max, cold_start_rdma_std])
        results.append([size, rvma_avg, rvma_min, rvma_max, rvma_std, rdma_avg, rdma_min, rdma_max, rdma_std, cold_start_rdma_avg, cold_start_rdma_min, cold_start_rdma_max, cold_start_rdma_std])

results_df = pd.DataFrame(results, columns=['Size', 'Average RVMA Time (µs)', 'Min RVMA Time', 'Max RVMA Time',
                                           'Std Dev RVMA Time', 'Average RDMA Time (µs)', 'Min RDMA Time', 'Max RDMA Time',
                                           'Std Dev RDMA Time', 'Average Cold Start RDMA Time (µs)',
                                           'Min Cold Start RDMA Time', 'Max Cold Start RDMA Time', 'Std Dev Cold Start RDMA Time'])

# Plotting
plt.rcParams["font.size"] = 18
plt.rcParams["axes.titlepad"] = 18
plt.rcParams["axes.labelsize"] = 18
plt.rcParams["xtick.labelsize"] = 18
plt.rcParams["ytick.labelsize"] = 18
fig, ax = plt.subplots(figsize=(10, 8))

def plot_with_error_bars(df, sizes, avg_column, min_column, max_column, label_name, color, marker):
    averages = df[avg_column].values
    errors = [averages - df[min_column].values, df[max_column].values - averages]
    plt.errorbar(sizes, averages, yerr=errors, fmt='-', label=label_name, capsize=5, color=color, marker=marker)

# Generate line plots with error bars for each technique
sizes = results_df['Size'].values
plot_with_error_bars(results_df, sizes, 'Average RVMA Time (µs)', 'Min RVMA Time', 'Max RVMA Time', 'RVMA', 'blue', 'o')
plot_with_error_bars(results_df, sizes, 'Average RDMA Time (µs)', 'Min RDMA Time', 'Max RDMA Time', 'RDMA', 'red', 's')
plot_with_error_bars(results_df, sizes,'Average Cold Start RDMA Time (µs)', 'Min Cold Start RDMA Time', 'Max Cold Start RDMA Time', 'Cold Start RDMA', 'green', '^')

plt.yscale('log', base=2)
plt.xscale('log', base=2)
plt.xlabel("Message Size (bytes)")
plt.ylabel("Execution Time (µs)")
plt.grid(True)

xticks = [4, 32, 128, 2048, 16384, 2 ** 17, 2 ** 20, 2 ** 23]
labels = ['4', '32', '128', '2048', '16KiB', '128KiB', '1MiB', '8MiB']
plt.xticks(ticks=xticks, labels=labels)

if BW:
    yticks = [2 ** i for i in range(10, 23, 2)]
    ylabel = ['1Ki', '4Ki', '16Ki', '64Ki', '256Ki', '1Mi', '4Mi']
    plt.yticks(ticks=yticks, labels=ylabel)

    plt.legend(loc='lower right')
    plt.title('PerfTest BW Benchmark: RVMA vs RDMA vs Cold Start RDMA Execution times')
    plt.savefig(os.path.join(directory, 'BW_comparison.png'), dpi=300, bbox_inches='tight')
else:
    yticks = [2 ** i for i in range(4, 19, 2)]
    ylabel = ['16', '64', '256', '1Ki', '4Ki', '16Ki', '64Ki', '256Ki']
    plt.yticks(ticks=yticks, labels=ylabel)

    plt.legend(loc='center left')
    plt.title('PerfTest LAT Benchmark: RVMA vs RDMA vs Cold Start RDMA Execution times')
    plt.savefig(os.path.join(directory, 'LAT_comparison.png'), dpi=300, bbox_inches='tight')

plt.show()
import re
import concurrent.futures
from bitcoinaddress import Wallet
from multiprocessing import cpu_count, Manager, Lock
from tqdm import tqdm
import time

# Define a function to filter only valid hex characters and symbols
def clean_key(key):
    return ''.join(re.findall(r'[0-9a-fA-F, ]', key))

# Thread function to process and write a chunk of keys
def process_keys_thread(key_batch, lock, output_file):
    local_results = []
    for key in key_batch:
        key = key.strip()
        key = clean_key(key)
        if key:
            try:
                wallet = Wallet(key)
                local_results.append(str(wallet))
            except Exception as e:
                local_results.append(f"Error: {e}")

    # Write local results to file under a lock
    if local_results:
        with lock:
            with open(output_file, 'a') as f:
                for result in local_results:
                    f.write(result + '\n')

# Function to process keys using multithreading
def process_keys_batch(batch, lock, output_file):
    num_threads = 8
    batch_size = len(batch)
    chunk_size = max(1, batch_size // num_threads)
    chunks = [batch[i:i + chunk_size] for i in range(0, batch_size, chunk_size)]

    with concurrent.futures.ThreadPoolExecutor(max_workers=num_threads) as thread_executor:
        futures = [thread_executor.submit(process_keys_thread, chunk, lock, output_file) for chunk in chunks]
        for future in concurrent.futures.as_completed(futures):
            future.result()

# Read all keys from file
def read_keys(file_path):
    with open(file_path, 'r') as file:
        return file.readlines()

# Main execution
def main():
    private_keys = read_keys('infinitesha.txt')
    batch_size = 1000  # Save after every 1000 keys
    total_keys = len(private_keys)
    batches = [private_keys[i:i + batch_size] for i in range(0, total_keys, batch_size)]
    num_workers = cpu_count()

    output_file = 'sha2add.txt'

    # Clear output file
    open(output_file, 'w').close()

    with Manager() as manager:
        lock = manager.Lock()

        with tqdm(total=total_keys, desc="Processing keys", ncols=100,
                  bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} Keys Left: {remaining}") as pbar:

            start_time = time.time()

            with concurrent.futures.ProcessPoolExecutor(max_workers=num_workers) as executor:
                futures = []
                for batch in batches:
                    futures.append(executor.submit(process_keys_batch, batch, lock, output_file))

                for future in concurrent.futures.as_completed(futures):
                    future.result()
                    pbar.update(batch_size)

            end_time = time.time()
            print(f"Processing completed in {end_time - start_time:.2f} seconds")

if __name__ == "__main__":
    main()

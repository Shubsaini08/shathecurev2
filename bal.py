import os
import json
import time
import random
import urllib3
import threading
from urllib3.util import Timeout, Retry
from concurrent.futures import ThreadPoolExecutor, as_completed

# Constants
SATOSHI = 1e8
BATCH_SIZE = 50
THREADS = 4

INPUT_FILE = 'outputs.txt'
BALANCE_FILE = 'balance.txt'
TRANSACTION_FILE = 'transaction.txt'
PUBKEY_FILE = 'pubkeys.txt'
CHECK_LOG_FILE = 'check.log'

HEADERS = [
    {'User-Agent': 'Mozilla/5.0 (Header1)'},
    {'User-Agent': 'Mozilla/5.0 (Header2)'},
    {'User-Agent': 'Mozilla/5.0 (Header3)'},
    {'User-Agent': 'Mozilla/5.0 (Header4)'},
]

# Create HTTP pool managers
http_list = [
    urllib3.PoolManager(timeout=Timeout(5.0), headers=headers)
    for headers in HEADERS
]

lock = threading.Lock()
counters = {
    'checked': 0,
    'with_balance': 0,
    'with_tx': 0,
    'pubkeys_found': 0,
    'skipped': 0
}

def load_addresses():
    if not os.path.exists(INPUT_FILE):
        print(f"Error: {INPUT_FILE} not found.")
        return []

    with open(INPUT_FILE, 'r') as f:
        all_addresses = [line.strip() for line in f if line.strip()]

    already_checked = set()
    if os.path.exists(CHECK_LOG_FILE):
        with open(CHECK_LOG_FILE, 'r') as f:
            already_checked = set(line.strip() for line in f)

    addresses = [addr for addr in all_addresses if addr not in already_checked]
    counters['skipped'] = len(all_addresses) - len(addresses)
    return addresses

def fetch_balance_batch(batch, http):
    url = 'https://blockchain.info/balance?active=' + '|'.join(batch)
    try:
        response = http.request('GET', url)
        return json.loads(response.data.decode('utf-8'))
    except Exception as e:
        print(f"Failed to fetch batch: {e}")
        return None

def fetch_public_key(address, http):
    url = f'https://blockchain.info/rawaddr/{address}'
    try:
        response = http.request('GET', url)
        data = json.loads(response.data.decode('utf-8'))
        for tx in data.get('txs', []):
            for out in tx.get('out', []):
                if out.get('addr') == address and 'spent' in out and out['spent']:
                    for inp in tx.get('inputs', []):
                        script = inp.get('script', '')
                        if len(script) > 130:
                            # Extract hex-encoded pubkey from scriptSig
                            pubkey = script.split(' ')[-1]
                            return pubkey
        return None
    except Exception as e:
        print(f"Error fetching pubkey for {address}: {e}")
        return None

def process_batch(batch, http, step):
    result = fetch_balance_batch(batch, http)
    if not result:
        return

    with open(BALANCE_FILE, 'a') as fbal, open(TRANSACTION_FILE, 'a') as ftx, open(CHECK_LOG_FILE, 'a') as flog, open(PUBKEY_FILE, 'a') as fpub:
        for addr in batch:
            data = result.get(addr, {})
            final_balance = data.get("final_balance", 0) / SATOSHI
            txs = data.get("n_tx", 0)

            with lock:
                counters['checked'] += 1

            if final_balance > 0:
                with lock:
                    counters['with_balance'] += 1
                    fbal.write(f"{addr}\t{final_balance:.8f} BTC\t{txs} txs\n")

            if txs > 0:
                with lock:
                    counters['with_tx'] += 1
                    ftx.write(f"{addr}\t{txs} txs\n")
                    pubkey = fetch_public_key(addr, http)
                    if pubkey:
                        counters['pubkeys_found'] += 1
                        fpub.write(f"{addr}\t{pubkey}\n")

            flog.write(addr + '\n')

def main():
    addresses = load_addresses()
    if not addresses:
        print("No addresses to check.")
        return

    batches = [addresses[i:i+BATCH_SIZE] for i in range(0, len(addresses), BATCH_SIZE)]
    print(f"Loaded {len(addresses)} new addresses. Skipped {counters['skipped']} already checked.")

    with ThreadPoolExecutor(max_workers=THREADS) as executor:
        futures = []
        for step, batch in enumerate(batches):
            http = http_list[step % len(http_list)]
            futures.append(executor.submit(process_batch, batch, http, step))

        for f in as_completed(futures):
            pass  # All work done in thread

    print("Scan Complete:")
    print(f"Checked: {counters['checked']}")
    print(f"With Balance: {counters['with_balance']}")
    print(f"With Transactions: {counters['with_tx']}")
    print(f"Public Keys Found: {counters['pubkeys_found']}")
    print(f"Skipped: {counters['skipped']}")

if __name__ == '__main__':
    main()

"""
download_fr.py -- Federal Register downloader for the DSA search-engine project.

Run:   pip install requests
       python download_fr.py --start 2024-01-01 --end 2025-12-31 --limit 3000     (small test first!)
       python download_fr.py --start 2024-01-01 --end 2025-12-31 --limit 60000    (full run later)

Output (all in ./data):
    metadata.jsonl   one JSON line per document (title, abstract, type, date, agencies, ...)
    texts/<docnum>.txt   full text of each document (cached, so the script is RESUMABLE)
    docs.jsonl       final merged file: metadata + "text" -> this is the input to the C++ indexer
"""
import argparse, json, os, re, statistics, time
from concurrent.futures import ThreadPoolExecutor
from datetime import date, timedelta

import requests

API = "https://www.federalregister.gov/api/v1/documents.json"
FIELDS = ["document_number", "title", "abstract", "type", "publication_date",
          "agency_names", "page_length", "raw_text_url", "html_url"]
HEADERS = {"User-Agent": "NUST-DSA-project (contact: anoor.bese25seecs@seecs.edu.pk)"}  # <-- put your email
DATA = "data"
TEXTS = os.path.join(DATA, "texts")


def get(url, params=None, tries=5):
    """GET with retries/backoff so one network hiccup does not kill a 1-hour download."""
    for i in range(tries):
        try:
            r = requests.get(url, params=params, headers=HEADERS, timeout=60)
            if r.status_code == 200:
                return r
            if r.status_code in (429, 500, 502, 503, 504):
                time.sleep(2 ** i)  # back off and retry
                continue
            r.raise_for_status()
        except requests.RequestException:
            time.sleep(2 ** i)
    return None


def month_slices(start, end):
    """Split [start, end] into calendar-month slices. The API refuses to page past 10,000 results,
    so each slice must hold fewer than 10,000 documents (one month is ~2,000-3,000)."""
    cur = start
    while cur <= end:
        nxt = (cur.replace(day=28) + timedelta(days=4)).replace(day=1)  # first day of next month
        yield cur, min(nxt - timedelta(days=1), end)
        cur = nxt


def fetch_metadata(start, end, limit):
    os.makedirs(DATA, exist_ok=True)
    docs = []
    for a, b in month_slices(start, end):
        page, got = 1, 0
        while True:
            params = [("per_page", 1000), ("page", page), ("order", "oldest"),
                      ("conditions[publication_date][gte]", a.isoformat()),
                      ("conditions[publication_date][lte]", b.isoformat())]
            params += [("fields[]", f) for f in FIELDS]
            r = get(API, params)
            if r is None:
                raise SystemExit(f"API failed for {a}..{b} page {page}")
            js = r.json()
            results = js.get("results", [])
            docs += results
            got += len(results)
            if page >= js.get("total_pages", 1) or not results:
                break
            page += 1
        print(f"{a}..{b}: {got} docs (running total {len(docs)})")
        if got >= 10000:
            print("WARNING: slice hit the 10,000 cap -> split it into smaller date ranges!")
        if len(docs) >= limit:
            break
    docs = docs[:limit]
    with open(os.path.join(DATA, "metadata.jsonl"), "w", encoding="utf-8") as f:
        for d in docs:
            f.write(json.dumps(d, ensure_ascii=False) + "\n")
    return docs


def clean(txt):
    """raw_text_url may wrap the text in HTML (<pre>...); strip tags, unescape basics, squeeze spaces."""
    txt = re.sub(r"<[^>]+>", " ", txt)
    txt = txt.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"')
    return re.sub(r"\s+", " ", txt).strip()


def fetch_text(d):
    """Download one document's full text unless we already have it (resume support)."""
    path = os.path.join(TEXTS, d["document_number"] + ".txt")
    if os.path.exists(path):
        return
    if not d.get("raw_text_url"):
        return
    r = get(d["raw_text_url"])
    if r is None:
        return  # will be picked up on the next run
    with open(path, "w", encoding="utf-8") as f:
        f.write(clean(r.text))
    time.sleep(0.1)  # be polite to the server


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--start", default="2024-01-01")
    ap.add_argument("--end", default="2025-12-31")
    ap.add_argument("--limit", type=int, default=3000)
    ap.add_argument("--threads", type=int, default=6)  # keep this modest
    a = ap.parse_args()

    os.makedirs(TEXTS, exist_ok=True)
    docs = fetch_metadata(date.fromisoformat(a.start), date.fromisoformat(a.end), a.limit)

    print(f"Downloading full text for {len(docs)} documents ...")
    with ThreadPoolExecutor(a.threads) as ex:
        for i, _ in enumerate(ex.map(fetch_text, docs), 1):
            if i % 500 == 0:
                print(f"  {i}/{len(docs)}")

    # Merge metadata + text into the single file the C++ indexer will read.
    counts, kept = [], 0
    with open(os.path.join(DATA, "docs.jsonl"), "w", encoding="utf-8") as out:
        for d in docs:
            p = os.path.join(TEXTS, d["document_number"] + ".txt")
            if not os.path.exists(p):
                continue
            d["text"] = open(p, encoding="utf-8").read()
            n = len(d["text"].split())
            if n < 20:  # junk / empty bodies
                continue
            counts.append(n)
            kept += 1
            out.write(json.dumps(d, ensure_ascii=False) + "\n")

    print(f"Kept {kept} docs. Median words: {statistics.median(counts):.0f} (spec needs >= 100). "
          f"Total words: {sum(counts):,}")


if __name__ == "__main__":
    main()

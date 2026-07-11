#!/usr/bin/env python3
"""
gen_wiki_revisions.py

Generates synthetic "wiki page with revisions" data for testing tools that
operate on repetitive, incrementally-edited documents (dedup, diffing,
delta compression, storage backends, search indexing, etc).

Design goals:
  - Realistic-ish editing pattern: each revision is a SMALL mutation of the
    previous one (a few words inserted/deleted/replaced/reordered), not a
    fresh random document. This is what makes the corpus "repetitive" and
    representative of real wiki history.
  - Streams output to disk (never holds the whole corpus in memory) so it
    scales cleanly to tens of GB.
  - Deterministic given --seed, for reproducible test fixtures.
  - Two output formats:
      xml    -> MediaWiki export-style XML (<mediawiki><page><revision>...)
      jsonl  -> one JSON object per revision (easier to parse/stream in tests)

Usage examples:
  # ~20 GB, MediaWiki XML format, default page/edit shape
  python3 gen_wiki_revisions.py --output dump.xml --target-size-gb 20

  # ~5 GB, JSONL, bigger pages, more revisions per page (more repetition)
  python3 gen_wiki_revisions.py --output dump.jsonl --format jsonl \
      --target-size-gb 5 --words-per-page 800 --revisions-per-page 200

  # Quick smoke test
  python3 gen_wiki_revisions.py --output test.xml --target-size-gb 0.001

  # Seed from your own text file instead of pseudo-random words.
  # Pages are cut as sliding windows over the source text; mutations
  # (inserts/replacements) draw from that same text's vocabulary.
  python3 gen_wiki_revisions.py --output dump.xml --target-size-gb 10 \
      --seed-file my_corpus.txt

  # Seed from an inline string
  python3 gen_wiki_revisions.py --output dump.xml --target-size-gb 1 \
      --seed-text "Lorem ipsum dolor sit amet ..."
"""

import argparse
import random
import sys
import time
import xml.sax.saxutils as saxutils
from datetime import datetime, timedelta

# A modest, fixed vocabulary keeps text realistic-looking and *compressible*
# (real prose has skewed word frequency; pure random tokens would not).
WORD_POOL = (
    "the quick history of local governance often begins with a simple "
    "charter drafted by early settlers who sought order economic growth "
    "and mutual defense against common threats over subsequent decades "
    "the town council expanded its authority to include roads schools "
    "and public health measures while neighboring villages maintained "
    "their own separate traditions of self rule scholars have debated "
    "whether centralized administration improved outcomes for ordinary "
    "residents or merely consolidated power among a small group of "
    "landowners and merchants archival records from this period remain "
    "incomplete though several letters diaries and tax ledgers survive "
    "in regional libraries providing valuable insight into daily life "
    "trade patterns and the slow evolution of legal institutions across "
    "the wider province during times of drought famine and occasional "
    "conflict with rival settlements further south and west"
).split()

USERNAMES = [
    "AnonEditor", "WikiGnome42", "HistBuff", "CiteChecker", "GrammarFixer",
    "NewUser2019", "TalkPageRegular", "LinkPatrol", "IPeditor203.0.113.7",
    "SectionRewriter",
]

EDIT_SUMMARIES = [
    "fix typo", "clarify wording", "added citation", "removed unsourced claim",
    "copyedit", "expanded section", "reverted vandalism", "minor rewording",
    "fixed grammar", "updated date", "added link", "trimmed redundant text",
]


def load_seed_words(seed_file, seed_text):
    """
    Load and tokenize seed source text from a file or inline string.
    Returns a flat list of whitespace-split tokens (punctuation kept
    attached to words, same as real prose -- this is intentional so
    mutated text still "looks like" the source).
    """
    if seed_file:
        with open(seed_file, "r", encoding="utf-8", errors="replace") as f:
            raw = f.read()
    else:
        raw = seed_text
    words = raw.split()
    if len(words) < 20:
        raise ValueError(
            "Seed text/file has too few words (need at least 20) to "
            "generate meaningful pages and mutations from."
        )
    return words


def make_title(rng, page_index, vocab):
    return f"Test_Article_{page_index}_{rng.choice(vocab).strip('.,;:!?').capitalize()}"


def make_base_text(rng, n_words, vocab):
    return " ".join(rng.choice(vocab) for _ in range(n_words))


def make_base_text_from_seed(rng, seed_words, n_words, page_index):
    """
    Cut a page's starting text as a window over the seed corpus rather than
    drawing random words. Window start advances/rotates per page so pages
    aren't all identical when the seed is short relative to n_words, but
    stays deterministic given the seed.
    """
    total = len(seed_words)
    if total <= n_words:
        # Seed shorter than one page: tile it to reach n_words.
        reps = (n_words // total) + 1
        tiled = (seed_words * reps)[:n_words]
        return tiled
    # Slide the window across the corpus; wrap around if we run past the end.
    start = (page_index * max(1, n_words // 3)) % total
    end = start + n_words
    if end <= total:
        return seed_words[start:end]
    return seed_words[start:] + seed_words[: end - total]


def mutate_text(rng, words, edit_intensity, vocab):
    """
    Apply a small number of localized edits to a word list, in place style,
    returning a new list. edit_intensity is the fraction of the document's
    length worth of "edit operations" to apply (kept small -> realistic).
    `vocab` supplies words used for inserts/replacements -- pass the seed
    text's own vocabulary to keep mutations stylistically consistent with
    source content, or WORD_POOL for the default synthetic behavior.
    """
    words = list(words)
    n_ops = max(1, int(len(words) * edit_intensity * rng.random()))
    for _ in range(n_ops):
        op = rng.random()
        if not words:
            words = [rng.choice(vocab)]
            continue
        pos = rng.randrange(len(words))
        if op < 0.4:
            # replace a word
            words[pos] = rng.choice(vocab)
        elif op < 0.7:
            # insert a word
            words.insert(pos, rng.choice(vocab))
        elif op < 0.9 and len(words) > 5:
            # delete a word
            del words[pos]
        else:
            # swap two nearby words (simulates reordering edits)
            pos2 = min(len(words) - 1, pos + rng.randint(1, 3))
            words[pos], words[pos2] = words[pos2], words[pos]
    return words


def xml_escape(s):
    return saxutils.escape(s)


def write_xml_header(f):
    f.write(
        '<mediawiki xmlns="http://www.mediawiki.org/xml/export-0.10/" '
        'version="0.10" xml:lang="en">\n'
    )


def write_xml_footer(f):
    f.write("</mediawiki>\n")


def write_page_xml(f, rng, page_id, title, revisions, start_time):
    f.write("  <page>\n")
    f.write(f"    <title>{xml_escape(title)}</title>\n")
    f.write(f"    <id>{page_id}</id>\n")
    ts = start_time
    for rev_id, (words, summary, user) in enumerate(revisions, start=1):
        ts = ts + timedelta(minutes=rng.randint(5, 600))
        text = " ".join(words)
        f.write("    <revision>\n")
        f.write(f"      <id>{page_id * 100000 + rev_id}</id>\n")
        f.write(f"      <timestamp>{ts.strftime('%Y-%m-%dT%H:%M:%SZ')}</timestamp>\n")
        f.write("      <contributor>\n")
        f.write(f"        <username>{xml_escape(user)}</username>\n")
        f.write("      </contributor>\n")
        f.write(f"      <comment>{xml_escape(summary)}</comment>\n")
        f.write(f"      <text xml:space=\"preserve\" bytes=\"{len(text)}\">"
                 f"{xml_escape(text)}</text>\n")
        f.write("    </revision>\n")
    f.write("  </page>\n")


def write_page_jsonl(f, rng, page_id, title, revisions, start_time):
    import json
    ts = start_time
    for rev_id, (words, summary, user) in enumerate(revisions, start=1):
        ts = ts + timedelta(minutes=rng.randint(5, 600))
        text = " ".join(words)
        record = {
            "page_id": page_id,
            "title": title,
            "revision_id": page_id * 100000 + rev_id,
            "timestamp": ts.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "user": user,
            "comment": summary,
            "text": text,
        }
        f.write(json.dumps(record, ensure_ascii=False) + "\n")


def human_size(n_bytes):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n_bytes < 1000:
            return f"{n_bytes:.2f} {unit}"
        n_bytes /= 1000
    return f"{n_bytes:.2f} PB"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--output", required=True, help="Output file path")
    ap.add_argument("--format", choices=["xml", "jsonl"], default="xml")
    ap.add_argument("--target-size-gb", type=float, required=True,
                     help="Stop once the output file reaches roughly this size (GB)")
    ap.add_argument("--words-per-page", type=int, default=300,
                     help="Approx word count of a page's initial revision (lareger = more repetitive text)")
    ap.add_argument("--revisions-per-page", type=int, default=10,
                     help="Number of revisions to generate per page")
    ap.add_argument("--edit-intensity", type=float, default=0.02,
                     help="Fraction of document length mutated per revision "
                          "(smaller = more repetitive/similar revisions)")
    ap.add_argument("--seed", type=int, default=42,
                     help="RNG seed for reproducibility (unrelated to --seed-file/--seed-text)")
    ap.add_argument("--seed-file", default=None,
                     help="Path to a text file to use as source content instead of "
                          "pseudo-random words. Pages are cut as windows over this "
                          "text and mutations draw from its own vocabulary.")
    ap.add_argument("--seed-text", default=None,
                     help="Inline text to use as source content (alternative to --seed-file)")
    ap.add_argument("--progress-every-mb", type=int, default=500,
                     help="Print a progress line every N MB written")
    args = ap.parse_args()

    if args.seed_file and args.seed_text:
        ap.error("Use either --seed-file or --seed-text, not both.")

    rng = random.Random(args.seed)
    target_bytes = int(args.target_size_gb * (1000 ** 3))
    start_time = datetime(2015, 1, 1)
    next_progress = args.progress_every_mb * 1000 * 1000

    seed_words = None
    if args.seed_file or args.seed_text:
        seed_words = load_seed_words(args.seed_file, args.seed_text)
        # Mutations pull from the source text's own (deduped) vocabulary so
        # edits look like plausible in-context wording rather than injecting
        # the default synthetic word pool into real content.
        vocab = sorted(set(seed_words))
        print(f"Loaded seed text: {len(seed_words)} words, "
              f"{len(vocab)} unique -> using as mutation vocabulary",
              file=sys.stderr)
    else:
        vocab = WORD_POOL

    t0 = time.time()
    page_id = 0

    with open(args.output, "w", encoding="utf-8") as f:
        if args.format == "xml":
            write_xml_header(f)

        while f.tell() < target_bytes:
            page_id += 1
            title = make_title(rng, page_id, vocab)
            if seed_words is not None:
                base_words = make_base_text_from_seed(
                    rng, seed_words, args.words_per_page, page_id)
            else:
                base_words = make_base_text(rng, args.words_per_page, vocab).split()

            revisions = []
            current = base_words
            for _ in range(args.revisions_per_page):
                current = mutate_text(rng, current, args.edit_intensity, vocab)
                summary = rng.choice(EDIT_SUMMARIES)
                user = rng.choice(USERNAMES)
                revisions.append((current, summary, user))

            if args.format == "xml":
                write_page_xml(f, rng, page_id, title, revisions, start_time)
            else:
                write_page_jsonl(f, rng, page_id, title, revisions, start_time)

            pos = f.tell()
            if pos >= next_progress:
                elapsed = time.time() - t0
                rate = pos / elapsed if elapsed > 0 else 0
                print(f"  {human_size(pos)} written, {page_id} pages, "
                      f"{rate/1000/1000:.1f} MB/s", file=sys.stderr)
                next_progress += args.progress_every_mb * 1000 * 1000

        if args.format == "xml":
            write_xml_footer(f)

    import os
    final_size = os.path.getsize(args.output)
    elapsed = time.time() - t0
    print(f"Done: {args.output} ({human_size(final_size)}), "
          f"{page_id} pages, {page_id * args.revisions_per_page} revisions, "
          f"{elapsed:.1f}s", file=sys.stderr)


if __name__ == "__main__":
    main()

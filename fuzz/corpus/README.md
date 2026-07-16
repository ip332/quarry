# BRF fuzz seed corpus

Corpus entries are stored as whitespace-separated hexadecimal bytes so the
seeds remain reviewable in source control.

Use `fuzz/run_seed_corpus.py` to convert these files to temporary raw inputs and
run a fuzz executable against them.

#!/bin/sh
# Clones, builds and runs the cross-project comparison in the README.
#
# Not wired into the Makefile because it needs the network and three foreign
# repositories. Everything it fetches lands in compare/rivals, which is ignored.
#
# Each rival gets its own best entry point: ACE ships six variants and its own
# README names ace_eval_decompress.c as the fastest, which is also the table-free
# one. -flto matters, because ACE and phevaluator are separate translation units
# and would otherwise pay a call per hand that the header-inline evaluators do not.
set -e
cd "$(dirname "$0")"
mkdir -p rivals && cd rivals

clone() { [ -d "$(basename "$1")" ] || git clone --depth 1 -q "$1"; }
clone https://github.com/ashelly/ACE_eval
clone https://github.com/zekyll/OMPEval
clone https://github.com/HenryRLee/PokerHandEvaluator

FLAGS="-O3 -mcpu=native -flto"
PHE=PokerHandEvaluator/cpp
mkdir -p obj
cc $FLAGS -c ACE_eval/ace_eval_decompress.c -o obj/ace.o 2>/dev/null
c++ $FLAGS -std=c++17 -c OMPEval/omp/HandEvaluator.cpp -o obj/omp.o
for f in evaluator7 hash hashtable hashtable7 dptables tables_bitwise 7462; do
    cc $FLAGS -I$PHE/include -c $PHE/src/$f.c -o obj/$f.o
done

c++ $FLAGS -std=c++17 -I.. -I../.. -IOMPEval -IACE_eval -I$PHE/include \
    -o shootout ../shootout.cpp obj/*.o
exec ./shootout

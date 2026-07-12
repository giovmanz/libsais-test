#!/usr/bin/env bash

# compute SA and LCP array for all files passed on the command line
# version specialized for capsa-sa with default input -tyep (t)
# note that the g input type only works for files containing 
# only the four symbols ACGT 

# exit immediately on error
set -e
# file containing the sha1sum of newly computed sa/lcp
sha1file=Csha1.out

if [ $# -lt 3 ]
then
  echo "Usage:"
  echo "         $0 exe threads file1 [file2 ...]"
  echo
  echo "Compute SA and LCP for all files on the command line"
  echo "reporting running time and peak memory usage"
  echo "compute sha1sum's for all files and write them to $sha1file"
  echo
  echo "Post and pre-processing for the caps-sa algorithm text mode"
  echo
  echo "Sample usage:"
  echo "         $0 \"bin/caps_sa\" 4 corpus/* "      
  exit
fi

exe=$1
export PARLAY_NUM_THREADS=$2
shift 2

echo ">>>>>>> deleting $sha1file"
rm -f $sha1file


for f in "$@"
do 
  echo ">>>>>>> covert file uint8 -> int8"
  Cu8_to_i8.py $f $f.i8
  echo ">>>>>>> sa and lcp computation "
  /usr/bin/time -o $f.stat -f"Command %C\n####### E(secs):%e Mem(kb):%M" $exe --output-lcp --data-type t $f.i8 $f.salcp 2>/dev/null
  cat $f.stat
  Csplit.py $f.salcp 
  # compute sha1sum and delete aux files 
  sha1sum  $f $f.lcp $f.sa >> $sha1file 
  rm -f $f.sa $f.lcp $f.salcp $f.stat $f.i8 
  sleep 2
done

#!/bin/kbash
# hello.sh - a kbash script.  Run: kbash hello.sh world
echo "Hello, ${1}! You are in $(pwd)"
for_each_file=$(ls /home)
echo "files here: $for_each_file"
uname -a && echo "status was $?"

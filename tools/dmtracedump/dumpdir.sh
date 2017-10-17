#!/bin/sh

FILES=`ls $1/*.data | sed -e"s/^\\(.*\\).data$/\\1/"`

mkdir -p "$2"

for F in $FILES
do
    G="$2/`echo $F | sed -e"s/.*\\///g"`.html"
    dmtracedump -h -p "$F" > "$G"
done

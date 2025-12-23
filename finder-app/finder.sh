#!/bin/sh 
#bash

if [ $# -ne 2 ]; then
        echo "Error: 2 arguments needed "
        exit 1
fi

filesdir="$1"
searchstr="$2"


if [ ! -d "$filesdir" ];then
	echo "Error: Directory $filesdir does not exist"
	exit 1
fi

file_count=$(find "$filesdir" -type f | wc -l)

match_found=$(grep -R "$searchstr" "$filesdir" | wc -l)

echo " The number of files are $file_count and the number of matching lines are $match_found"

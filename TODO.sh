#!/bin/sh

FILES='src/*'
grep -n --colour=always -r TODO $FILES | sed -re 's/(.+:.+:)(\x1b\[m\x1b\[K)[[:space:]]*(.*)/\1\x01\2\3/'

#!/bin/sh
# Frame rate of the first seconds on q3dm1: fps-ioq3.sh; runs of 500 and 3500 frames per renderer, the difference gives frames/s.
cd $HOME/ioq3port
for r in opengl1 opengl2; do
	for n in 500 3500; do
		s=$(date +%s)
		./run-ioq3.sh $r +devmap q3dm1 +wait $n +quit > fps-$r-$n.log 2>&1
		e=$(date +%s)
		echo "$r $n $((e - s))" >> fps.txt
	done
done
echo done >> fps.txt

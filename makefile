hellomake: main.c
	gcc -Wall streamer.c -o streamer $(shell pkg-config --cflags --libs gstreamer-1.0)



all: client server decodetimeoverlay libgsttimeoverlayparse.so

CFLAGS?=-Wall -Werror -O2

libgsttimeoverlayparse.so : \
        gsttimestampoverlay.c \
        gsttimestampoverlay.h \
        gsttimeoverlayparse.c \
        gsttimeoverlayparse.h \
        plugin.c
	$(CC) -o$@ --shared -fPIC $^ $(CFLAGS) \
	    $$(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0)

decodetimeoverlay : decodetimeoverlay.c
	$(CC) -o$@ $^ $(CFLAGS) $$(pkg-config --cflags --libs gstreamer-1.0) -lm

zaysan-server : zaysan-server.c
	$(CC) -o$@ $^ $(CFLAGS) $$(pkg-config --cflags --libs gstreamer-1.0) -lm

server : server.c
	$(CC) -o$@ $^ $(CFLAGS) $$(pkg-config --cflags --libs gstreamer-1.0) -lm

client : client.c
	$(CC) -o$@ $^ $(CFLAGS) $$(pkg-config --cflags --libs gstreamer-1.0)

dist:
	git archive -o latency-clock-0.0.1.tar HEAD --prefix=latency-clock-0.0.1/

install:

clean:
	rm -f client server decodetimeoverlay gsttimestampoverlay.so

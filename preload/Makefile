all: libpreload.so

cppflags=-g -O2 -std=c++17 -fPIC -fvisibility=hidden -shared -fno-omit-frame-pointer

ifeq ($(LOGONLY), 1)
	cppflags += -DLOG_ONLY
endif

ifeq ($(DEBUG), 1)
	cppflags += -DPRELOAD_DEBUG
endif

ifeq ($(IMAGE_CRPD), 1)
	cppflags += -DIMAGE_CRPD
endif

ifeq ($(IMAGE_BIRD), 1)
	cppflags += -DIMAGE_BIRD
endif

SRC_FILES = preload.cpp\
	preload_nohdr.cpp\
	netlink.cpp\
	tcp.cpp\
	udp.cpp\
	fdesc.cpp\
	debug_nl.cpp\
	debug.cpp

HDR_FILES = debug.h\
	netlink.h\
	tcp.h\
	udp.h\
	fdesc.h\
	preload.h\
	util.h

libpreload.so: ${SRC_FILES} ${HDR_FILES} Makefile
	g++ ${cppflags} ${SRC_FILES} -fpermissive -mcx16 -pthread -lrt -ldl -o libpreload.so

clean:
	rm -f *.so

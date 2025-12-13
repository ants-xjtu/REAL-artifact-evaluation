all: controller

cppflags=-g -O2 -std=c++20 -fPIC -fno-omit-frame-pointer -Wall

ifeq ($(DEBUG), 1)
	cppflags += -DMNG_DEBUG
endif

ifeq ($(ITER_CONV), 1)
    cppflags += -DITER_CONV
endif

SRC_FILES = \
	main.cpp \
	channel.cpp \
	channel_manager.cpp \
	replay_manager.cpp \
	node_ops.cpp \
	remote_channel.cpp \
	remote_worker.cpp \
	const.cpp \

HDR_FILES = \
	message.hpp \
	debug.hpp \
	channel.hpp \
	channel_manager.hpp \
	replay_manager.hpp \
	node_ops.hpp \
	remote_channel.hpp \
	remote_worker.hpp \
	const.hpp \
	json.hpp \
	ring_buffer.hpp \

controller: ${SRC_FILES} ${HDR_FILES} Makefile
	g++ ${cppflags} ${SRC_FILES} -o controller

clean:
	rm -f controller

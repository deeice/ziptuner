SOURCES := cJSON.c ziptuner.c
OBJS := $(SOURCES:.c=.o)

CFLAGS_IZ2S := -Os -pthread
CFLAGS := -g
LFLAGS := -Wl,--unresolved-symbols=ignore-in-shared-libs -L/usr/share/gcc/wrt/usr/lib
LIBS := -lcurl -lm

all: ziptuner

ziptuner: $(OBJS)
	$(CC) $(CFLAGS) -o ziptuner $(OBJS) $(LFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<


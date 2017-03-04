SOURCES := cJSON.c ziptuner.c
OBJS := $(SOURCES:.c=.o)

CFLAGS := -g
LFLAGS := -Wl,--unresolved-symbols=ignore-in-shared-libs -L/usr/share/gcc/wrt/usr/lib
LIBS := -lcurl -lm

all: ziptuner

ziptuner: $(OBJS)
	$(CC) $(CFLAGS) -o ziptuner $(OBJS) $(LFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<


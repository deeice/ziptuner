SOURCES := cJSON.c ziptuner.c
OBJS := $(SOURCES:.c=.o)

CFLAGS := -g

LIBS := -lcurl -lm

all: ziptuner

ziptuner: $(OBJS)
	$(CC) $(CFLAGS) -o ziptuner $(OBJS) $(LFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<


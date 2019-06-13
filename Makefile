CFLAGS=-O2 -W -Wall -Werror -pedantic -std=c11
OBJS=regexomatic.o main.o
APP=regexomatic

.PHONY=all clean test

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(CFLAGS) -o $(APP) $(OBJS)

%.o: %.c
	$(CC) -c $(CFLAGS) $<

test: $(APP)
	./$(APP) ./words.txt

clean:
	$(RM) -f $(APP) $(OBJS)

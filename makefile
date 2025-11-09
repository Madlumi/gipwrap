
CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = gipwrap

SRCDIR = src
OBJDIR = obj

SRCS = $(SRCDIR)/main.c $(SRCDIR)/ai_core.c $(SRCDIR)/gippy.c $(SRCDIR)/claud.c $(SRCDIR)/deepy.c $(SRCDIR)/ollama.c
OBJS = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/ai.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) ~/scripts/runnable

.PHONY: all clean install

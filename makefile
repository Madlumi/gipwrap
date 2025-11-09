
CC = gcc
CFLAGS = -Wall -Wextra -O2 -Isrc
TARGET = gipwrap

SRCDIR = src
OBJDIR = obj

AIIMPLDIR = $(SRCDIR)/aiImpl

SRCS = \
	$(SRCDIR)/main.c \
	$(SRCDIR)/ai_core.c \
	$(AIIMPLDIR)/gippy.c \
	$(AIIMPLDIR)/claud.c \
	$(AIIMPLDIR)/deepy.c \
	$(AIIMPLDIR)/ollama.c
OBJS = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/ai.h | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) ~/scripts/runnable

.PHONY: all clean install

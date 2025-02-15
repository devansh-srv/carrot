CFLAGS = -g -I./lib -Wall -Wextra

SRCDIR = src
BUILDDIR = build

SOURCES = $(wildcard $(SRCDIR)/*.c)

OBJECTS = $(patsubst $(SRCDIR)/*.c, $(BUILDDIR)/%.o, $(SOURCES))

TARGET = $(BUILDDIR)/bin

all:$(TARGET)
	@echo "Build complete"


$(TARGET): $(OBJECTS)
	gcc $(CFLAGS) $^ -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/$.c
	gcc $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR)/*

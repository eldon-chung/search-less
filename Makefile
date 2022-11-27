all: my_all

DEBUG := 1
SANITIZE := $(DEBUG)

CXX := clang++
ifeq ($(DEBUG), 1)
CXXOPT := -O0
else
CXXOPT := -O3
endif
CXXWARNINGS := -Wall -Wextra -pedantic -Wimplicit-fallthrough
CXXWERROR := -Werror
CXXWERROR += -Wno-error=unused-parameter
CXXWERROR += -Wno-error=unused-private-field
CXXWERROR += -Wno-error=unused-variable
CXXWERROR += -Wno-error=unused-function
CXXWERROR += -Wconversion
CXXWERROR += -Wtautological-constant-out-of-range-compare
CXXWERROR += -Wno-unused-but-set-variable
CXXFLAGS := -g -std=c++20 -Isrc -ftime-trace -fno-omit-frame-pointer
ifeq ($(SANITIZE), 1)
CXXFLAGS += -fsanitize=thread
endif
CXXFLAGS += $(CXXOPT)
CXXFLAGS += $(CXXWARNINGS)
CXXFLAGS += $(CXXWERROR)

# build dir
ifeq ($(DEBUG), 1)
BUILDDIR := build
else
BUILDDIR := build-release
endif
$(BUILDDIR):
	mkdir -p $(BUILDDIR)
PRECIOUS_TARGETS += $(BUILDDIR)

# sources
SRCS := $(shell find src -iname "*.cpp")
OBJS := $(SRCS:%.cpp=%.o)

$(BUILDDIR)/%.o: %.cpp Makefile
	mkdir -p $(shell dirname $@)
	$(COMPILE.cpp) $(@:$(BUILDDIR)/%.o=%.cpp) -MMD $(OUTPUT_OPTION)
PRECIOUS_TARGETS += $(BUILDDIR)/%.o

# Example: adding absl_hash
# PKGCONFIG_LIBS += absl_hash
# PKGCONFIG_LIBS += openssl
# PKGCONFIG_LIBS += ncurses
# PKGCONFIG_LIBS += readline

# Example: adding boost_system (can't use pkg-config cause they dumb)
# LDFLAGS += -lboost_system
LDFLAGS += -lncurses

# Example: adding boost asio
# # Remember to add `openssl` and `boost_system` manually...
# # First we need this magic flag to enable separate compilation for (slightly) faster compile times
# CXXFLAGS += -DBOOST_ASIO_SEPARATE_COMPILATION
# # Next make the .o file for the boost lib
# $(BUILDDIR)/boost_asio_ssl.o: Makefile
# 	mkdir -p $(shell dirname $@)
# 	echo $$'#include <boost/asio/impl/src.hpp>\n#include <boost/asio/ssl/impl/src.hpp>' | \
# 		$(COMPILE.cpp) -MD $(OUTPUT_OPTION) -x c++ -
# PRECIOUS_TARGETS += $(BUILDDIR)/boost_asio_ssl.o
# # Add it to our list of object files
# OBJS += boost_asio_ssl.o

# Finish specifying deps

.PRECIOUS: $(PRECIOUS_TARGETS)

ifeq ($(PKGCONFIG_LIBS), "")
CXXFLAGS += $(shell pkg-config --cflags $(PKGCONFIG_LIBS))
CFLAGS += $(shell pkg-config --cflags $(PKGCONFIG_LIBS))
LDFLAGS += $(shell pkg-config --libs $(PKGCONFIG_LIBS))
endif

# Targets

BUILDOBJS := $(OBJS:%=$(BUILDDIR)/%)
$(BUILDDIR)/main.out: $(BUILDOBJS) Makefile
	mkdir -p $(shell dirname $@)
	$(LINK.cpp) $(BUILDOBJS) -MMD $(LOADLIBES) $(LDLIBS) $(OUTPUT_OPTION)

my_all: $(BUILDDIR)/main.out;
# elditor: $(BUILDDIR)/elditor.out;

# test: $(BUILDDIR)/test.out;

clean: Makefile
	rm -fr $(BUILDDIR)

format:
	clang-format -i $(SRCS)

.PHONY: format;

-include $(BUILDDIR)/**/*.d

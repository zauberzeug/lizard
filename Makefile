#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := lizard
PROJECT_VER := $(shell git describe --always | git log -1 --format=%ci) $(shell git describe --always --tags --dirty)
EXTRA_COMPONENT_DIRS += $(PROJECT_PATH)/src

include $(IDF_PATH)/make/project.mk

#########################################################################
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# The Free Software Foundation; version 3 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# Copyright @ 2007, 2009 Astfin
# Primary Authors: Dimitar Penev dpn@ucpbx.com
# Copyright @ 2010 SwitchFin dpn@switchfin.org
#########################################################################
#############################################
# DAHDI package for SwitchFin.org
#############################################

BFSPI_VERSION=0.5
BFSPI_NAME=bfspi-$(BFSPI_VERSION)
BFSPI_DIR=$(BUILD_DIR)/$(BFSPI_NAME)
BFSPI_SOURCE=$(BFSPI_NAME).tar.gz
BFSPI_SITE=http://downloads.asterisk.org/pub/telephony/dahdi-linux-complete/releases
BFSPI_UNZIP=zcat
BFSPI_SOURCES=$(SOURCES_DIR)/bfspi
BFSPI_MODULES_EXTRA=
TARGET_KERNEL_MODULES=$(shell ls -d $(TARGET_DIR)/lib/modules/*sw* | tail -n1)

$(BFSPI_DIR)/.unpacked:
	mkdir -p $(BFSPI_DIR)
	mkdir -p $(BFSPI_DIR)/driver
	mkdir -p $(BFSPI_DIR)/tools
	cp -f $(BFSPI_SOURCES)/bfspi.h $(BFSPI_DIR)/driver/bfspi.h			
	cp -f $(BFSPI_SOURCES)/bfspi.c $(BFSPI_DIR)/driver/bfspi.c
	cp -f $(BFSPI_SOURCES)/bfspi.mk $(BFSPI_DIR)/Makefile
	cp -f $(BFSPI_SOURCES)/bfspi-tools.mk $(BFSPI_DIR)/tools/Makefile
	cp -f $(BFSPI_SOURCES)/bfspi-drivers.mk $(BFSPI_DIR)/driver/Makefile
	cp -f $(BFSPI_SOURCES)/bfspi-test.c $(BFSPI_DIR)/tools/bfspi-test.c
	cp -f $(BFSPI_SOURCES)/spidev-test.c $(BFSPI_DIR)/tools/spidev-test.c
	touch $(BFSPI_DIR)/.unpacked

$(BFSPI_DIR)/.linux: $(BFSPI_DIR)/.unpacked
        # build DAHDI kernel modules
	cd $(BFSPI_DIR)/driver; \
	$(MAKE1) -C $(UCLINUX_DIR)/linux-2.6.x SUBDIRS=$(BFSPI_DIR)/driver
	touch $(BFSPI_DIR)/.linux

$(BFSPI_DIR)/.tools: $(BFSPI_DIR)/.unpacked
	cd $(BFSPI_DIR)/tools; \
	$(MAKE1) CC=$(TARGET_CC) -C $(BFSPI_DIR)/tools all
	touch $(BFSPI_DIR)/.tools

$(BFSPI_DIR)/.configured: $(BFSPI_DIR)/.linux $(BFSPI_DIR)/.tools
	touch $(BFSPI_DIR)/.configured


bfspi: $(BFSPI_DIR)/.configured
	mkdir -p $(TARGET_KERNEL_MODULES)/misc
	cp -f $(BFSPI_DIR)/driver/bfspi.ko $(TARGET_KERNEL_MODULES)/misc
	#cp -f $(BFSPI_DIR)/tools/bfspi-test  $(TARGET_DIR)/usr/bin

bfspi-tools-clean:
	rm -rf $(BFSPI_DIR)/.tools
	rm -rf $(BFSPI_DIR)/tools/bfspi-test
	rm -rf $(BFSPI_DIR)/tools/spidev-test

bfspi-clean:
	rm -rf $(BFSPI_DIR)/.linux
	rm -rf $(BFSPI_DIR)/driver/bfspi.ko
	rm -rf $(BFSPI_DIR)/driver/bfspi.o

bfspi-base-clean: tools-clean bfspi-clean
	rm -rf $(BFSPI_DIR)/.unpacked
	rm -rf $(BFSPI_DIR)/.configured
	
################################################
#
# Toplevel Makefile options
#
#################################################
TARGETS+=bfspi

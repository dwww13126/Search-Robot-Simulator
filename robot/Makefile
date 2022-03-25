# robot Makefile to test a small map (21x21 with 2 survivors and 2 mines)
# 
# name of your application
APPLICATION = robot

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../..

# map
NUM_LINES ?= 21
NUM_COLUMNS ?= 21
# x,y,x,y,x,y...
SURVIVOR_LIST ?= 2,3,18,19
# x,y,x,y,x,y...
MINES_LIST ?= 15,4,8,14

MAX_MINES ?= 4
ENERGY ?= 200

CFLAGS += -DNUM_LINES=$(NUM_LINES)
CFLAGS += -DNUM_COLUMNS=$(NUM_COLUMNS)
CFLAGS += -DSURVIVOR_LIST=\"$(SURVIVOR_LIST)\"
CFLAGS += -DMINES_LIST=\"$(MINES_LIST)\"
CFLAGS += -DROBOT_PORT=$(ROBOT_PORT)
CFLAGS += -DROBOT_ID=$(ROBOT_ID)
CFLAGS += -DMAX_MINES=$(MAX_MINES)
CFLAGS += -DENERGY=$(ENERGY)

# Include packages that pull up and auto-init the link layer.
# NOTE: 6LoWPAN will be included if IEEE802.15.4 devices are present
USEMODULE += gnrc_netdev_default
USEMODULE += auto_init_gnrc_netif
# Activate ICMPv6 error messages
USEMODULE += gnrc_icmpv6_error
# Specify the mandatory networking modules for IPv6 and UDP
USEMODULE += gnrc_ipv6_router_default
USEMODULE += gnrc_udp
USEMODULE += gnrc_sock_udp

# Add also the shell, some shell commands
USEMODULE += shell
USEMODULE += shell_commands

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1
# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include

# Set a custom channel if needed
include $(RIOTMAKE)/default-radio-settings.inc.mk

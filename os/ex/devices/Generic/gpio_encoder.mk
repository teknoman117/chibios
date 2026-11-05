# List of all the GPIO encoder device files.
GPIOENCODERSRC := $(CHIBIOS)/os/ex/devices/Generic/gpio_encoder.c

# Required include directories
GPIOENCODERINC := $(CHIBIOS)/os/ex/include \
                  $(CHIBIOS)/os/ex/devices/Generic

# Shared variables
ALLCSRC += $(GPIOENCODERSRC)
ALLINC  += $(GPIOENCODERINC)

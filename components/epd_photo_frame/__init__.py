"""
ESPHome component for EPD Photo Frame with Spectra6 e-paper display.
Supports downloading and displaying images from URLs.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, spi
from esphome import pins
from esphome.const import (
    CONF_ID,
    CONF_RESET_PIN,
    CONF_DC_PIN,
    CONF_BUSY_PIN,
    CONF_UPDATE_INTERVAL,
)
from esphome.core import CORE
from esphome.cpp_helpers import gpio_pin_expression

CODEOWNERS = ["@ahadi"]
DEPENDENCIES = ["spi", "display"]
AUTO_LOAD = ["display"]

# Define component-specific config keys that are not in esphome.const
CONF_POWER_PIN = "power_pin"
CONF_CS_MASTER_PIN = "cs_master_pin"
CONF_CS_SLAVE_PIN = "cs_slave_pin"
CONF_IMAGE_URL = "image_url"

epd_photo_frame_ns = cg.esphome_ns.namespace("epd_photo_frame")
EPDPhotoFrame = epd_photo_frame_ns.class_(
    "EPDPhotoFrame", cg.PollingComponent, display.DisplayBuffer
)

CONFIG_SCHEMA = display.BASIC_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(EPDPhotoFrame),
        cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
        cv.Required(CONF_POWER_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_CS_MASTER_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_CS_SLAVE_PIN): pins.gpio_output_pin_schema,
        cv.Optional(
            CONF_IMAGE_URL, default="http://10.0.0.253:8080/image.bin"
        ): cv.string,
        cv.Optional(CONF_UPDATE_INTERVAL, default="30min"): cv.update_interval,
    }
).extend(cv.polling_component_schema("30min"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await display.register_display(var, config)

    # SPI device registration omitted to avoid requiring cs_pin in YAML

    # Add pins
    reset_pin = await gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_reset_pin(reset_pin))

    dc_pin = await gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc_pin))

    busy_pin = await gpio_pin_expression(config[CONF_BUSY_PIN])
    cg.add(var.set_busy_pin(busy_pin))

    power_pin = await gpio_pin_expression(config[CONF_POWER_PIN])
    cg.add(var.set_power_pin(power_pin))

    cs_master_pin = await gpio_pin_expression(config[CONF_CS_MASTER_PIN])
    cg.add(var.set_cs_master_pin(cs_master_pin))

    cs_slave_pin = await gpio_pin_expression(config[CONF_CS_SLAVE_PIN])
    cg.add(var.set_cs_slave_pin(cs_slave_pin))

    # Set image URL
    cg.add(var.set_image_url(config[CONF_IMAGE_URL]))

    # Set update interval
    if CONF_UPDATE_INTERVAL in config:
        cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))

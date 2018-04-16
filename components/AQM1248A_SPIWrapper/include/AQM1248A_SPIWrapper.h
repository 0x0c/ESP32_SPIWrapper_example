#pragma once

#include <SPIWrapper.h>
#include <cstring>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <soc/gpio_struct.h>

namespace m2d
{
namespace ESP32
{
	namespace AQM1248A_SPIWrapper
	{
		class LCD
		{
		private:
			enum class Command : uint8_t
			{
				DisplayOff = 0xae,
				DisplayOn = 0xaf,

				ADC_Normal = 0xa0,
				ADC_Reverse = 0xa1,

				DisplayNormal = 0xa6,
				DisplayReverse = 0xa7,

				DisplayAllPointsNormal = 0xa4,
				DisplayAllPointsOn = 0xa5,

				LCD_Bias_1_9 = 0xa2,
				LCD_Bias_1_7 = 0xa3,

				InternalReset = 0xe2,

				SetSleepMode = 0xab,
				SetNormalMode = 0xac,

				ElectronicVolumeModeSet = 0x81,
				ElectronicVolumeRegistereSet = 0x00,

				RegisterRatio = 0x20,

				PageAddressSet = 0xb0,
				ColumnAdderssSet1 = 0x10,
				ColumnAdderssSet2 = 0x00,

				DisplayStartLineSet = 0x40,

				CommonOutputNormal = 0xc0,
				CommonOutputReverse = 0xc8,

				PowerControl1 = 0x2c,
				PowerControl2 = 0x2e,
				PowerControl3 = 0x2f,
			};

			m2d::ESP32::SPIWrapper *spi;
			gpio_num_t rs_gpio;
			gpio_num_t reset_gpio;

			static uint8_t command_data(Command c)
			{
				return static_cast<uint8_t>(c);
			}

			void write_commands(const uint8_t *command, int byte_length)
			{
				gpio_set_level(this->rs_gpio, 0);
				for (int i = 0; i < byte_length; i++) {
					this->spi_transaction(&command[i], 1);
				}
			}

			void write_data(const uint8_t *data, int byte_length)
			{
				gpio_set_level(this->rs_gpio, 1);
				this->spi_transaction(data, byte_length);
			}

			void spi_transaction(const uint8_t *data, int byte_length)
			{
				m2d::ESP32::SPITransaction t;
				t.set_tx_buffer(data, byte_length);
				assert(this->spi->transmit(t) == ESP_OK);
			}

		public:
			enum class SleepMode : uint8_t
			{
				Enabled,
				Disabled
			};

			enum class DisplayMode : uint8_t
			{
				On,
				Off
			};

			enum class DisplayRotation : uint8_t
			{
				Standard,
				Flip
			};

			enum class DisplayColor : uint8_t
			{
				Normal,
				Inverted
			};

			uint8_t display_ram[6][128];

			LCD(gpio_num_t sclk, gpio_num_t sdi, gpio_num_t cs, gpio_num_t rs, gpio_num_t reset, spi_host_device_t host = HSPI_HOST)
			{
				this->rs_gpio = rs;
				this->reset_gpio = reset;
				gpio_config_t io_conf;
				io_conf.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_DISABLE;
				io_conf.mode = GPIO_MODE_OUTPUT;
				io_conf.pin_bit_mask = ((1 << this->rs_gpio) | (1 << this->reset_gpio));
				io_conf.pull_down_en = (gpio_pulldown_t)GPIO_PULLDOWN_DISABLE;
				io_conf.pull_up_en = (gpio_pullup_t)GPIO_PULLUP_DISABLE;
				gpio_config(&io_conf);
				gpio_set_level(this->reset_gpio, 1);

				this->spi = new SPIWrapper(5000000, 3, sclk, (gpio_num_t)-1, sdi, cs, host);

				// initialize commands
				uint8_t c = LCD::command_data(Command::DisplayOff);
				this->write_commands(&c, 1);
				c = LCD::command_data(Command::ADC_Normal);
				this->write_commands(&c, 1);
				c = LCD::command_data(Command::CommonOutputReverse);
				this->write_commands(&c, 1);
				c = LCD::command_data(Command::LCD_Bias_1_7);
				this->write_commands(&c, 1);

				// power on
				c = LCD::command_data(Command::PowerControl1);
				this->write_commands(&c, 1);
				vTaskDelay(2 / portTICK_RATE_MS);
				c = LCD::command_data(Command::PowerControl2);
				this->write_commands(&c, 1);
				vTaskDelay(2 / portTICK_RATE_MS);
				c = LCD::command_data(Command::PowerControl3);
				this->write_commands(&c, 1);

				// contrast settings
				this->set_contrast(3);
				c = 0x81;
				this->write_commands(&c, 1);
				c = 0x1c;
				this->write_commands(&c, 1);

				// display settings
				c = LCD::command_data(Command::DisplayAllPointsNormal);
				this->write_commands(&c, 1);
				c = LCD::command_data(Command::DisplayStartLineSet);
				this->write_commands(&c, 1);
				c = LCD::command_data(Command::DisplayNormal);
				this->write_commands(&c, 1);
				c = LCD::command_data(Command::DisplayOn);
				this->write_commands(&c, 1);
			}

			~LCD()
			{
				this->spi->detach();
				delete this->spi;
			}

			void reset()
			{
				gpio_set_level(this->reset_gpio, 0);
				gpio_set_level(this->reset_gpio, 1);
			}

			void flush()
			{
				std::memset(&this->display_ram, 0, sizeof(this->display_ram));
				this->draw();
			}

			esp_err_t set_page_address(uint8_t address)
			{
				if (address > 0b00001111) {
					return ESP_FAIL;
				}
				uint8_t command = LCD::command_data(Command::PageAddressSet) | address;
				this->write_commands(&command, 1);

				return ESP_OK;
			}

			void set_column_address(uint8_t address)
			{
				uint8_t command1 = LCD::command_data(Command::ColumnAdderssSet1) | ((address & 0xf0) >> 4);
				uint8_t command2 = LCD::command_data(Command::ColumnAdderssSet2) | (address & 0x0f);
				uint8_t commands[2] = {
					command1,
					command2
				};
				this->write_commands(commands, 2);
			}

			void set_data(uint8_t *data, int size)
			{
				std::memcpy(this->display_ram, data, size);
			}

			void draw()
			{
				for (int page = 0; page < 6; page++) {
					this->set_page_address(page);
					this->set_column_address(0x00);
					uint8_t *d = this->display_ram[page];
					this->write_data(d, 128);
				}
			}

			esp_err_t set_display_mode(DisplayMode mode)
			{
				uint8_t command = LCD::command_data(Command::DisplayOn);
				if (mode == DisplayMode::On) {
					this->write_commands(&command, 1);
					return ESP_OK;
				}
				else if (mode == DisplayMode::Off) {
					command = LCD::command_data(Command::DisplayOff);
					this->write_commands(&command, 1);
					return ESP_OK;
				}
				return ESP_FAIL;
			}

			esp_err_t set_display_color(DisplayColor color)
			{
				uint8_t command = LCD::command_data(Command::DisplayNormal);
				if (color == DisplayColor::Normal) {
					this->write_commands(&command, 1);
					return ESP_OK;
				}
				else if (color == DisplayColor::Inverted) {
					command = LCD::command_data(Command::DisplayReverse);
					this->write_commands(&command, 1);
					return ESP_OK;
				}
				return ESP_FAIL;
			}

			esp_err_t set_contrast(uint8_t ratio)
			{
				if (ratio > 0b00000111) {
					return ESP_FAIL;
				}
				uint8_t command = LCD::command_data(Command::RegisterRatio) | ratio;
				this->write_commands(&command, 1);
				return ESP_OK;
			}

			esp_err_t set_contrast_detail(uint8_t volume)
			{
				if (volume > 0b00111111) {
					return ESP_FAIL;
				}
				uint8_t contrast = LCD::command_data(Command::ElectronicVolumeRegistereSet) | volume;
				uint8_t commands[2] = {
					LCD::command_data(Command::ElectronicVolumeModeSet),
					contrast
				};
				this->write_commands(commands, 2);
				return ESP_OK;
			}

			void set_sleep_mode(SleepMode mode)
			{
				uint8_t command = LCD::command_data(Command::SetSleepMode);
				if (mode == SleepMode::Enabled) {
					this->write_commands(&command, 1);
				}
				else {
					command = LCD::command_data(Command::SetNormalMode);
					this->write_commands(&command, 1);
				}
			}
		};
	}
}
}

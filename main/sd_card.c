/* SD card and FAT filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// This example uses SDMMC peripheral to communicate with SD card.

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "sd_card.h"

static const char *TAG = "sd_card";

#define MOUNT_POINT "/sdcard"
sdmmc_card_t *card;
const char mount_point[] = MOUNT_POINT;

FILE *pcmFile;

// const char *file_pcm = "/sdcard/audio_data.txt";
const char *file_pcm = MOUNT_POINT "/audio.pcm";

void sd_card_init(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Set bus width to use:
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.width = 4;
#else
    slot_config.width = 1;
#endif

    // On chips where the GPIOs used for SD card can be configured, set them in
    // the slot_config structure:
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = CONFIG_EXAMPLE_PIN_CLK;
    slot_config.cmd = CONFIG_EXAMPLE_PIN_CMD;
    slot_config.d0 = CONFIG_EXAMPLE_PIN_D0;
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.d1 = CONFIG_EXAMPLE_PIN_D1;
    slot_config.d2 = CONFIG_EXAMPLE_PIN_D2;
    slot_config.d3 = CONFIG_EXAMPLE_PIN_D3;
#endif // CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
#endif // CONFIG_SOC_SDMMC_USE_GPIO_MATRIX

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // write code to list files on SD Card
    DIR *d;
    struct dirent *dir;
    d = opendir(mount_point);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            ESP_LOGI(TAG, "%s", dir->d_name);
        }
        closedir(d);
    }
    else
    {
        ESP_LOGE(TAG, "Can't Open Dir.");
    }
}

void sd_card_create_file(void)
{
    // Check if destination file exists before renaming
    struct stat st;
    if (stat(file_pcm, &st) == 0)
    {
        // Delete it if it exists
        unlink(file_pcm);
        ESP_LOGW(TAG, "Existing file deleted");
    }

    pcmFile = fopen(file_pcm, "w+");
    if (pcmFile == NULL)
    {
        ESP_LOGE(TAG, "%s file for writing", file_pcm);
        return;
    }

    // ESP_LOGI(TAG, "Opening file %s", file_pcm);
    // pcmFile = fopen(file_pcm, "wb");
    // if (pcmFile == NULL)
    // {
    //     ESP_LOGE(TAG, "Failed to open file for writing");
    //     return;
    // }
    // ESP_LOGI(TAG, "File created");

    // uint8_t data_sample[] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    // sd_card_write_data(data_sample, sizeof(data_sample));

    // read_pcm_file();
}

void sd_card_write_data(const uint8_t *data, uint32_t len, size_t *bytes_written)
{
    static bool isFileClodes = false;

    // check parameters
    if (data == NULL || len == 0)
    {
        ESP_LOGE(TAG, "Invalid parameters");
        return;
    }

    *bytes_written += len;

    if (*bytes_written > (1220 * 1024) && !isFileClodes)
    {
        isFileClodes = true;
        ESP_LOGI(TAG, "Closing file (len = %ld)", len);
        fclose(pcmFile);
        ESP_LOGI(TAG, "File closed\n\n");
        *bytes_written = 0;

        // check the size of the file
        struct stat st;
        if (stat(file_pcm, &st) == 0)
        {
            ESP_LOGI(TAG, "File size: %ld bytes", st.st_size);
        }
    }

    if (!isFileClodes && pcmFile)
    {
        ESP_LOGI(TAG, "Writing data (len = %ld)", len);
        fwrite(data, 1, len, pcmFile);
    }
}

int read_pcm_file(void)
{
    // Open the file for reading in binary mode
    FILE *f = fopen(file_pcm, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Failed to open file for reading\n");
        return 1;
    }

    // Get the size of the file
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Allocate memory to store the PCM data
    uint8_t *pcm_data = (uint8_t *)malloc(file_size);
    if (pcm_data == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(f);
        return 1;
    }

    // Read the PCM data from the file
    size_t bytes_read = fread(pcm_data, 1, file_size, f);
    fclose(f);

    if (bytes_read != file_size)
    {
        fprintf(stderr, "Error reading PCM data from file\n");
        free(pcm_data);
        return 1;
    }

    // Print the first few bytes of the PCM data (adjust as needed)
    for (int i = 0; i < 16 && i < bytes_read; i++)
    {
        printf("%02X ", pcm_data[i]);
    }

    // Free allocated memory
    free(pcm_data);

    return 0;
}

void test_sd_card(void)
{
    // Use POSIX and C standard library functions to work with files:

    // First create a file.
    const char *file_hello = MOUNT_POINT "/hello.txt";

    ESP_LOGI(TAG, "Opening file %s", file_hello);
    FILE *f = fopen(file_hello, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello %s!\n", card->cid.name);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    const char *file_foo = MOUNT_POINT "/foo.txt";

    // Check if destination file exists before renaming
    struct stat st;
    if (stat(file_foo, &st) == 0)
    {
        // Delete it if it exists
        unlink(file_foo);
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file %s to %s", file_hello, file_foo);
    if (rename(file_hello, file_foo) != 0)
    {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file %s", file_foo);
    f = fopen(file_foo, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    // Read a line from file
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);

    // Strip newline
    char *pos = strchr(line, '\n');
    if (pos)
    {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    // All done, unmount partition and disable SDMMC peripheral
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");
}